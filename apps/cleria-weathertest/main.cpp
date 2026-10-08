// cleria-weathertest: the floor-3 script weather (build/effect_re/WEATHER.md, src/cleria/weather).
//  1. The port vs the retail update bodies: FUN_0058e7c0 (0x125's ember emitter) and FUN_0058daa0
//     (0x122's smoke emitter) were executed on the exe's own bytes by an x86 interpreter
//     (tools/weather_oracle/), tick by tick; oracle.inl holds the shared xorshift128 state after
//     every tick it moved and each FUN_00583d90 descriptor. The port must match bit for bit.
//  2. The op decode: 0x125 reads int operand 0 only, 0x122 its int, 0x123 three floats.
//  3. RNG alignment on the shipped S_3009 scripts: FIRE_DROP.XSO (FireDrop(180), Wait_Time(10))
//     and FRAME_SMOKE.XSO (FrameSmoke(120), Wait_Time(20)) run through the ScriptManager into the
//     weather pass for 600 ticks; the shared state must equal the seed advanced by exactly the
//     draw schedule read off the export (ember: 10 draws on each 16th tick of its life; smoke: 13
//     on its first tick, then 3 per tick; flag 290 adds 1 / 2).
//  4. FireBlur's timer (FUN_00427c90) and a sprite's key-frame fade (FUN_00584270); FireBlur's wave
//     grid vs the retail FUN_005e44c0 / FUN_00427c90 bytes (blur_oracle.inl, gen_blur.py).
// Usage: cleria-weathertest <assets>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include "cleria/weather.hpp"
#include "cleria/xso.hpp"

using namespace cleria;
using namespace cleria::weather;

