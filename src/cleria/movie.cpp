#include "cleria/movie.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace cleria {
namespace fs = std::filesystem;

// ---- tables ---------------------------------------------------------------------------------

namespace {
// 0x6a0650[0..7] -> the wide strings at 0x6a0380..0x6a04ac [BYTES].
constexpr const char* kMovieFiles[kMovieCount] = {
    "en_pro.avi",    "yso_op.avi",    "yso_ins01.dat", "yso_ins02.dat",
    "yso_ins03.dat", "yso_logo.avi",  "yso_ed01.dat",  "yso_ed02.dat",
};
}  // namespace

const char* movie_file(int idx) {
    return idx >= 0 && idx < kMovieCount ? kMovieFiles[idx] : nullptr;
}

std::vector<const char*> movie_file_candidates(int idx) {
    std::vector<const char*> v;
    if (const char* f = movie_file(idx)) v.push_back(f);
    if (idx == 0) v.push_back("yso_pro.avi");   // [H] see the header
    return v;
}

fs::path find_movie(const fs::path& asset_root, int idx) {
    std::vector<fs::path> dirs;
    if (const char* e = std::getenv("CLERIA_MOVIE_DIR")) dirs.emplace_back(e);
    dirs.push_back(asset_root / "movie");
    dirs.push_back(asset_root.parent_path() / "release");
    for (const fs::path& d : dirs)
        for (const char* name : movie_file_candidates(idx)) {
            std::error_code ec;
            const fs::path p = d / name;
            if (!d.empty() && fs::is_regular_file(p, ec)) return p;
        }
    return {};
}

int demo_number(std::string_view s) {
    while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.remove_suffix(1);
    if (s.size() != 5) return 0;
    const char* d = "demo";
    for (int i = 0; i < 4; ++i)
        if (std::tolower((unsigned char)s[i]) != d[i]) return 0;
    const int n = s[4] - '0';
    return n >= 1 && n <= 8 ? n : 0;
}

std::optional<DemoScene> demo_scene(int n, bool toal) {
    // FUN_00431840: FUN_005773e0(next, idx) per stricmp arm [EXPORT 00431840.c L232..L567].
    switch (n) {
        case 1: return DemoScene{0, toal ? "s_10\\s_1000\\" : "s_00\\s_0002\\"};
        case 2: return DemoScene{1, toal ? "s_70\\s_7001\\" : "s_00\\s_0002\\"};
        case 3: return DemoScene{2, "s_10\\s_1000\\"};
        case 4: return DemoScene{3, "s_60\\s_6080\\"};
        case 5: return DemoScene{4, "s_70\\s_7002\\"};
        case 6: return DemoScene{5, "title\\"};
        case 7: return DemoScene{6, "s_00\\s_0003\\"};
        case 8: return DemoScene{7, "s_00\\s_0003\\"};
        default: return std::nullopt;
    }
}

bool movie_skippable(int demo, const MovieSeen& s) {
    switch (demo) {
        case 1: case 2: case 3: case 4: case 5: return s.first_play;
        case 6: return true;
        case 7: return s.first_play && (s.clear_7b || s.clear_7a);
        case 8: return s.first_play && s.clear_79;
        default: return false;
    }
}

MovieQuad movie_quad(std::uint32_t w, std::uint32_t h) {
    const double W = double(w), H = double(h);
    MovieQuad q;
    q.x0 = 0.0f;
    q.x1 = float(W);
    // 0x765190 (FUN_005d3ab0): 1.0, or H / (W/2) when (double)(W/2) < H (W/2 an int).
    const bool wide = !(double(w / 2) < H);
    if (wide) {                          // 0x577ad4 / 0x577b44: top 0, bottom 0x6bd778
        q.y0 = 0.0f;
        q.y1 = 320.0f;
        return q;
    }
    const double vh = (W < 2.0 * H) ? (W / (2.0 * H)) * H : H;   // the video height
    const double margin = (H - vh) * 0.5;                          // 0x6bd518
    if (vh == 320.0) {                   // the literal 320.0 case: no truncation
        q.y0 = float(margin + 0.0);      // + 0x6bcf98 (0.0)
        q.y1 = float(320.0 + margin);
    } else {
        q.y0 = float(double(int(margin + vh * 0.0)));
        q.y1 = float(double(int(margin + vh)));
    }
    return q;
}

