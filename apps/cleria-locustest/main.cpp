// cleria-locustest — the GoLocusSword hit arm (src/cleria/locus) against the SL2 data and the
// bytes of FUN_00464960 / FUN_005002e0 (build/ability_re/LOCUS.md, ABILITIES_VERIFIED.md §3.1).
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>

#include "cleria/aso.hpp"
#include "cleria/hit_volume.hpp"
#include "cleria/locus.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[locus] %-74s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static float len3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

static std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A one-row synthetic SL2 for the geometry checks.
static Sl2 synth(std::vector<Vec3> tips, std::vector<float> adv, float row_scale = 1.0f, float file_scale = 1.0f) {
    Sl2 s;
    s.nframes = std::int32_t(tips.size()); s.nverts = 1; s.scale = file_scale;
    s.rows.push_back({0, s.nframes, row_scale});
    s.tip = std::move(tips); s.advance = std::move(adv);
    return s;
}

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path("assets");

    // ---- the SL2 files (FUN_00453e90) ----
    Sl2Set set;
    check(set.load(root), "SWORD0..5.SL2 load (FUN_00453e90 layout, sizes consistent)");
    const int nrows[6] = {15, 6, 8, 7, 8, 23}, nfr[6] = {91, 46, 51, 38, 68, 195};
    bool shape_ok = true;
    for (int i = 0; i < 6; ++i)
        shape_ok = shape_ok && int(set.sword[i].rows.size()) == nrows[i] && set.sword[i].nframes == nfr[i] &&
                   set.sword[i].nverts == 38 && set.sword[i].scale == 1.0f;
    check(shape_ok, "row / frame counts 15,6,8,7,8,23 / 91,46,51,38,68,195; 38 verts; file scale 1.0");
    const Sl2& s4 = set.sword[4];
    if (s4.rows.size() > 7) {
        const Sl2::Row& r7 = s4.rows[7];
        check(r7.start == 58 && r7.end == 68 && near(r7.scale, 1.2f), "SWORD4 row 7 (Whirlwind) = start 58, end 68, scale 1.2");
        float lo = 1e9f, hi = 0, ylo = 1e9f, yhi = -1e9f;
        for (int f = 58; f < 68; ++f) {
            const Vec3& v = s4.tip[std::size_t(f)];
            lo = std::min(lo, len3(v)); hi = std::max(hi, len3(v));
            ylo = std::min(ylo, v.y); yhi = std::max(yhi, v.y);
        }
        std::printf("        row 7 raw tip |v| %.3f..%.3f, y %.3f..%.3f\n", lo, hi, ylo, yhi);
        check(near(lo, 1.92f, 0.01f) && near(hi, 2.25f, 0.01f) && near(ylo, 0.63f, 0.001f) && near(yhi, 0.63f, 0.001f),
              "row 7 tip (vertex 0, UNscaled) |v| 1.92..2.25 at y 0.63");
    } else check(false, "SWORD4 has a row 7");
    check(set.sword[1].rows.size() == 6, "SWORD1 has only rows 0..5 (Yunica's rows 6/7 need SWORD4)");

    // ---- the file choice (FUN_00464960 0x464a5f..) ----
    check(locus_sword_index(0xd, 1, 1) == 4 && locus_sword_index(0xd, 1, 0) == 1, "Yunica: SWORD4 with g_flags[149], else SWORD1");
    check(locus_sword_index(0xd, 2, 0) == 2 && locus_sword_index(0xd, 3, 1) == 3, "Hugo SWORD2, Toal SWORD3 (flag 149 ignored)");
    check(locus_sword_index(0xd, 0, 0) == 0 && locus_sword_index(0xd, 0, 0, true) == 5, "Adol SWORD0 (SWORD5 in Extra mode)");
    check(locus_sword_index(0xd, 7, 1) == 0, "an unknown character (> 3) falls back to SWORD0");
    check(locus_sword_index(0xa6, 0, 1) == 4 && locus_sword_index(0xa8, 0, 0) == 3 && locus_sword_index(0x60, 1, 1) == 0,
          "cast 0xa6 = the Yunica rule, 0xa8 = SWORD3, others SWORD0");

    // ---- spawn: Whirlwind GenerateLocus(263, 0,0,0, 56,66, 1, 1.5, 35, 10) ----
    LocusArgs ww; ww.row = 263; ww.clank_lo = 56; ww.clank_hi = 66; ww.bonus = 1.0f; ww.knock = 1.5f; ww.life = 35; ww.rehit = 10;
    LocusSword L;
    check(L.spawn(s4, ww, 0x1000123u), "Whirlwind spawns on SWORD4");
    check(L.frame() == 58.0f && L.end_frame() == 67.0f && near(L.row_scale(), 1.2f), "frame 58, end 67 (= end - 1), scale 1.2");
    check(L.life() == 35 && L.rehit() == 10 && L.bonus() == 1.0f && L.knock() == 1.5f && L.token() == 0x1000123u,
          "life 35, re-hit 10, bonus 1.0, knock 1.5, the swing's token");
    {
        LocusArgs z = ww; z.rehit = 0; z.life = 0;
        LocusSword Z; Z.spawn(s4, z, 1);
        check(Z.rehit() == 15 && Z.life() == 15, "re-hit 0 -> 15, life 0 -> the re-hit value");
        z.rehit = 9; LocusSword Y; Y.spawn(s4, z, 1);
        check(Y.life() == 9, "life 0 -> re-hit 9");
        LocusSword X; LocusArgs bad = ww; bad.row = 0x100 + 6;
        check(!X.spawn(set.sword[1], bad, 1), "a row past the file's table is refused [H]");
        LocusSword W; W.spawn(s4, ww, 1, /*flag215*/1);
        LocusSword V; V.spawn(set.sword[3], LocusArgs{}, 1, 0, 0x2000, 3);
        LocusSword U; U.spawn(set.sword[3], LocusArgs{}, 1, 0, 0x2000, 1);
        check(near(W.row_scale(), 1.8f) && near(V.row_scale(), 0.9f * 1.4f) && near(U.row_scale(), 0.9f),
              "row scale x1.5 with g_flags[215]; x1.4 for Toal with bc80 & 0x2000");
    }

    // ---- the per-tick run: Whirlwind's hit window ----
    {
        LocusSword R; R.spawn(s4, ww, 1);
        LocusSword::Owner o; o.origin = {0, 1.075f, 0}; o.floor_off = 1.075f; o.facing = 0;
        int first = 0, last = 0, n = 0, died = 0;
        float rmin = 1e9f, rmax = 0;
        bool radii = true, mid = true, frac = true;
        for (int t = 1; t <= 60; ++t) {
            if (!R.tick(o)) { died = t; break; }
            if (!R.armed()) continue;
            ++n; if (!first) first = t; last = t;
            const auto& sp = R.spheres();
            for (const HitShape& h : sp) radii = radii && h.radius == 0.45f && h.shape == 0;
            const Vec3 fl{0, 0, 0};
            const float tipd = len3(R.tip());
            // centre_k = floor + tip * (1 - c_k * 0.45 / |tip|): the tip sphere sits 0.09 inside the
            // tip, the body sphere 1.125 back from it (past the floor point when |tip| < 1.125).
            auto at = [&](float back) {
                const float f = 1.0f - back / tipd;
                return Vec3{fl.x + R.tip().x * f, fl.y + R.tip().y * f, fl.z + R.tip().z * f};
            };
            frac = frac && len3(sub(sp[0].centre, at(0.2f * 0.45f))) < 1e-4f &&
                   len3(sub(sp[1].centre, at(2.5f * 0.45f))) < 1e-4f;
            const Vec3 m{(sp[0].centre.x + sp[1].centre.x) * 0.5f, (sp[0].centre.y + sp[1].centre.y) * 0.5f,
                         (sp[0].centre.z + sp[1].centre.z) * 0.5f};
            mid = mid && len3(sub(m, sp[2].centre)) < 1e-5f;
            const float hr = std::sqrt(R.tip().x * R.tip().x + R.tip().z * R.tip().z);
            rmin = std::min(rmin, hr); rmax = std::max(rmax, hr);
        }
        std::printf("        Whirlwind: scanned ticks %d..%d (%d), dies on tick %d, tip horizontal reach %.2f..%.2f\n",
                    first, last, n, died, rmin, rmax);
        check(first == 1 && last == 24 && n == 24 && died == 25, "Whirlwind scans on ticks 1..24 and dies on tick 25 (frame >= 67)");
        check(radii, "three spheres, r 0.45 each");
        check(frac, "on floor->tip: sphere 0 at 1 - 0.2 r', sphere 1 at 1 - 2.5 r' (r' = 0.45/|P|)");
        check(mid, "sphere 2 = the midpoint of spheres 0 and 1");
    }
    {   // a short life runs out before the frames do (FUN_004fed50 decrements before the test)
        LocusArgs a = ww; a.life = 3;
        LocusSword R; R.spawn(s4, a, 1);
        LocusSword::Owner o;
        int n = 0, alive = 0;
        for (int t = 0; t < 40; ++t) { if (!R.tick(o)) break; ++alive; n += R.armed(); }
        check(n == 3 && alive == 24, "life 3: exactly 3 scanned ticks; the trail lives on unscanned");
    }

    // ---- geometry: the rotation, the y scale, the lerp ----
    {
        const Sl2 s = synth({{0, 1.0f, -2.0f}, {0, 1.0f, -2.0f}, {0, 1.0f, -2.0f}}, {0.5f, 0.5f, 0.5f});
        LocusArgs a; a.life = 99;
        LocusSword R; R.spawn(s, a, 1);
        LocusSword::Owner o; o.origin = {10, 5, 20}; o.floor_off = 1.0f; o.facing = 0;
        R.tick(o);
        check(near(R.tip().x, 2.0f) && near(R.tip().z, 0.0f) && near(R.tip().y, 1.0f),
              "facing 0 (+X): local -Z maps forward (rot = -facing - pi/2)");
        check(near(R.spheres()[0].centre.y, 4.0f + 1.0f * (1.0f - 0.2f * 0.45f / std::sqrt(5.0f))),
              "spheres hang off the FLOOR point (origin.y - +0x324)");
        LocusSword Q; Q.spawn(s, a, 1); o.facing = 1.5707964f; Q.tick(o);
        check(near(Q.tip().x, 0.0f) && near(Q.tip().z, 2.0f), "facing pi/2 (+Z): the tip swings to +Z");
        const Sl2 s2 = synth({{0, 1.0f, -2.0f}, {0, 1.0f, -2.0f}, {0, 1.0f, -2.0f}}, {0.5f, 0.5f, 0.5f}, 1.5f, 2.0f);
        LocusSword S; S.spawn(s2, a, 1); o.facing = 0; S.tick(o);
        check(near(S.tip().x, 2.0f * 1.5f * 2.0f) && near(S.tip().y, 2.0f * 1.0f * (1.5f + 1.0f) * 0.5f),
              "x,z x row scale; y x (scale + 1) / 2; then x the file scale");
        const Sl2 s3 = synth({{0, 0, -1.0f}, {0, 0, -3.0f}, {0, 0, -3.0f}, {0, 0, -3.0f}}, {0.25f, 0.25f, 0.25f, 0.25f});
        LocusSword T; T.spawn(s3, a, 1); T.tick(o);
        check(near(T.frame(), 0.25f) && near(T.tip().x, 1.5f), "frame 0.25 lerps vertex 0 a quarter of the way");
    }

    // ---- the freeze (+0x180) and the owner-death fade ----
    {
        LocusSword R; R.spawn(s4, ww, 1);
        LocusSword::Owner o;
        R.tick(o);
        const float f1 = R.frame();
        R.freeze();
        R.tick(o);
        check(R.frame() == f1, "a landed hit holds the frame for one tick");
        R.tick(o);
        check(R.frame() > f1, "then it advances again");
        LocusSword D; D.spawn(s4, ww, 1);
        o.dead = true;
        int t = 0;
        while (D.tick(o) && t < 40) ++t;
        check(t == 4, "owner HP < 1: the fade starts (0.15 x2 per tick), the locus dies on the 5th tick");
    }

    // ---- shared token with the swing (FUN_004faac0) ----
    {
        RehitBank bank;
        const std::uint32_t tok = 0x1000042u;
        check(!bank.check_and_record(tok, 30), "the body box lands first and records the token");
        check(bank.check_and_record(tok, 10), "the locus with the SAME token is refused while that slot runs");
        RehitBank b2;
        b2.check_and_record(tok, 10);
        int ok_at = 0;
        for (int t = 1; t <= 20 && !ok_at; ++t) { b2.tick(); if (!b2.check_and_record(tok, 10)) ok_at = t; }
        check(ok_at == 10, "Whirlwind alone re-hits the same victim every 10 ticks");
    }

    // ---- reach: a Yunica combo swing hits at ~1.8u where the body box cannot ----
    {
        // C_010 section 256: GenerateCollision(0, 0.5, 0.6, 0, -0.5, ...) + GenerateLocus(256, 0,0,0, 0,3, 8, 1, 24, 24).
        LocusArgs a; a.row = 256; a.clank_hi = 3; a.bonus = 8; a.knock = 1; a.life = 24; a.rehit = 24;
        const Vec3 origin{0, 1.075f, 0};
        const float hurt_r = 0.45f;   // a typical small monster's CASTINFO +0x30
        float a_ang = 0;
        for (int file : {1, 4}) {
            float best = 0;
            for (float ang = -1.2f; ang <= 1.2f; ang += 0.05f) {      // victims around the facing
                for (float d = 1.0f; d <= 5.0f; d += 0.05f) {
                    const HitShape body = body_shape({std::cos(ang) * d, 1.075f, std::sin(ang) * d}, hurt_r, 0, 0, -0.4f);
                    LocusSword R; R.spawn(set.sword[file], a, 1);
                    LocusSword::Owner o; o.origin = origin; o.floor_off = 1.075f;
                    bool hit = false;
                    while (R.tick(o) && !hit)
                        if (R.armed()) for (const HitShape& h : R.spheres()) hit = hit || overlap(h, body);
                    if (hit && d > best) { best = d; a_ang = ang; }
                }
            }
            HitVolume box; box.radius = 0.5f; box.fwd = 0.6f; box.voff = -0.5f;
            float box_best = 0;
            for (float d = 0.5f; d <= 3.5f; d += 0.05f)
                if (overlap(volume_shape(box, origin, 0), body_shape({d, 1.075f, 0}, hurt_r, 0, 0, -0.4f))) box_best = d;
            std::printf("        SWORD%d row 0: the locus reaches a victim centre %.2fu out (angle %.2f); the body box %.2fu\n",
                        file, best, a_ang, box_best);
            check(best >= 1.8f && box_best < 1.6f, file == 1 ? "SWORD1 row 0 reaches >= 1.8u; the r 0.5 / fwd 0.6 box does not"
                                                               : "SWORD4 row 0 reaches >= 1.8u; the r 0.5 / fwd 0.6 box does not");
        }
    }

    // ---- the ASO surfaces GenerateLocus (C_010 section 269 = Whirlwind) ----
    {
        const std::vector<std::uint8_t> b = read_file(root / "ANI" / "C_010.ASO");
        Aso aso = Aso::parse(b);
        AsoVm vm; vm.bind(&aso);
        bool got = false; AsoVm::LocusEvent ev;
        if (vm.set_section(269, true))
            for (int t = 0; t < 60 && !got; ++t) {
                vm.tick();
                if (!vm.loci().empty()) { got = true; ev = vm.loci().front(); }
            }
        check(got && ev.i[0] == 263 && ev.i[4] == 56 && ev.i[5] == 66 && ev.bonus == 1.0f && ev.knock == 1.5f &&
                  ev.i[8] == 35 && ev.i[9] == 10,
              "C_010 s269 fires GenerateLocus(263, 0,0,0, 56,66, 1.0, 1.5, 35, 10)");
    }

    // ---- the trail's draw (FUN_004581c0, FUN_00464960's texture choice) ----
    {
        bool uv = true;
        for (int i = 0; i < 6; ++i) {
            const Sl2& s = set.sword[i];
            if (s.u.size() != 38 || s.alpha.size() != std::size_t(s.nframes)) { uv = false; continue; }
            for (int k = 0; k < 38; ++k)
                uv = uv && near(s.u[std::size_t(k)], float(k / 2) / 18.0f) && s.v[std::size_t(k)] == float(k & 1);
        }
        check(uv, "every SWORD template: a 19 x 2 strip, u = column / 18, v 0 outer / 1 inner");
        const Sl2& s1 = set.sword[1];
        std::vector<Vec3> p; std::uint8_t a = 0;
        const int f0 = s1.rows.empty() ? 0 : s1.rows[0].start;
        bool ok = locus_strip(s1, float(f0), 1.0f, 1.5f, p, a) && p.size() == 38;
        for (int k = 0; ok && k < 38; ++k) {
            const Vec3& q = s1.verts[std::size_t(f0) * 38 + std::size_t(k)];
            ok = (k & 1) ? (p[std::size_t(k)].x == q.x && p[std::size_t(k)].y == q.y && p[std::size_t(k)].z == q.z)
                         : (near(p[std::size_t(k)].x, q.x * 1.5f) && near(p[std::size_t(k)].y, q.y * 1.25f) &&
                            near(p[std::size_t(k)].z, q.z * 1.5f));
        }
        check(ok, "strip at a whole frame: the frame's vertices, the outer edge x 1.5 (y x 1.25), the inner as is");
        Sl2 sy;
        sy.nframes = 2; sy.nverts = 2; sy.u = {0, 0}; sy.v = {0, 1}; sy.alpha = {1.0f, 0.0f};
        sy.verts = {{0, 0, 0}, {0, 0, 0}, {2, 4, 6}, {2, 4, 6}};
        locus_strip(sy, 0.5f, 1.0f, 2.0f, p, a);
        check(near(p[0].x, 2.0f) && near(p[0].y, 3.0f) && near(p[0].z, 6.0f) && near(p[1].x, 1.0f) && near(p[1].z, 3.0f) && a == 128,
              "strip between frames: lerped, then the outer edge scaled; alpha 0.5 -> 128");
        std::uint8_t a1 = 0, a2 = 0, a3 = 0;
        sy.alpha = {1.3f, 1.3f};
        locus_strip(sy, 0.0f, 1.0f, 1.0f, p, a1);
        locus_strip(sy, 0.0f, 0.1f, 1.0f, p, a2);
        sy.alpha = {0.999f, 0.999f};
        locus_strip(sy, 0.0f, 1.0f, 1.0f, p, a3);
        check(a1 == 255 && a2 == 33 && a3 == 255, "the diffuse byte: 1.3 -> 255, 0.13 -> 33, 0.999 -> 255");
    }
    check(locus_texture(0xd, 0, 0, false, 0x100, 0x100, 3, false) == 0x10 && locus_texture(0xd, 0, 0, false, 0x10e, 0x100, 3, false) == 0x19,
          "Adol: locus0, locus_sp0 for rows 0x10D / 0x10E");
    check(locus_texture(0xd, 0, 0, true, 0x100, 0x112, 0, false) == 0x18 && locus_texture(0xd, 0, 0, true, 0x100, 0x113, 0, false) == 0x1a &&
              locus_texture(0xd, 0, 0, true, 0x100, 0x120, 1, false) == 0x1c && locus_texture(0xd, 0, 0, true, 0x100, 0x120, 2, false) == 0x1b &&
              locus_texture(0xd, 0, 0, true, 0x100, 0x120, 3, false) == 0x18,
          "Adol Napishtim: locus5 below section 0x113, then Ventus 6 / Terra 8 / Ignis 7 / none 5");
    check(locus_texture(0xd, 1, 0, false, 0x100, 0x100, 0, false) == 0x11 && locus_texture(0xd, 1, 0, false, 0x100, 0x110, 0, false) == 0x12 &&
              locus_texture(0xd, 1, 1, false, 0x100, 0x100, 0, false) == 0x16 && locus_texture(0xd, 1, 1, false, 0x100, 0x10d, 0, false) == 0x17,
          "Yunica: locus1 (_ex2 in sections 0x10D / 0x110), locus4 (_ex2) with g_flags[149]");
    check(locus_texture(0xd, 2, 0, false, 0x100, 0x100, 0, true) == 0x13 && locus_texture(0xd, 3, 0, false, 0x100, 0x100, 0, false) == 0x14 &&
              locus_texture(0xd, 3, 0, false, 0x100, 0x100, 0, true) == 0x15 && locus_texture(0xa8, 0, 0, false, 0, 0, 0, true) == 0x15 &&
              locus_texture(0xa6, 0, 1, false, 0, 0, 0, false) == 0x16 && locus_texture(0x60, 1, 0, false, 0, 0, 0, false) == 0x10,
          "Hugo locus2, Toal locus3 (_ex boosting), cast 0xa8 as Toal, 0xa6 as Yunica, others locus0");
    {
        bool files = locus_texture_file(0x0f) == nullptr && locus_texture_file(0x1d) == nullptr &&
                     std::string(locus_texture_file(0x19)) == "LOCUS_SP0.DDS";
        for (int h = 0x10; h <= 0x1c; ++h) files = files && fs::exists(root / "IMAGE" / locus_texture_file(h));
        check(files, "handles 0x10..0x1C name the 13 shipped IMAGE/LOCUS*.DDS");
    }

    std::printf("%s (%d failed)\n", fails ? "FAIL" : "ALL OK", fails);
    return fails ? 1 : 0;
}
