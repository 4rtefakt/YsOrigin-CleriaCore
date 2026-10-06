// cleria-movietest — the demo movies (src/cleria/movie, build/event_re/MOVIES.md): the engine's
// movie / demo-scene tables, the skip gate, the letterbox quad of FUN_005779e0, and — when the
// AVIs are present under <assets>/movie — the Media Foundation decode of real frames.
//
//   cleria-movietest <assets> [out.png] [movie idx] [seconds]
//
// With the AVIs and Media Foundation it decodes movie `idx` (default 1, the opening) up to
// `seconds` (default 20) and writes that frame as a PNG (default build/movie_frame.png). Without
// the files the decode checks are skipped, not failed.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cleria/movie.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[movie] %-76s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

// A minimal PNG writer: RGB8, zlib "stored" blocks (no compression), CRC32 + Adler-32.
static std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0) {
    c = ~c;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static bool write_png(const fs::path& path, std::uint32_t w, std::uint32_t h,
                      const std::vector<std::uint8_t>& bgra) {
    std::vector<std::uint8_t> raw;   // filter byte 0 + RGB per row
    raw.reserve(std::size_t(h) * (1 + w * 3));
    for (std::uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t* q = bgra.data() + (std::size_t(y) * w + x) * 4;
            raw.push_back(q[2]); raw.push_back(q[1]); raw.push_back(q[0]);
        }
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (std::size_t off = 0; off < raw.size() || off == 0;) {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - off);
        const bool last = off + len >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(std::uint8_t(len)); z.push_back(std::uint8_t(len >> 8));
        z.push_back(std::uint8_t(~len)); z.push_back(std::uint8_t(~len >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(off), raw.begin() + std::ptrdiff_t(off + len));
        off += len;
        if (last) break;
    }
    const std::uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back(std::uint8_t(ad >> s));
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto be32 = [&](std::uint32_t v) { const char c[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; f.write(c, 4); };
    auto chunk = [&](const char* type, const std::vector<std::uint8_t>& d) {
        be32(std::uint32_t(d.size()));
        std::vector<std::uint8_t> td(type, type + 4);
        td.insert(td.end(), d.begin(), d.end());
        f.write(reinterpret_cast<const char*>(td.data()), std::streamsize(td.size()));
        be32(crc32(td.data(), td.size()));
    };
    f.write("\x89PNG\r\n\x1a\n", 8);
    std::vector<std::uint8_t> ihdr = {std::uint8_t(w >> 24), std::uint8_t(w >> 16), std::uint8_t(w >> 8), std::uint8_t(w),
                                      std::uint8_t(h >> 24), std::uint8_t(h >> 16), std::uint8_t(h >> 8), std::uint8_t(h),
                                      8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return bool(f);
}

int main(int argc, char** argv) {
    const fs::path assets = argc > 1 ? fs::path(argv[1]) : fs::path("assets");
    const fs::path out = argc > 2 ? fs::path(argv[2]) : fs::path("build") / "movie_frame.png";
    const int pick = argc > 3 ? std::atoi(argv[3]) : 1;
    const double at = argc > 4 ? std::atof(argv[4]) : 20.0;

    // ---- tables [BYTES 0x6a0650 / EXPORT 00431840.c] ----
    check(std::strcmp(movie_file(0), "en_pro.avi") == 0 && std::strcmp(movie_file(1), "yso_op.avi") == 0 &&
              std::strcmp(movie_file(2), "yso_ins01.dat") == 0 && std::strcmp(movie_file(5), "yso_logo.avi") == 0 &&
              std::strcmp(movie_file(7), "yso_ed02.dat") == 0 && movie_file(8) == nullptr,
          "0x6a0650: 0 en_pro, 1 yso_op, 2 yso_ins01.dat, 5 yso_logo, 7 yso_ed02.dat");
    {
        const auto c = movie_file_candidates(0);
        check(c.size() == 2 && std::strcmp(c[1], "yso_pro.avi") == 0, "movie 0 falls back to the shipped yso_pro.avi [H]");
        check(movie_file_candidates(1).size() == 1, "the other movies have one name");
    }
    check(demo_number("demo3\\") == 3 && demo_number("DEMO6") == 6 && demo_number("demo9\\") == 0 &&
              demo_number("s_00\\s_0002\\") == 0 && demo_number("title\\") == 0,
          "demo_number: demo1..8 only");
    {
        auto d1 = demo_scene(1, false), d1t = demo_scene(1, true), d2t = demo_scene(2, true);
        auto d3 = demo_scene(3, false), d6 = demo_scene(6, false), d8 = demo_scene(8, true);
        check(d1 && d1->movie == 0 && std::strcmp(d1->next, "s_00\\s_0002\\") == 0, "demo1 -> movie 0 -> s_0002");
        check(d1t && std::strcmp(d1t->next, "s_10\\s_1000\\") == 0, "demo1 (Toal) -> s_1000");
        check(d2t && d2t->movie == 1 && std::strcmp(d2t->next, "s_70\\s_7001\\") == 0, "demo2 (Toal) -> movie 1 -> s_7001");
        check(d3 && d3->movie == 2 && std::strcmp(d3->next, "s_10\\s_1000\\") == 0, "demo3 -> movie 2 -> s_1000");
        check(d6 && d6->movie == 5 && std::strcmp(d6->next, "title\\") == 0, "demo6 -> the logo -> title");
        check(d8 && d8->movie == 7 && std::strcmp(d8->next, "s_00\\s_0003\\") == 0, "demo8 -> movie 7 -> s_0003");
        check(!demo_scene(0, false) && !demo_scene(9, false), "no demo0 / demo9");
    }

    // ---- the skip gate (FUN_00577750) ----
    {
        MovieSeen fresh;
        check(movie_skippable(6, fresh) && !movie_skippable(1, fresh) && !movie_skippable(3, fresh),
              "a fresh system: only the logo (demo6) can be skipped");
        MovieSeen seen; seen.first_play = true;
        check(movie_skippable(1, seen) && movie_skippable(5, seen) && !movie_skippable(7, seen) &&
                  !movie_skippable(8, seen),
              "GameFirstPlay: demo1..5; demo7/8 still need their clear bytes");
        seen.clear_7a = true;
        check(movie_skippable(7, seen) && !movie_skippable(8, seen), "demo7 with 0x74ab7a");
        seen.clear_79 = true;
        check(movie_skippable(8, seen), "demo8 with 0x74ab79");
        MovieSkip s;
        check(!s.step(true, true), "a button held from before the movie does not skip (not armed)");
        check(!s.step(false, true) && s.armed, "a tick with it released arms the gate");
        check(s.step(true, true), "the next press skips");
        MovieSkip u;
        u.step(false, false);
        check(!u.step(true, false), "an unskippable movie ignores the press");
    }

    // ---- the quad (FUN_005779e0) ----
    {
        auto eq = [](MovieQuad q, float x0, float y0, float x1, float y1) {
            return q.x0 == x0 && q.y0 == y0 && q.x1 == x1 && q.y1 == y1;
        };
        check(eq(movie_quad(640, 480), 0, 80, 640, 400), "640x480: h 320, rows 80..400");
        check(eq(movie_quad(1280, 800), 0, 80, 1280, 720), "1280x800: h 640, rows 80..720");
        check(eq(movie_quad(1024, 768), 0, 128, 1024, 640), "1024x768: h 512, rows 128..640");
        check(eq(movie_quad(1920, 1080), 0, 60, 1920, 1020), "1920x1080: h 960, rows 60..1020");
        check(eq(movie_quad(641, 480), 0, 79, 641, 400), "641x480: h 320.5, truncated (79.75 -> 79, 400.25 -> 400)");
        check(eq(movie_quad(640, 481), 0, 80.5f, 640, 400.5f), "640x481: h == 320.0, not truncated");
        check(eq(movie_quad(2560, 1080), 0, 0, 2560, 320), "2560x1080 (>= 2:1): the retail quirk, rows 0..320");
    }

    // ---- the '00xx' chunk ids ----
    {
        // RIFF AVI { LIST movi { 00xx(2) 01wb(1) 00dc(0) } idx1 { 00xx, 01wb } }
        std::vector<std::uint8_t> a;
        auto put = [&](const char* s4) { a.insert(a.end(), s4, s4 + 4); };
        auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) a.push_back(std::uint8_t(v >> (8 * k))); };
        put("RIFF"); u32(0); put("AVI ");
        put("LIST"); u32(4 + 10 + 10 + 8); put("movi");
        put("00xx"); u32(2); a.push_back(1); a.push_back(2);
        put("01wb"); u32(1); a.push_back(9); a.push_back(0);   // odd size + pad
        put("00dc"); u32(0);
        put("idx1"); u32(32);
        put("00xx"); u32(0x10); u32(4); u32(2);
        put("01wb"); u32(0); u32(14); u32(1);
        const std::size_t k = avi_patch_chunk_ids(a.data(), a.size());
        check(k == 2 && std::memcmp(a.data() + 24, "00dc", 4) == 0 && std::memcmp(a.data() + 34, "01wb", 4) == 0 &&
                  std::memcmp(a.data() + 60, "00dc", 4) == 0 && std::memcmp(a.data() + 76, "01wb", 4) == 0,
              "'00xx' -> '00dc' in movi and idx1; '01wb' untouched");
        std::vector<std::uint8_t> junk = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
        check(avi_patch_chunk_ids(junk.data(), junk.size()) == 0, "not an AVI: untouched");
    }

    // ---- NV12 -> BGRA, BT.601 video range ----
    {
        // a 2x2 frame, pitch 2: Y row 0 = {235, 16}, row 1 = {81, 81}; one chroma pair
        const std::uint8_t yp[4] = {235, 16, 81, 81};
        const std::uint8_t grey[2] = {128, 128}, red[2] = {90, 240};
        std::uint8_t o[16] = {};
        nv12_to_bgra(yp, grey, 2, 2, 1, o);
        check(o[0] == 255 && o[1] == 255 && o[2] == 255 && o[3] == 255 && o[4] == 0 && o[5] == 0 && o[6] == 0,
              "Y 235 -> white, Y 16 -> black (video range)");
        nv12_to_bgra(yp + 2, red, 2, 2, 1, o);
        check(o[2] >= 253 && o[1] <= 1 && o[0] <= 1, "Y 81 Cb 90 Cr 240 -> BT.601 red");
    }

    // ---- the files and the decoder ----
    const fs::path logo = find_movie(assets, 5);
    if (logo.empty()) {
        std::printf("[movie] SKIP the decode checks: no movies under %s/movie (copy release\\*.avi, *.dat)\n",
                    assets.string().c_str());
    } else {
        const auto dur = avi_duration(logo);
        check(dur && std::fabs(*dur - 4920 * 16666e-6) < 1e-6, "yso_logo.avi avih: 4920 frames x 16666 us (82.0 s)");
        const fs::path pro = find_movie(assets, 0);
        const auto dp = avi_duration(pro);
        check(!pro.empty() && dp && std::fabs(*dp - 218.0) < 0.01, "movie 0 resolves (yso_pro.avi), 218.0 s = BGM 19");
        const fs::path ins = find_movie(assets, 2);
        check(!ins.empty() && avi_duration(ins).has_value(), "yso_ins01.dat is an AVI too");
        {
            std::ifstream f(logo, std::ios::binary);
            std::vector<std::uint8_t> img{std::istreambuf_iterator<char>(f), {}};
            const std::size_t k = avi_patch_chunk_ids(img.data(), img.size());
            check(k == 2 * 4920, "yso_logo.avi: all 4920 video chunks + 4920 idx1 entries are '00xx'");
        }

        const fs::path file = find_movie(assets, pick);
        MovieDecoder dec;
        const auto t0 = std::chrono::steady_clock::now();
        const bool opened = dec.open(file);
        check(opened, "Media Foundation opens the movie (XviD in AVI)");
        if (opened) {
            check(dec.width() == 1920 && dec.height() == 960, "1920 x 960");
            std::printf("[movie] %s: %ux%u, %.2f s\n", file.filename().string().c_str(), dec.width(), dec.height(),
                        dec.duration());
            // Decode frame by frame up to `at` (every frame, as the view does at 60 Hz).
            int changes = 0;
            for (double t = 0; t <= at + 1e-9; t += 1.0 / 60.0) changes += dec.advance(t);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("[movie] %d frames to %.2f s in %.2f s wall (%.1f ms / frame)\n", changes, at, secs,
                        changes ? secs * 1000.0 / changes : 0.0);
            check(changes >= int(at * 60) - 1, "every 60 fps frame is shown on its tick");
            std::size_t lit = 0;
            const auto& px = dec.bgra();
            for (std::size_t i = 0; i < px.size(); i += 4) lit += (px[i] + px[i + 1] + px[i + 2]) > 48;
            std::printf("[movie] lit pixels: %zu of %u\n", lit, dec.width() * dec.height());
            check(lit > 0, "the frame has picture content");
            std::error_code ec;
            fs::create_directories(out.parent_path(), ec);
            check(write_png(out, dec.width(), dec.height(), px), "frame written");
            std::printf("[movie] wrote %s\n", out.string().c_str());
            // The end of the stream: open the short logo-less insert and run past its end.
            if (pick != 2) {
                MovieDecoder e;
                if (e.open(find_movie(assets, 2))) {
                    const double d = e.duration();
                    for (double t = d - 0.5; t < d + 0.5 && !e.ended(); t += 1.0 / 60.0) e.advance(t);
                    // Seeking by decode is slow; we jump: advance() drops frames up to t.
                    check(e.ended(), "the stream ends after its last frame");
                }
            }
        }
    }

    std::printf(fails ? "[movie] %d FAILED\n" : "[movie] all passed\n", fails);
    return fails ? 1 : 0;
}