std::optional<double> avi_duration(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<char> b(1 << 16);
    f.read(b.data(), std::streamsize(b.size()));
    const std::size_t n = std::size_t(f.gcount());
    if (n < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "AVI ", 4) != 0)
        return std::nullopt;
    for (std::size_t i = 12; i + 8 + 20 <= n; ++i) {
        if (std::memcmp(b.data() + i, "avih", 4) != 0) continue;
        std::uint32_t us = 0, frames = 0;
        std::memcpy(&us, b.data() + i + 8, 4);          // dwMicroSecPerFrame
        std::memcpy(&frames, b.data() + i + 8 + 16, 4); // dwTotalFrames
        if (us == 0 || frames == 0) return std::nullopt;
        return double(us) * double(frames) * 1e-6;
    }
    return std::nullopt;
}

std::size_t avi_patch_chunk_ids(std::uint8_t* d, std::size_t n) {
    auto u32 = [&](std::size_t o) {
        std::uint32_t v = 0;
        std::memcpy(&v, d + o, 4);
        return std::size_t(v);
    };
    auto is = [&](std::size_t o, const char* id) { return o + 4 <= n && std::memcmp(d + o, id, 4) == 0; };
    if (n < 12 || !is(0, "RIFF") || !is(8, "AVI ")) return 0;
    std::size_t count = 0;
    // The top-level chunks of the RIFF AVI form: walk them to the LIST movi and idx1.
    for (std::size_t o = 12; o + 8 <= n;) {
        const std::size_t sz = u32(o + 4);
        if (is(o, "LIST") && is(o + 8, "movi")) {
            const std::size_t end = std::min(n, o + 8 + sz);
            for (std::size_t c = o + 12; c + 8 <= end;) {
                if (is(c, "00xx")) { std::memcpy(d + c, "00dc", 4); ++count; }
                c += 8 + u32(c + 4) + (u32(c + 4) & 1);
            }
        } else if (is(o, "idx1")) {
            const std::size_t end = std::min(n, o + 8 + sz);
            for (std::size_t e = o + 8; e + 16 <= end; e += 16)
                if (is(e, "00xx")) { std::memcpy(d + e, "00dc", 4); ++count; }
        }
        o += 8 + sz + (sz & 1);
    }
    return count;
}

void nv12_to_bgra(const std::uint8_t* y_plane, const std::uint8_t* uv_plane, std::size_t pitch,
                  std::uint32_t w, std::uint32_t h, std::uint8_t* out) {
    // BT.601, video range, in 8.8 fixed point; one chroma pair per two pixels.
    static const auto lut = [] {
        struct T { int y[256], bu[256], gu[256], gv[256], rv[256]; std::uint8_t clamp[1024]; } t{};
        for (int i = 0; i < 256; ++i) {
            t.y[i] = 298 * (i - 16) + 128;
            t.bu[i] = 516 * (i - 128); t.gu[i] = -100 * (i - 128);
            t.gv[i] = -208 * (i - 128); t.rv[i] = 409 * (i - 128);
        }
        for (int i = 0; i < 1024; ++i) t.clamp[i] = std::uint8_t(std::clamp(i - 384, 0, 255));
        return t;
    }();
    const std::uint8_t* cl = lut.clamp + 384;   // [-384, 640): covers every reachable value
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint8_t* yr = y_plane + pitch * y;
        const std::uint8_t* cr = uv_plane + pitch * (y / 2);
        std::uint8_t* d = out + std::size_t(w) * 4 * y;
        for (std::uint32_t x = 0; x < w; x += 2) {
            const int u = cr[x], v = cr[x + 1];
            const int b = lut.bu[u], g = lut.gu[u] + lut.gv[v], r = lut.rv[v];
            for (std::uint32_t k = x; k < x + 2 && k < w; ++k) {
                const int c = lut.y[yr[k]];
                d[4 * k + 0] = cl[(c + b) >> 8];
                d[4 * k + 1] = cl[(c + g) >> 8];
                d[4 * k + 2] = cl[(c + r) >> 8];
                d[4 * k + 3] = 255;
            }
        }
    }
}