#include "oracle.inl"
#include "blur_oracle.inl"

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[weather] %-86s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static std::vector<std::uint8_t> rd(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

struct Shared {                       // the game's one xorshift128 (0x74622c..38), counted
    std::uint32_t s[4];
    std::uint64_t calls = 0;
    std::uint32_t next() {
        ++calls;
        const std::uint32_t t = s[0] ^ (s[0] << 11);
        s[0] = s[1]; s[1] = s[2]; s[2] = s[3];
        s[3] = s[3] ^ (s[3] >> 19) ^ t ^ (t >> 8);
        return s[3];
    }
};

static std::uint32_t bits(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return b; }
static std::uint32_t handle(Tex t) {   // the oracle's stand-in handle values (oracle.py)
    switch (t) {
        case Tex::Sparkle: return 0xAAAA0030u;
        case Tex::Sparkle2: return 0xAAAA0031u;
        case Tex::EftTex34: return 0xBBBB0022u;
        case Tex::Kao00: return 0xBBBB003Cu;
    }
    return 0;
}
static void words(const Desc& d, std::uint32_t w[27]) {
    w[0] = handle(d.tex); w[1] = d.rstate; w[2] = d.kind; w[3] = d.argb; w[4] = d.delay; w[5] = d.pad;
    w[6] = bits(d.pos.x); w[7] = bits(d.pos.y); w[8] = bits(d.pos.z);
    w[9] = bits(d.vel.x); w[10] = bits(d.vel.y); w[11] = bits(d.vel.z);
    w[12] = d.uv; w[13] = bits(d.angle); w[14] = bits(d.drag); w[15] = bits(d.depth_k);
    w[16] = bits(d.life); w[17] = bits(d.t1); w[18] = bits(d.t2);
    for (int k = 0; k < 4; ++k) { w[19 + k] = bits(d.alpha[k]); w[23 + k] = bits(d.size[k]); }
}

// 1) Replay one oracle scenario.
static bool replay(const OScen& sc, std::string* why) {
    Shared r;
    std::memcpy(r.s, sc.seed, sizeof r.s);
    Weather wx;
    wx.rng = [&] { return r.next(); };
    if (sc.smoke) wx.frame_smoke(sc.cap); else wx.fire_drop(sc.cap);
    Env e;
    e.eye = {sc.eye[0], sc.eye[1], sc.eye[2]};
    e.at = {sc.at[0], sc.at[1], sc.at[2]};
    e.noise = sc.noise != 0;
    e.map268 = 0x1234;
    const OTick* exp = kTicks + sc.first;
    int k = 0;
    std::uint32_t prev[4];
    std::memcpy(prev, sc.seed, sizeof prev);
    bool prev_dead = false;
    char buf[200];
    for (int t = 1; t <= sc.ticks; ++t) {
        // fail_at 1: the first child alloc (tick 1 for a puff, 16 for an ember) finds the pool
        // full — the emitter holds its only slot — and the later ones find a slot
        wx.pool_cap = sc.fail_at && t <= (sc.smoke ? 1 : 16) ? 1 : 2048;
        wx.step(e);
        const Obj& em = *wx.objects().front();
        const std::vector<Desc> got = wx.take_emits();
        const bool moved = std::memcmp(prev, r.s, sizeof prev) != 0 || !got.empty() || em.dead != prev_dead;
        std::memcpy(prev, r.s, sizeof prev);
        prev_dead = em.dead;
        if (!moved) continue;
        if (k >= sc.count || exp[k].t != t) {
            std::snprintf(buf, sizeof buf, "tick %d: the port moved, the exe did not", t); *why = buf; return false;
        }
        const OTick& o = exp[k++];
        if (std::memcmp(o.rng, r.s, sizeof r.s) != 0) {
            std::snprintf(buf, sizeof buf, "tick %d: rng %08x.. vs exe %08x..", t, r.s[3], o.rng[3]); *why = buf; return false;
        }
        if (int(em.dead) != o.dead) { std::snprintf(buf, sizeof buf, "tick %d: dead %d vs %d", t, int(em.dead), o.dead); *why = buf; return false; }
        if ((o.emit >= 0) != !got.empty()) { std::snprintf(buf, sizeof buf, "tick %d: emit count", t); *why = buf; return false; }
        if (o.emit >= 0) {
            std::uint32_t w[27];
            words(got[0], w);
            for (int j = 0; j < 27; ++j)
                if (w[j] != kEmits[o.emit].d[j]) {
                    std::snprintf(buf, sizeof buf, "tick %d: descriptor word %d %08x vs exe %08x", t, j, w[j], kEmits[o.emit].d[j]);
                    *why = buf; return false;
                }
        }
        if (sc.smoke && !em.dead) {
            const std::uint32_t off[3] = {bits(em.off.x), bits(em.off.y), bits(em.off.z)};
            if (std::memcmp(off, o.off, sizeof off) != 0) {
                std::snprintf(buf, sizeof buf, "tick %d: smoke offset", t); *why = buf; return false;
            }
            if (em.child) {
                // the child's position as the emitter wrote it (at + offset): the oracle has no
                // child step; here the child steps after its emitter from the next tick on, so
                // the written value is its `prev` then (FUN_00584030: +0x1f8 = +0x50 first)
                const Vec3& w = t == 1 ? em.child->s.pos : em.child->s.prev;
                const std::uint32_t cp[3] = {bits(w.x), bits(w.y), bits(w.z)};
                if (std::memcmp(cp, o.cpos, sizeof cp) != 0) {
                    std::snprintf(buf, sizeof buf, "tick %d: smoke child position", t); *why = buf; return false;
                }
            }
        }
    }                                           // (no sweep: the emitter stays objects().front())
    if (k != sc.count) { std::snprintf(buf, sizeof buf, "the exe moved %d more times", sc.count - k); *why = buf; return false; }
    return true;
}

struct H : XsoHost {
    std::vector<std::string> log;
    Weather* wx = nullptr;
    int tick = 0;
    std::vector<int> drops, smokes;
    void spawn(int, int, float, float, float, float, int) override {}
    bool is_alive(int) const override { return false; }
    void fire_drop(int life) override {
        log.push_back("drop " + std::to_string(life)); drops.push_back(tick);
        if (wx) wx->fire_drop(std::uint32_t(life));
    }
    void frame_smoke(int life) override {
        log.push_back("smoke " + std::to_string(life)); smokes.push_back(tick);
        if (wx) wx->frame_smoke(std::uint32_t(life));
    }
    void fire_blur(float a, float b, float c) override {
        char t[96]; std::snprintf(t, sizeof t, "blur %.4f %.4f %.4f", a, b, c); log.push_back(t);
    }
};

struct DirLoader : ScriptLoader {
    std::string dir;
    std::shared_ptr<const Xso> load_script(const std::string& name) override {
        auto b = rd(dir + name + ".XSO");
        if (b.size() < 4) return nullptr;
        return std::make_shared<Xso>(Xso::parse(b));
    }
};

// A synthetic XSR builder with typed operand descriptors (the cleria-xso13test one).
struct Asm {
    std::vector<std::int32_t> code;
    struct A { int t; std::int32_t v; };
    static A I(std::int32_t v) { return {0, v}; }
    static A F(float f) { std::int32_t w; std::memcpy(&w, &f, 4); return {1, w}; }
    void op(int sub, std::initializer_list<A> args) {
        const std::uint32_t n = std::uint32_t(args.size()), nd = (n + 7) / 8;
        code.push_back(std::int32_t((2u << 24) | (std::uint32_t(sub) << 12) | (nd << 8) | n));
        std::vector<std::uint32_t> d(nd, 0u);
        std::uint32_t i = 0;
        for (const A& a : args) { d[i >> 3] |= std::uint32_t(a.t) << ((i & 7) * 4); ++i; }
        for (std::uint32_t w : d) code.push_back(std::int32_t(w));
        for (const A& a : args) code.push_back(a.v);
    }
    void raw(std::int32_t w) { code.push_back(w); }
    std::vector<std::uint8_t> bytes() const {
        std::vector<std::uint8_t> b(0x24, 0);
        std::memcpy(b.data(), "XSR\0", 4);
        const std::uint32_t words = std::uint32_t(code.size()), cnt = 0;
        std::memcpy(b.data() + 0x1C, &words, 4);
        std::memcpy(b.data() + 0x20, &cnt, 4);
        const auto* cp = reinterpret_cast<const std::uint8_t*>(code.data());
        b.insert(b.end(), cp, cp + code.size() * 4);
        return b;
    }
};

// The export's draw schedule for the S_3009 pair, independent of weather.cpp: an ember born on
// tick b draws on tick T when (T - b + 1) is 16, 32, .. 176 (its counter after the increment);
// a puff born on b draws 3 + 7 + 3 on b (5 + 7 + 3 under the noise), then 3 on b+1 .. b+cap-1.
static std::uint64_t schedule(const std::vector<int>& drops, const std::vector<int>& smokes, int T, bool noise) {
    std::uint64_t n = 0;
    for (int b : drops) {
        const int c = T - b + 1;
        if (c >= 16 && c <= 176 && c % 16 == 0) n += noise ? 11 : 10;
    }
    for (int b : smokes) {
        if (T == b) n += noise ? 15 : 13;
        else if (T > b && T <= b + 119) n += 3;
    }
    return n;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-weathertest <assets>\n"); return 2; }
    const std::string root = argv[1];

    // 1) the retail bodies, bit for bit
    for (const OScen& sc : kScen) {
        std::string why;
        const bool ok = replay(sc, &why);
        char t[160];
        std::snprintf(t, sizeof t, "exe replay %-14s (%d ticks)%s%s", sc.name, sc.ticks, ok ? "" : ": ", why.c_str());
        check(ok, t);
    }

    // 2) the op decode
    {
        Asm a;
        a.op(0x125, {Asm::I(180), Asm::I(7)});
        a.op(0x122, {Asm::I(120)});
        a.op(0x123, {Asm::F(1.0f), Asm::F(-0.0025f), Asm::F(0.3f)});
        a.raw(0x01000000);
        H h; XsoVm vm; vm.host = &h;
        bool un = false;
        vm.on_unhandled = [&](std::uint32_t) { un = true; };
        vm.run(Xso::parse(a.bytes()));
        check(!un && h.log.size() == 3 && h.log[0] == "drop 180" && h.log[1] == "smoke 120" &&
                  h.log[2] == "blur 1.0000 -0.0025 0.3000",
              "0x125 FireDrop(180, 7) / 0x122 FrameSmoke(120) / 0x123 FireBlur(1, -0.0025, 0.3) decode");
        // the shipped FIRE_BLUR.XSO: a 0x81 roll picks one of three strengths, each a 400-tick timer
        const auto fb = rd(root + "/MAP/S_30/S_3009/FIRE_BLUR.XSO");
        bool seen = fb.size() > 4;
        for (std::uint32_t seed = 1; seen && seed <= 3; ++seed) {
            H hb; XsoVm vb; vb.host = &hb;
            vb.rs[3] ^= seed * 0x9e3779b9u;
            vb.g_flags[165] = 1;
            vb.run(Xso::parse(fb), 0, 64);
            seen = !hb.log.empty() && (hb.log[0] == "blur 1.0000 -0.0025 0.3000" || hb.log[0] == "blur 1.0000 -0.0025 0.2500" ||
                                       hb.log[0] == "blur 1.0000 -0.0025 0.2000");
        }
        check(seen, "S_3009 FIRE_BLUR.XSO: FireBlur(1.0, -0.0025, 0.3 / 0.25 / 0.2) (a 400-tick haze)");
    }

    // 3) RNG alignment on the shipped S_3009 FIRE_DROP + FRAME_SMOKE
    for (int noise = 0; noise < 2; ++noise) {
        Shared r{{123456789u, 362436069u, 521288629u, 916191069u}};
        Weather wx;
        wx.rng = [&] { return r.next(); };
        H h; h.wx = &wx;
        DirLoader ld; ld.dir = root + "/MAP/S_30/S_3009/";
        ScriptManager mgr; mgr.loader = &ld; mgr.vm.host = &h;
        mgr.vm.g_flags[165] = 1;            // both scripts loop only while flag 165 != 0 (Flag_IsEqualInt(165, 0) + jz)
        Env e;
        e.eye = {-35.0f, 12.0f, 20.0f};
        e.at = {-35.1f, 1.0f, 34.9f};
        e.noise = noise != 0;
        const int N = 600;
        bool aligned = true;
        std::size_t peak = 0;
        std::uint64_t expect = 0;
        for (int t = 1; t <= N && aligned; ++t) {
            h.tick = t;
            if (t == 1) {
                const auto fd = ld.load_script("FIRE_DROP"), fs = ld.load_script("FRAME_SMOKE");
                if (!fd || !fs) { std::fprintf(stderr, "missing S_3009 scripts under %s\n", ld.dir.c_str()); return 2; }
                mgr.start_root(fd, "FIRE_DROP");
                mgr.start_root(fs, "FRAME_SMOKE");
            } else {
                mgr.tick_all();
            }
            wx.step(e);
            wx.sweep();
            peak = std::max(peak, wx.objects().size());
            expect += schedule(h.drops, h.smokes, t, e.noise);
            aligned = r.calls == expect;
        }
        Shared ref{{123456789u, 362436069u, 521288629u, 916191069u}};
        for (std::uint64_t k = 0; k < expect; ++k) ref.next();
        // Wait_Time(N) parks max(1, N - 1) frames (0x56de74, audit event_vm #14): a FireDrop every
        // 9 ticks, a FrameSmoke every 19
        const bool births = h.drops.size() == 67 && h.drops[1] == 10 && h.drops.back() == 595 &&
                            h.smokes.size() == 32 && h.smokes[1] == 20 && h.smokes.back() == 590;
        char t[200];
        std::snprintf(t, sizeof t, "S_3009 FIRE_DROP + FRAME_SMOKE x %d ticks%s: %llu draws = schedule %llu, state %08x",
                      N, noise ? " (flag 290)" : "", (unsigned long long)r.calls, (unsigned long long)expect, r.s[3]);
        check(aligned && births && std::memcmp(r.s, ref.s, sizeof r.s) == 0, t);
        std::snprintf(t, sizeof t, "  births every 9 / 19 ticks (drops %zu, puffs %zu), %zu emitters live, pool peak %zu",
                      h.drops.size(), h.smokes.size(), wx.live_emitters(), peak);
        check(births && wx.live_emitters() == 20 + 6 && peak < wx.pool_cap, t);
    }

    // 4a) FireBlur's timer: level 1 - 0.0025 / tick -> the post-fx strength for 400 ticks
    {
        Weather wx;
        wx.fire_blur(1.0f, -0.0025f, 0.3f);
        int on = 0;
        for (int t = 0; t < 1000; ++t) { wx.step_blur(); on += wx.blur.out != 0.0f; }
        check(on >= 399 && on <= 400 && wx.blur.level == 0.0f && wx.blur.out == 0.0f,
              "FireBlur(1, -0.0025, 0.3): 0x7b249c = 0.3 while the level runs down (~400 ticks), then 0");
        wx.fire_blur(1.0f, 0.5f, 0.2f);
        wx.step_blur();
        check(wx.blur.level == 1.0f && wx.blur.out == 0.2f, "FireBlur level clamps at 1");
    }
    // 4c) FireBlur's wave grid vs the retail bytes (blur_oracle.inl, tools/weather_oracle/gen_blur.py):
    //     FUN_005e44c0's 36 vertices for three back-buffer / copy sizes, then FUN_00427c90 ticked 420
    //     times with the draw flag 0x7b249c raised before most ticks: level, 0x7b249c, 0x7b2e1c and
    //     every vertex x / y after each tick. cos is the CRT's in the exe and std::cos here: 1 ulp.
    {
        bool ok = true;
        for (const BGrid& G : kBGrids) {
            const BlurGrid grid;
            for (int i = 0; i < BlurGrid::kN; ++i) {
                const BlurGrid::V v = BlurGrid::base(i, G.w, G.h, G.tw, G.th);
                const bool o = bits(v.x) == G.v[i][0] && bits(v.y) == G.v[i][1] && bits(v.u) == G.v[i][5] &&
                               bits(v.v) == G.v[i][6] && grid.a[i] == G.a[i] && grid.b[i] == G.b[i];
                if (!o && ok)
                    std::printf("  %dx%d vertex %d: %08x %08x %08x %08x %d %d vs %08x %08x %08x %08x %d %d\n", G.w, G.h, i,
                                bits(v.x), bits(v.y), bits(v.u), bits(v.v), grid.a[i], grid.b[i], G.v[i][0], G.v[i][1],
                                G.v[i][5], G.v[i][6], G.a[i], G.b[i]);
                ok = ok && o;
            }
        }
        check(ok, "FireBlur grid: FUN_005e44c0's 36 strip vertices (x, y, u, v) and phases, bit for bit");
        Blur bl;
        bl.level = 1.0f; bl.delta = -0.0025f; bl.strength = 0.3f;
        const BGrid& G = kBGrids[0];
        auto ulp = [](float f, std::uint32_t b) { const std::int64_t d = std::int64_t(bits(f)) - std::int64_t(b); return d <= 1 && d >= -1; };
        bool tk = true;
        int bad = -1, drawn_on = 0;
        for (int t = 0; t < int(std::size(kBTicks)); ++t) {
            const BTick& T = kBTicks[t];
            bl.grid.drawn = T.drawn_before != 0;
            bl.step();
            bool o = bits(bl.level) == T.level && bits(bl.out) == T.out && int(bl.grid.drawn) == T.drawn_after;
            for (int i = 0; i < BlurGrid::kN; ++i) {
                const BlurGrid::V v = bl.grid.vertex(i, G.w, G.h, G.tw, G.th);
                o = o && ulp(v.x, T.xy[i][0]) && ulp(v.y, T.xy[i][1]);
            }
            drawn_on += bl.active();
            if (!o && bad < 0) bad = t + 1;
            tk = tk && o;
        }
        if (bad >= 0) std::printf("  first mismatch at tick %d\n", bad);
        check(tk && drawn_on == 400 && !bl.active(),
              "FireBlur step: FUN_00427c90 x420 (level, 0x7b249c, 0x7b2e1c, the 36 vertices; 400 ticks drawn)");
    }
    // 4b) an ember's fade: in over 0.1 life, held to 0.4 life, out to its end
    {
        Shared r{{1u, 2u, 3u, 4u}};
        Weather wx;
        wx.rng = [&] { return r.next(); };
        wx.fire_drop(180);
        Env e;
        for (int t = 0; t < 16; ++t) wx.step(e);
        const Obj* c = wx.objects().back().get();
        bool ok = c->kind == Obj::Child;
        if (ok) {
            Sprite s = c->s;
            s.age = 0; const Look a0 = look(s);
            s.age = 9; const Look a9 = look(s);
            s.age = 18; const Look a18 = look(s);
            s.age = 72; const Look a72 = look(s);
            s.age = s.d.life; const Look aend = look(s);
            ok = a0.alpha == 0.0f && std::fabs(a9.alpha - 127.5f) < 1e-3f && a18.alpha == 255.0f && a72.alpha == 255.0f &&
                 std::fabs(aend.alpha) < 1e-3f && a0.half == 0.0225f;
        }
        check(ok, "ember sprite: alpha 0 -> 255 by 18, 255 to 72, -> 0 at its life; half-size 0.0225");
    }

    std::printf("[weather] %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
