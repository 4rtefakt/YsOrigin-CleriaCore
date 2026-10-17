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

    {   // FUN_00584550, the dust puffs (GOXX_PARTICLES.md §3.1): 4 draws per puff, the descriptor, the stagger.
        Shared r{{11u, 22u, 33u, 44u}};
        Weather wx;
        wx.rng = [&] { return r.next(); };
        wx.dust(5, 0xc0bda684u, false, {1.0f, 2.0f, 3.0f}, 0.35f, 0.32f, 0.012f);
        const std::vector<Desc> ds = wx.take_emits();
        bool ok = r.calls == 20 && ds.size() == 5;
        for (std::size_t k = 0; ok && k < ds.size(); ++k) {
            const Desc& d = ds[k];
            const float dx = d.pos.x - 1.0f, dz = d.pos.z - 3.0f, rr = std::sqrt(dx * dx + dz * dz);
            const float sp = std::sqrt(d.vel.x * d.vel.x + d.vel.z * d.vel.z);
            ok = d.tex == Tex::EftTex34 && d.rstate == 0x4641u && d.kind == 0xcu && d.argb == 0xc0bda684u &&
                 d.delay == 2 * k && d.pos.y == 2.0f && rr >= 0.08f - 1e-4f && rr <= 0.32f + 1e-4f &&
                 std::fabs(sp - 0.012f) < 1e-4f && d.vel.y == 0.0f && (d.life == 24.0f || d.life == 25.0f) &&
                 d.t1 == 5.0f && d.t2 == 18.0f && d.depth_k == 0.35f * -0.25f && d.size[0] == 0.175f && d.size[1] == 0.35f &&
                 d.size[2] == 0.35f * 0.75f && d.size[3] == 0.175f && d.alpha[0] == 0.5f && d.alpha[1] == 1.0f &&
                 d.alpha[2] == 0.5f && d.alpha[3] == 0.0f && d.angle >= 0.0f && d.angle < 6.2832f &&
                 std::fabs(dx * d.vel.z - dz * d.vel.x) < 1e-4f &&dx * d.vel.x + dz * d.vel.z > 0.0f;      // drifting outward
        }
        check(ok, "dust: 5 puffs = 20 draws; ring 0.08-0.32, speed 0.012 outward, life 24-25, delays 0/2/4/6/8, keys");
        Env e;
        wx.step(e);                                  // puff 0 moves; the others count their delay down
        const auto& ob = wx.objects();
        check(ob.size() == 5 && ob[0]->s.age == 1.0f && ob[1]->s.age == 0.0f && ob[1]->s.d.delay == 1 && ob[4]->s.d.delay == 7,
              "dust: a puff neither ages nor moves while its start delay runs");
        const std::uint64_t c0 = r.calls;
        wx.dust(7, 0xa0606060u, true, {}, 0.6f, 0.32f, 0.012f, 0.0f, true);
        const std::vector<Desc> d7 = wx.take_emits();
        check(r.calls - c0 == 28 && d7.size() == 7 && d7[0].rstate == 0x46c1u && d7[0].kind == 9u,
              "dust: 7 puffs = 28 draws; additive = state 0x46c1, layer 9 in water");
        wx.pool_cap = wx.objects().size() + 2;
        const std::uint64_t c1 = r.calls;
        wx.dust(5, 0xc0bda684u, false, {}, 0.35f, 0.32f, 0.012f);
        check(r.calls - c1 == 8, "dust: a full pool ends the loop before that puff's draws (2 slots = 8 draws)");
    }

    {   // FUN_00589130 / FUN_00588450 / FUN_005886b0 / FUN_00588a50, the spark streaks (GOXX_PARTICLES.md §2.3, §3.7).
        Shared r{{5u, 6u, 7u, 8u}};
        Weather wx;
        wx.rng = [&] { return r.next(); };
        wx.sparks(3, 1, 0x19, 0.11f, {1.0f, 2.0f, 3.0f}, 0.05f, 0.125f, 0.05f, 0.075f, 0.0f, 6.2831855f);   // the guard's
        bool ok = r.calls == 12 && wx.objects().size() == 3 && wx.live_sparks() == 3;
        for (std::size_t k = 0; ok && k < 3; ++k) {
            const Spark& s = wx.objects()[k]->sp;
            const float h = std::sqrt(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
            // ten quarter-steps: vy fell 10 x 0.0075; the tail's far end is still the start
            ok = wx.objects()[k]->kind == Obj::Streak && s.delay == k && s.life >= 35.0f && s.life <= 60.0f &&
                 h >= 0.05f - 1e-5f && h <= 0.125f + 1e-5f && s.vel.y >= 0.05f - 0.075f - 1e-5f && s.vel.y <= 1e-5f &&
                 std::fabs(s.half - 0.044f) < 1e-6f && s.hist[10].x == 1.0f && s.hist[10].y == 2.0f && s.hist[10].z == 3.0f &&
                 (s.hist[1].x != 1.0f || s.hist[1].z != 3.0f) && s.pos.y > 2.0f;
        }
        check(ok, "sparks: the guard's 3 = 12 draws with no terrain; delays 0/1/2, life 35-60, 10 pre-rolled quarter-steps");
        Env e;
        const std::uint64_t c0 = r.calls;
        const float y0 = wx.objects()[0]->sp.pos.y, vy0 = wx.objects()[0]->sp.vel.y;
        wx.step(e);
        const Spark& s0 = wx.objects()[0]->sp;
        check(r.calls == c0 && s0.age == 1.0f && s0.pos.y == y0 + vy0 && s0.vel.y == vy0 - 0.0075f &&
              wx.objects()[1]->sp.age == 0.0f && wx.objects()[1]->sp.delay == 0 && wx.objects()[2]->sp.delay == 1,
              "sparks: a step moves by the velocity, then the full gravity; a delayed spark waits; no draw in the air");
        SparkQuad q[4];
        spark_ribbon(s0, {0.0f, 0.0f, 1.0f}, q);
        const float wdt = std::sqrt((q[1].p[0].x - q[1].p[1].x) * (q[1].p[0].x - q[1].p[1].x) +
                                    (q[1].p[0].y - q[1].p[1].y) * (q[1].p[0].y - q[1].p[1].y) +
                                    (q[1].p[0].z - q[1].p[1].z) * (q[1].p[0].z - q[1].p[1].z));
        check(std::fabs(wdt - 0.088f) < 1e-4f && q[0].v[0] == 0.1f && q[0].v[1] == 0.4f && q[3].v[1] == 0.9f &&
              std::fabs((q[0].p[0].x + q[0].p[1].x) * 0.5f - s0.hist[1].x) < 1e-5f &&
              std::fabs((q[3].p[2].y + q[3].p[3].y) * 0.5f - s0.hist[9].y) < 1e-5f && q[1].p[0].z == q[1].p[1].z,
              "sparks: the ribbon = 4 quads through hist[1,3,5,7,9], width 2 x 0.4 x the width, across the view, v 0.1-0.9");
        Spark f; f.life = 40.0f; f.age = 30.0f;
        check(spark_alpha(f) == 0.5f && (f.age = 10.0f, spark_alpha(f) == 1.0f), "sparks: alpha 1, fading over the last 20 ticks");
        // One bounce = one draw: a spark dropped straight down from just above a floor bounces once in its pre-roll.
        Weather wf;
        Shared r2{{9u, 8u, 7u, 6u}};
        wf.rng = [&] { return r2.next(); };
        int rays = 0;
        wf.hit = [&](const Vec3& a, const Vec3& b, Vec3* p, Vec3* n) {
            ++rays;
            if (!(a.y > 0.0f && b.y <= 0.0f)) return false;
            const float t = a.y / (a.y - b.y);
            *p = {a.x + (b.x - a.x) * t, 0.0f, a.z + (b.z - a.z) * t};
            *n = {0.0f, 1.0f, 0.0f};
            return true;
        };
        wf.sparks(1, 0, 0, 0.15f, {0.0f, 0.001f, 0.0f}, 0.0f, 0.0f, -0.05f, -0.05f, 0.0f, 0.0f);
        const Spark& sb = wf.objects()[0]->sp;
        check(r2.calls == 5 && rays == 9 && sb.pos.y > 0.0f && sb.life == 35.0f,
              "sparks: 4 draws + 1 for the one bounce of the pre-roll (a zero-length first ray is not cast)");
        // The crowd throttle: more than 12 live sparks skip the next collision call, more than 20 the next two.
        Weather wt;
        Shared r3{{1u, 1u, 2u, 3u}};
        wt.rng = [&] { return r3.next(); };
        int rays2 = 0;
        wt.hit = [&](const Vec3&, const Vec3&, Vec3*, Vec3*) { ++rays2; return false; };
        wt.sparks(13, 0, 0, 0.1f, {}, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f);
        // sparks 1..12: 9 rays each (calls 2..10); the 13th (13 live): calls 1 (zero ray, sets skip), 3, 5, 7, 9 cast -> 4 rays
        check(rays2 == 12 * 9 + 4 && wt.live_sparks() == 13, "sparks: the 13th live spark casts its ray every other call");
        for (auto& o : wt.objects()) o->sp.life = 1.0f;
        wt.step(e); wt.sweep();
        check(wt.live_sparks() == 0 && wt.objects().empty(), "sparks: the sweep returns the live count");
    }

    {   // FUN_0058a5f0 / FUN_0058a210 / FUN_0058a900 / FUN_00589f60, the blood splats (GOXX_PARTICLES.md §2.4, §3.3-§3.5).
        Shared r{{3u, 1u, 4u, 1u}};
        Weather wx;
        wx.rng = [&] { return r.next(); };
        auto floor0 = [](const Vec3& a, const Vec3& b, Vec3* p, Vec3* n) {
            if (!(a.y > 0.0f && b.y <= 0.0f)) return false;
            const float t = a.y / (a.y - b.y);
            *p = {a.x + (b.x - a.x) * t, 0.0f, a.z + (b.z - a.z) * t};
            *n = {0.0f, 1.0f, 0.0f};
            return true;
        };
        wx.hit = floor0;
        wx.blood(3, 0xff7c0000u, 2, 0x18, 1.3f, 1.8f, {10.0f, 1.0f, 20.0f}, 0.65f, false, false);   // a monster's
        bool ok = r.calls == 18 && wx.live_decals() == 3;
        for (std::size_t k = 0; ok && k < 3; ++k) {
            const Decal& d = wx.objects()[k]->dc;
            const float dx = d.pos.x - 10.0f, dz = d.pos.z - 20.0f;
            ok = d.variant < 3 && d.life >= 370.0f && d.life <= 394.0f && d.delay == 2 * k && d.pos.y == 0.0f &&
                 dx * dx + dz * dz <= 0.65f * 0.65f + 1e-4f && d.size >= 1.3f && d.size < 1.8f &&
                 d.angle >= -3.1416f && d.angle < 3.1416f && d.foot && !d.slime && !d.keep && d.normal.y == 1.0f;
        }
        check(ok, "blood: 3 splats = 18 draws; on the floor within 0.65, size 1.3-1.8, life 370-394 at level 2, delays 0/2/4");
        {   // the decal's mesh (FUN_00589690): on a level floor, a 2-unit decal turned by 0 at (10, 0, 20)
            weather::Decal dm;
            dm.pos = {10.0f, 0.0f, 20.0f}; dm.normal = {0.0f, 1.0f, 0.0f}; dm.angle = 0.0f; dm.size = 2.0f;
            dm.argb = 0xff7c0000u; dm.life = 100.0f; dm.age = 3.0f;
            const std::vector<Vec3> tris = {
                {9, 0, 19}, {11, 0, 19}, {11, 0, 21},                 // under the decal
                {40, 0, 40}, {41, 0, 40}, {41, 0, 41},                // far away: every u > 1
                {9, 5, 19}, {11, 5, 19}, {11, 5, 21},                 // 5 above: outside the centroid window
                {10, 0, 20}, {10, 0.25f, 21}, {11, 0.6f, 20}};        // a slope: the weight falls with the height
            weather::decal_mesh(dm, tris);
            bool mok = dm.verts.size() == 6;
            if (mok) {
                mok = std::fabs(dm.verts[0].u - 0.0f) < 0.03f && std::fabs(dm.verts[0].v - 0.0f) < 0.03f &&
                      std::fabs(dm.verts[2].u - 1.0f) < 0.03f && std::fabs(dm.verts[2].v - 1.0f) < 0.03f &&
                      std::fabs(dm.verts[3].u - 0.5f) < 0.03f && std::fabs(dm.verts[3].v - 0.5f) < 0.03f &&
                      std::fabs(dm.verts[0].w - 223.0f / 255.0f) < 1e-6f &&
                      std::fabs(dm.verts[4].w - 0.5f * 223.0f / 255.0f) < 1e-5f && dm.verts[5].w == 0.0f;
            }
            check(mok, "decal mesh: whole triangles under the square, UV 0..1 centred on the point, weight 223/255 "
                       "falling to 0 half a unit above");
            check(std::fabs(weather::decal_alpha(dm) - 0.5f) < 1e-6f && weather::decal_alpha8(223.0f / 255.0f, 1.0f) == 223 &&
                  weather::decal_alpha8(1.0f, 1.0f) == 255 && weather::decal_alpha8(0.5f, 0.5f) == 64,
                  "decal alpha: the 6-tick fade in; the byte = floor(256 x weight x alpha), at most 255");
            dm.age = 75.0f;
            check(std::fabs(weather::decal_alpha(dm) - 0.5f) < 1e-6f, "decal alpha: the 50-tick fade out");
            std::vector<Vec3> many;
            for (int k = 0; k < 60; ++k) { many.push_back({9, 0, 19}); many.push_back({11, 0, 19}); many.push_back({11, 0, 21}); }
            weather::decal_mesh(dm, many);
            check(dm.verts.size() == 135, "decal mesh: at most 45 triangles");
        }
        // No floor within 5 below: the draws are taken, no decal is left.
        const std::uint64_t c0 = r.calls;
        wx.blood(3, 0xff7c0000u, 2, 0x18, 1.3f, 1.8f, {0.0f, 9.0f, 0.0f}, 0.65f, false, false);
        wx.sweep();
        check(r.calls - c0 == 18 && wx.live_decals() == 3, "blood: without a floor within 5 the 18 draws are taken and no decal stays");
        // Level 1: no +120 and no footprints; flag B (slime): variant 0, the trigger armed.
        wx.bloody = 1;
        wx.blood(1, 0xff7c0000u, 2, 0x18, 1.3f, 1.8f, {0.0f, 1.0f, 0.0f}, 0.65f, false, false);
        const Decal l1 = wx.objects().back()->dc;
        wx.bloody = 2;
        wx.blood(1, 0xff22cc44u, 2, 10, 0.4f, 0.8f, {0.0f, 1.0f, 0.0f}, 0.2f, false, true);
        const Decal sl = wx.objects().back()->dc;
        check(l1.life >= 250.0f && l1.life <= 274.0f && !l1.foot && sl.variant == 0 && sl.foot && sl.slime &&
              sl.life >= 250.0f && sl.life <= 260.0f,
              "blood: level 1 = life 250-274, no footprints; a slime puddle = variant 0, life 250-260, the trigger armed");
        // The drop of a debris bounce: 4 draws, variants 1 / 2, life x 0.75, no offset.
        const std::uint64_t c1 = r.calls;
        wx.blood_drop(0xff7c0000u, {5.0f, 0.5f, 5.0f}, 0.75f, 0.9f);
        const Decal dr = wx.objects().back()->dc;
        check(r.calls - c1 == 4 && (dr.variant == 1 || dr.variant == 2) && dr.pos.x == 5.0f && dr.pos.z == 5.0f && dr.pos.y == 0.0f &&
              dr.life >= 370.0f * 0.75f && dr.life <= 406.0f * 0.75f && dr.size >= 0.75f && dr.size < 0.9f && !dr.foot,
              "blood: a debris drop = 4 draws, variant 1-2, under the piece, life (370-406) x 0.75, size 0.75-0.9");
        // The footprint trigger: the grounded player inside size x 0.275 of a splat on his floor.
        Env e;
        e.player = true; e.player_grounded = true; e.player_ground = 0.0f;
        const Decal& d0 = wx.objects()[0]->dc;
        e.player_pos = {d0.pos.x + d0.size * 0.27f, 1.075f, d0.pos.z};
        wx.step(e);
        const bool on = wx.foot_ticks == 0x2d && wx.foot_rgb == 0x7c0000u && wx.slime_ticks == 0;
        wx.foot_ticks = 0;
        e.player_pos = {d0.pos.x + 30.0f, 1.075f, d0.pos.z};
        wx.step(e);
        check(on && wx.foot_ticks == 0 && d0.age == 2.0f, "blood: walking through a splat arms 45 ticks of footprints in its colour");
        // A water room: the clouds instead (type-0 sprites), 5 draws each, none of the decal's 6.
        Weather ww;
        Shared r2{{2u, 7u, 1u, 8u}};
        ww.rng = [&] { return r2.next(); };
        ww.hit = floor0;
        ww.water_mode = 2;
        ww.blood(3, 0xff7c0000u, 2, 0x18, 1.3f, 1.8f, {0.0f, 1.0f, 0.0f}, 0.65f, false, false);
        const std::vector<Desc> cl = ww.take_emits();
        check(r2.calls == 15 && cl.size() == 3 && ww.live_decals() == 0 && cl[0].kind == 9u && cl[0].rstate == 0x4641u &&
              cl[1].delay == 8 && cl[0].t1 == 35.0f && cl[0].t2 == 158.0f && cl[0].life >= 254.0f && cl[0].life <= 350.0f &&
              cl[0].size[3] == cl[0].size[2] * 1.25f / 1.0f && cl[0].vel.x == 0.0f && cl[0].pos.y == 1.0f,
              "blood: in a water room 3 clouds = 15 draws (no decal): layer 9, delays 8 apart, life 254-350, growing");
        const std::uint64_t c2 = r2.calls;
        ww.blood_drop(0xff7c0000u, {0.0f, 1.0f, 0.0f}, 0.75f, 0.9f);
        check(r2.calls - c2 == 5, "blood: a debris drop in a water room = one cloud, 5 draws");
    }

    std::printf("[weather] %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