// ---- the decoder ----------------------------------------------------------------------------

#ifdef _WIN32
namespace {
template <class T> void release(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}
}  // namespace

struct MovieDecoder::Impl {
    bool com = false, mf = false;
    IMFSourceReader* reader = nullptr;
    IMFSample* next = nullptr;       // decoded, not shown yet
    LONGLONG next_ts = 0;            // 100 ns
    LONG stride = 0;                 // the NV12 output's default Y stride (bytes)
    bool eos = false;

    ~Impl() {
        release(next);
        release(reader);
        if (mf) MFShutdown();
        if (com) CoUninitialize();
    }

    // Reads the next video sample into `next`. False at the end of the stream or on error.
    bool read_next() {
        release(next);
        for (;;) {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* s = nullptr;
            const HRESULT hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
                                                  nullptr, &flags, &ts, &s);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR))) {
                release(s);
                eos = true;
                return false;
            }
            if (!s) continue;   // a gap / stream tick: no sample this read
            next = s;
            next_ts = ts;
            return true;
        }
    }
};

MovieDecoder::MovieDecoder() = default;
MovieDecoder::~MovieDecoder() { close(); }

bool MovieDecoder::is_open() const { return impl_ && impl_->reader; }

void MovieDecoder::close() {
    impl_.reset();
    w_ = h_ = 0;
    duration_ = 0;
    ended_ = false;
    frame_.clear();
}

bool MovieDecoder::open(const fs::path& file) {
    close();
    std::error_code ec;
    if (file.empty() || !fs::is_regular_file(file, ec)) return false;
    auto im = std::make_unique<Impl>();
    const HRESULT hc = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    im->com = SUCCEEDED(hc);   // RPC_E_CHANGED_MODE: COM is already up on this thread, fine
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    im->mf = true;

    // The whole file is read into memory and its '00xx' video chunk ids renamed (see
    // avi_patch_chunk_ids), then resolved from those bytes with an ".avi" URL hint: the demo
    // inserts are AVIs named *.dat.
    IStream* mem = nullptr;
    {
        std::ifstream f(file, std::ios::binary | std::ios::ate);
        const std::streamoff n = f ? std::streamoff(f.tellg()) : 0;
        HGLOBAL hg = n > 0 ? GlobalAlloc(GMEM_MOVEABLE, SIZE_T(n)) : nullptr;
        if (!hg) return false;
        auto* p = static_cast<std::uint8_t*>(GlobalLock(hg));
        f.seekg(0);
        const bool read = p && f.read(reinterpret_cast<char*>(p), n);
        if (read) avi_patch_chunk_ids(p, std::size_t(n));
        if (p) GlobalUnlock(hg);
        if (!read || FAILED(CreateStreamOnHGlobal(hg, TRUE, &mem))) {
            GlobalFree(hg);
            return false;
        }
    }
    IMFByteStream* bs = nullptr;
    IMFSourceResolver* res = nullptr;
    IUnknown* obj = nullptr;
    IMFMediaSource* src = nullptr;
    IMFAttributes* attr = nullptr;
    IMFMediaType* want = nullptr;
    IMFMediaType* got = nullptr;
    bool ok = false;
    do {
        if (FAILED(MFCreateMFByteStreamOnStream(mem, &bs))) break;
        if (FAILED(MFCreateSourceResolver(&res))) break;
        MF_OBJECT_TYPE ot = MF_OBJECT_INVALID;
        if (FAILED(res->CreateObjectFromByteStream(
                bs, L"movie.avi",
                MF_RESOLUTION_MEDIASOURCE | MF_RESOLUTION_CONTENT_DOES_NOT_HAVE_TO_MATCH_EXTENSION_OR_MIME_TYPE,
                nullptr, &ot, &obj)))
            break;
        if (FAILED(obj->QueryInterface(IID_PPV_ARGS(&src)))) break;
        if (FAILED(MFCreateAttributes(&attr, 1))) break;
        // The decoder's own NV12, converted here: the source reader's RGB32 video processor
        // costs ~10 ms a 1920x960 frame, the decode alone ~2 ms.
        if (FAILED(MFCreateSourceReaderFromMediaSource(src, attr, &im->reader))) break;
        im->reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        if (FAILED(im->reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE))) break;
        if (FAILED(MFCreateMediaType(&want))) break;
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (FAILED(im->reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, want)))
            break;
        if (FAILED(im->reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &got))) break;
        UINT32 w = 0, h = 0;
        if (FAILED(MFGetAttributeSize(got, MF_MT_FRAME_SIZE, &w, &h)) || !w || !h) break;
        UINT32 st = 0;
        im->stride = SUCCEEDED(got->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) ? LONG(INT32(st)) : LONG(w);
        w_ = w;
        h_ = h;
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(im->reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE),
                                                           MF_PD_DURATION, &pv)) && pv.vt == VT_UI8)
            duration_ = double(pv.uhVal.QuadPart) * 1e-7;
        PropVariantClear(&pv);
        ok = true;
    } while (false);
    release(got);
    release(want);
    release(attr);
    release(src);
    release(obj);
    release(res);
    release(bs);
    release(mem);
    if (!ok) {
        w_ = h_ = 0;
        duration_ = 0;
        return false;
    }
    frame_.assign(std::size_t(w_) * h_ * 4, 0);
    for (std::size_t i = 3; i < frame_.size(); i += 4) frame_[i] = 255;
    impl_ = std::move(im);
    return true;
}

