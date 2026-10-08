#include "cleria/ni_archive.hpp"

#include <cstring>
#include <fstream>
#include <system_error>

#include "miniz.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace cleria::ni {
namespace fs = std::filesystem;

namespace {
std::uint32_t rd32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
           std::uint32_t(p[3]) << 24;
}
void set_err(std::string* err, std::string msg) {
    if (err) *err = std::move(msg);
}
bool ends_with_z(std::string_view s) {
    return s.size() >= 2 && s[s.size() - 2] == '.' && (s.back() == 'z' || s.back() == 'Z');
}
std::FILE* open_read(const fs::path& p) {
#ifdef _WIN32
    return _wfopen(p.c_str(), L"rb");
#else
    return std::fopen(p.c_str(), "rb");
#endif
}
bool seek64(std::FILE* f, std::uint64_t off) {
#ifdef _WIN32
    return _fseeki64(f, static_cast<long long>(off), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}
}  // namespace

void decrypt(std::uint8_t* p, std::size_t n) {
    std::uint32_t k = kCipherInit;
    for (std::size_t i = 0; i < n; ++i) {
        k *= kCipherMul;
        p[i] = std::uint8_t(p[i] - std::uint8_t(k >> 16));
    }
}

#ifdef _WIN32
namespace {
std::string convert(std::string_view s, UINT from, UINT to) {
    if (s.empty()) return {};
    const int wn = MultiByteToWideChar(from, 0, s.data(), int(s.size()), nullptr, 0);
    if (wn <= 0) return std::string(s);
    std::wstring w(std::size_t(wn), L'\0');
    MultiByteToWideChar(from, 0, s.data(), int(s.size()), w.data(), wn);
    const int n = WideCharToMultiByte(to, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string(s);
    std::string out(std::size_t(n), '\0');
    WideCharToMultiByte(to, 0, w.data(), wn, out.data(), n, nullptr, nullptr);
    return out;
}
}  // namespace
std::string cp932_to_utf8(std::string_view s) { return convert(s, 932, CP_UTF8); }
std::string utf8_to_cp932(std::string_view s) { return convert(s, CP_UTF8, 932); }
#else
// Off Windows: iconv's CP932 (glibc and macOS libiconv both have it). Like the Windows path, an input
// it cannot convert comes back unchanged.
namespace {
std::string convert(std::string_view s, const char* from, const char* to) {
    if (s.empty()) return {};
    iconv_t cd = iconv_open(to, from);
    if (cd == iconv_t(-1)) return std::string(s);
    std::string in(s), out(s.size() * 4 + 16, '\0');
    char* ip = in.data();
    char* op = out.data();
    std::size_t il = in.size(), ol = out.size();
    const std::size_t r = iconv(cd, &ip, &il, &op, &ol);
    iconv_close(cd);
    if (r == std::size_t(-1)) return std::string(s);
    out.resize(out.size() - ol);
    return out;
}
}  // namespace
std::string cp932_to_utf8(std::string_view s) { return convert(s, "CP932", "UTF-8"); }
std::string utf8_to_cp932(std::string_view s) { return convert(s, "UTF-8", "CP932"); }
#endif

std::string clean_name(std::string_view p) {
    if (ends_with_z(p)) p.remove_suffix(2);
    std::string out(p);
    for (char& c : out) {
        if (c == '\\') c = '/';
        else if (c != '\0' && std::strchr("<>:\"|?*", c)) c = '_';
    }
    return out;
}

std::string lookup_key(std::string_view name) {
    if (ends_with_z(name)) name.remove_suffix(2);
    std::string k(name);
    for (char& c : k) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return k;
}

bool Archive::open(const fs::path& ni_path, std::string* err) {
    ni_ = ni_path;
    na_ = fs::path(ni_path).replace_extension(".na");
    entries_.clear();
    by_key_.clear();

    std::ifstream f(ni_path, std::ios::binary);
    if (!f) return set_err(err, "cannot open " + ni_path.string()), false;
    std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 16 || std::memcmp(d.data(), "NNI\0", 4) != 0)
        return set_err(err, ni_path.string() + ": not an NNI index"), false;
    const std::uint32_t n = rd32(&d[4]), names_size = rd32(&d[8]), flags = rd32(&d[12]);
    if (flags & 1u) return set_err(err, ni_path.string() + ": incremental-link index (flag 1) unsupported"), false;
    const std::uint64_t toc_bytes = std::uint64_t(n) * 16;
    if (16 + toc_bytes + names_size > d.size())
        return set_err(err, ni_path.string() + ": truncated index"), false;
    std::uint8_t* toc = d.data() + 16;
    std::uint8_t* names = toc + toc_bytes;
    decrypt(toc, std::size_t(toc_bytes));
    decrypt(names, names_size);

