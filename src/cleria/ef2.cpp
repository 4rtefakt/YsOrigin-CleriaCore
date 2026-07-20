#include "cleria/ef2.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace cleria {
namespace fs = std::filesystem;

namespace {
constexpr std::size_t kRegHeader = 8;    // {u32 zero; u32 count}
constexpr std::size_t kRegStride = 196;  // {u32 stored_id; char name[192]}

std::string read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string field(const std::string& b, std::size_t off, std::size_t max) {
    if (off + max > b.size()) return {};
    const char* p = b.data() + off;
    std::size_t n = 0;
    while (n < max && p[n] != '\0') ++n;
    return std::string(p, n);
}

// The extract stores names upper-cased with '\' as the separator; normalise a stored
// name into path components.
std::vector<std::string> split_upper(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\\' || c == '/') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Read one {id, name} registry into a vector indexed by stored id.
bool read_registry(const fs::path& file, std::vector<std::string>& out, int id_bias) {
    const std::string b = read_all(file);
    if (b.size() < kRegHeader) return false;
    std::uint32_t count = 0;
    std::memcpy(&count, b.data() + 4, 4);
    if (kRegHeader + std::size_t(count) * kRegStride > b.size()) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t o = kRegHeader + std::size_t(i) * kRegStride;
        std::uint32_t id = 0;
        std::memcpy(&id, b.data() + o, 4);
        const std::string nm = field(b, o + 4, 192);
        if (nm.empty()) continue;
        const std::size_t slot = std::size_t(int(id) + id_bias);
        if (slot >= out.size()) out.resize(slot + 1);
        out[slot] = nm;
    }
    return true;
}
}  // namespace

bool EftList::load(const fs::path& asset_root) {
    const fs::path d = asset_root / "EFFECT" / "EFT";
    // The three lists merge by flat slot; stored_id already equals the flat index for
    // all 640 records, so reading the stored id needs no per-file offset.
    bool any = false;
    any |= read_registry(d / "EFTLIST.DAT", names_, 0);
    any |= read_registry(d / "EFTLIST2.DAT", names_, 0);
    any |= read_registry(d / "EFTLIST3.DAT", names_, 0);
    return any && !names_.empty();
}

const std::string& EftList::name(int id) const {
    static const std::string kEmpty;
    if (id < 0 || std::size_t(id) >= names_.size()) return kEmpty;
    return names_[std::size_t(id)];
}

fs::path EftList::path(const fs::path& asset_root, int id) const {
    const std::string& nm = name(id);
    if (nm.empty()) return {};
    std::vector<std::string> parts = split_upper(nm);
    if (parts.empty()) return {};
    // The list says ".eft"; what ships is ".EF2".
    std::string& leaf = parts.back();
    const std::size_t dot = leaf.rfind('.');
    if (dot != std::string::npos) leaf.resize(dot);
    leaf += ".EF2";
    fs::path p = asset_root / "EFFECT" / "EFT";
    for (const std::string& s : parts) p /= s;
    std::error_code ec;
    return fs::exists(p, ec) ? p : fs::path();
}

bool EftTex::load(const fs::path& asset_root) {
    return read_registry(asset_root / "EFFECT" / "EFT" / "EFTTEX.DAT", names_, 0) &&
           !names_.empty();
}

fs::path EftTex::path(const fs::path& asset_root, int tex_id) const {
    if (tex_id <= 0 || std::size_t(tex_id) >= names_.size()) return {};
    const std::string& nm = names_[std::size_t(tex_id)];
    if (nm.empty()) return {};
    std::vector<std::string> parts = split_upper(nm);
    if (parts.empty()) return {};
    const std::string leaf = parts.back();
    parts.pop_back();
    for (const char* tier : {"H", "L"}) {
        fs::path p = asset_root / "EFFECT" / "EFT";
        for (const std::string& s : parts) p /= s;
        p /= tier;
        p /= leaf;
        std::error_code ec;
        if (fs::exists(p, ec)) return p;
    }
    return {};
}

Ef2 Ef2::load(const fs::path& file) {
    Ef2 e;
    const std::string b = read_all(file);
    if (b.size() < 32 || std::memcmp(b.data(), "EF2", 3) != 0) return e;
    std::uint32_t nA = 0, nB = 0;
    std::memcpy(&nA, b.data() + 0x14, 4);
    std::memcpy(&nB, b.data() + 0x18, 4);
    // A stride that merely fits is how a misaligned parse survives its first look, so
    // require the sizes to account for the file exactly (holds for 24/24 corpus files).
    constexpr std::size_t kB1 = 0xf0, kB2 = 0x924;
    if (nB == 0 || nB > 64 || nA > 64) return e;
    if (32 + std::size_t(nA) * 8 + std::size_t(nB) * (kB1 + kB2) != b.size()) return e;

    const std::size_t b1 = 0x20 + std::size_t(nA) * 8;
    const std::size_t b2 = b1 + std::size_t(nB) * kB1;
    e.emitters_.resize(nB);
    for (std::uint32_t i = 0; i < nB; ++i) {
        const std::size_t o1 = b1 + std::size_t(i) * kB1;
        const std::size_t o2 = b2 + std::size_t(i) * kB2;
        Ef2Emitter& em = e.emitters_[i];
        std::memcpy(&em.life, b.data() + o1 + 0x24, 4);
        std::memcpy(&em.sx, b.data() + o1 + 0x28, 4);
        std::memcpy(&em.sy, b.data() + o1 + 0x2c, 4);
        std::memcpy(&em.sz, b.data() + o1 + 0x30, 4);
        std::uint32_t fc = 0;
        std::memcpy(&fc, b.data() + o2 + 0x48, 4);
        if (fc > 16) fc = 16;  // the parser's own bound; 0 = draws no textured quad
        em.frames.resize(fc);
        for (std::uint32_t k = 0; k < fc; ++k) {
            Ef2Frame& fr = em.frames[k];
            std::memcpy(&fr.tex_id, b.data() + o2 + 0x4c + std::size_t(k) * 4, 4);
            float uv[4];
            std::memcpy(uv, b.data() + o2 + 0x8c + std::size_t(k) * 16, 16);
            fr.u0 = uv[0]; fr.v0 = uv[1]; fr.u1 = uv[2]; fr.v1 = uv[3];
        }
    }
    e.ok_ = true;
    return e;
}

const Ef2Emitter* Ef2::primary() const {
    for (const Ef2Emitter& em : emitters_)
        if (em.textured()) return &em;
    return nullptr;
}

std::uint32_t Ef2::life() const {
    std::uint32_t m = 0;
    for (const Ef2Emitter& em : emitters_)
        if (em.life > m) m = em.life;
    return m;
}

}  // namespace cleria