bool MovieDecoder::advance(double t) {
    if (!impl_ || !impl_->reader || ended_) return false;
    Impl& im = *impl_;
    const LONGLONG tt = LONGLONG(t * 1e7);
    IMFSample* show = nullptr;
    for (;;) {
        if (!im.next && !im.eos) im.read_next();
        if (!im.next) break;                    // the end of the stream
        if (im.next_ts > tt + 20000) break;     // not due yet (2 ms of slack for t's rounding)
        release(show);
        show = im.next;                         // due: the newest due frame wins
        im.next = nullptr;
    }
    if (!show) {
        if (im.eos && !im.next) ended_ = true;
        return false;
    }
    // NV12: the Y plane (pitch x rows), then the interleaved U/V plane at half height. The
    // allocated row count can exceed the frame height, so it is taken from the buffer length.
    IMFMediaBuffer* buf = nullptr;
    bool changed = false;
    if (SUCCEEDED(show->ConvertToContiguousBuffer(&buf))) {
        IMF2DBuffer* b2 = nullptr;
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        DWORD len = 0;
        if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) &&
            (FAILED(b2->Lock2D(&scan0, &pitch)) || FAILED(b2->GetContiguousLength(&len)))) {
            if (scan0) b2->Unlock2D();
            scan0 = nullptr;
            release(b2);                        // fall back to the plain lock below
        }
        if (!b2 && SUCCEEDED(buf->Lock(&scan0, nullptr, &len))) pitch = im.stride;
        if (scan0 && pitch > 0) {
            const std::size_t rows = std::max<std::size_t>(h_, std::size_t(len) * 2 / (std::size_t(pitch) * 3));
            nv12_to_bgra(scan0, scan0 + std::size_t(pitch) * rows, std::size_t(pitch), w_, h_, frame_.data());
            changed = true;
        }
        if (scan0) {
            if (b2) b2->Unlock2D();
            else buf->Unlock();
        }
        release(b2);
        release(buf);
    }
    release(show);
    return changed;
}

#else   // no decoder on this platform: open() fails and the view uses its stand-in

struct MovieDecoder::Impl {};
MovieDecoder::MovieDecoder() = default;
MovieDecoder::~MovieDecoder() = default;
bool MovieDecoder::is_open() const { return false; }
void MovieDecoder::close() {}
bool MovieDecoder::open(const fs::path&) { return false; }
bool MovieDecoder::advance(double) { return false; }

#endif

}  // namespace cleria