    std::error_code ec;
    na_size_ = fs::file_size(na_, ec);
    if (ec) return set_err(err, "cannot stat " + na_.string()), false;

    entries_.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint8_t* t = toc + std::size_t(i) * 16;
        Entry e;
        e.hash = rd32(t);
        e.size = rd32(t + 4);
        e.pos = rd32(t + 8);
        const std::uint32_t np = rd32(t + 12);
        if (np >= names_size) return set_err(err, ni_path.string() + ": name offset out of range"), false;
        std::size_t end = np;
        while (end < names_size && names[end]) ++end;
        e.raw.assign(reinterpret_cast<const char*>(names + np), end - np);
        e.compressed = ends_with_z(e.raw);
        e.name = clean_name(cp932_to_utf8(e.raw));
        if (std::uint64_t(e.pos) + e.size > na_size_)
            return set_err(err, e.name + ": extends past the end of " + na_.string()), false;
        by_key_.emplace(lookup_key(e.name), entries_.size());
        entries_.push_back(std::move(e));
    }
    return true;
}

const Entry* Archive::find(std::string_view name) const {
    // Keys come from the sanitised names; a raw "?"-name is looked up via its clean form.
    auto it = by_key_.find(lookup_key(clean_name(name)));
    return it == by_key_.end() ? nullptr : &entries_[it->second];
}

bool Archive::read(const Entry& e, std::vector<std::uint8_t>& out, std::string* err) const {
    NaReader r(*this);
    if (!r.ok()) return set_err(err, "cannot open " + na_.string()), false;
    return r.read(e, out, err);
}

NaReader::NaReader(const Archive& a) : a_(a), f_(open_read(a.na_path())) {}

NaReader::~NaReader() {
    if (f_) std::fclose(f_);
}

bool NaReader::read(const Entry& e, std::vector<std::uint8_t>& out, std::string* err) {
    out.clear();
    if (!f_) return set_err(err, "cannot open " + a_.na_path().string()), false;
    if (e.size == 0) return true;
    std::vector<std::uint8_t>& dst = e.compressed ? buf_ : out;
    dst.resize(e.size);
    if (!seek64(f_, e.pos) || std::fread(dst.data(), 1, e.size, f_) != e.size)
        return set_err(err, e.name + ": short read"), false;
    if (!e.compressed) return true;

    if (e.size < 8) return set_err(err, e.name + ": packed entry shorter than its prefix"), false;
    const std::uint32_t crc = rd32(&buf_[0]), usize = rd32(&buf_[4]);
    out.resize(usize);
    if (usize == 0) return crc == 0 ? true : (set_err(err, e.name + ": crc32 mismatch"), false);
    mz_ulong dlen = usize, slen = e.size - 8;
    const int rc = mz_uncompress2(out.data(), &dlen, buf_.data() + 8, &slen);
    if (rc != MZ_OK || dlen != usize)
        return set_err(err, e.name + ": inflate failed (" + std::to_string(rc) + ")"), false;
    if (mz_crc32(MZ_CRC32_INIT, out.data(), out.size()) != crc)
        return set_err(err, e.name + ": crc32 mismatch"), false;
    return true;
}

}  // namespace cleria::ni
