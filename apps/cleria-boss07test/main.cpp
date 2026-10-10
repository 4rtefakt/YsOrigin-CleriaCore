// Headless tests for src/cleria/boss07 (Dalles) against build/boss_re/DALLES.md (P1), DALLES_AB.md (AB),
// DALLES_CD1.md (CD1) and DALLES_D23.md (D23). Every expectation is a number of those specs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "cleria/boss07.hpp"
#include "cleria/hit.hpp"

using namespace cleria;
using namespace cleria::boss07;

static int fails = 0, total = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss07] %-110s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok; ++total;
}

struct XorShift {
    std::uint32_t x = 123456789, y = 362436069, z = 521288629, w = 88675123;
    std::uint32_t operator()() {
        const std::uint32_t t = x ^ (x << 11);
        x = y; y = z; z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        return w;
    }
};

static const float kDeg = 0.017453292f, kHalfPi = 1.5707964f;
static bool nearf(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
static bool vnear(const Vec3& a, const Vec3& b, float tol = 1e-4f) {
    return nearf(a.x, b.x, tol) && nearf(a.y, b.y, tol) && nearf(a.z, b.z, tol);
}
static bool veq(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static std::uint32_t bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
static std::uint32_t r15(std::uint32_t v) { return v << 17; }            // a word whose (w >> 17) is v
static const std::uint32_t kU25 = 0x200000, kU50 = 0x400000, kU75 = 0x600000;   // words whose U() is .25 / .5 / .75

static int n_fx(const Boss07Output& o, int id) { int n = 0; for (const auto& e : o.effects) n += e.id == id; return n; }
static const Boss07Effect* fx(const Boss07Output& o, int id, int nth = 0) {
    for (const auto& e : o.effects) if (e.id == id && nth-- == 0) return &e;
    return nullptr;
}
static int n_se(const Boss07Output& o, int id, bool se2 = false) {
    int n = 0; for (const auto& s : o.sounds) n += s.id == id && s.se2 == se2; return n;
}
static int n_clip(const Boss07Output& o, int slot) { int n = 0; for (const auto& c : o.clips) n += c.slot == slot; return n; }
static bool flag(const Boss07Output& o, int f, int v) {
    for (const auto& p : o.flags) if (p.first == f && p.second == v) return true;
    return false;
}
static bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
static bool motion(const Boss07Output& o, int mesh, const char* name) {
    for (const auto& m : o.motions) if (m.mesh == mesh && std::strcmp(m.name, name) == 0) return true;
    return false;
}
static int n_bul(const Boss07Output& o, int kind, int effect) {
    int n = 0; for (const auto& b : o.bullets) n += b.kind == kind && b.effect == effect; return n;
}
static const Boss07Bullet* bul(const Boss07Output& o, int kind, int effect, int nth = 0) {
    for (const auto& b : o.bullets) if (b.kind == kind && b.effect == effect && nth-- == 0) return &b;
    return nullptr;
}

// The host stand-in: the clip model of AB §0 (advance_clip, one 0.25 advance a tick; a SetClip restarts at the
// first frame), fixed node offsets around the body (forward = core - hontai = -z), the RNG (forced raw words,
// else a fixed word, else xorshift).
struct Rig {
    Boss07 b;
    Boss07Input in;
    XorShift rng;
    std::deque<std::uint32_t> forced;
    std::optional<std::uint32_t> fixed;
    int draws = 0;
    int sec = 0; float frame = 310.0f; std::uint32_t cnt = 0;
    explicit Rig(const Boss07Setup& s = Boss07Setup{}) : b(s) {
        in.rng = [this]() -> std::uint32_t {
            ++draws;
            if (!forced.empty()) { const std::uint32_t v = forced.front(); forced.pop_front(); return v; }
            if (fixed) return *fixed;
            return rng();
        };
        in.player_pos = {0.0f, 0.0f, -14.5f};                      // the room's start [P1 §1.1]
    }
    Rig(const Rig&) = delete;
    void clip(int k) { sec = k; frame = float(kSectionTable[k].first); cnt = 0; }
    void feed() {
        in.section = sec; in.count = cnt; in.frame = frame;
        auto T = [](const Vec3& v) { return ef2sim::translation(v.x, v.y, v.z); };
        const Vec3 p = b.body().pos;
        in.nodes[kCore] = T(p); in.nodes[kHontai] = T({p.x, p.y, p.z + 1});
        in.nodes[kHandR] = T({p.x + 1, p.y, p.z}); in.nodes[kHandL] = T({p.x - 1, p.y, p.z});
        in.nodes[kNailL1] = T({p.x - 1, p.y, p.z - 1});
        for (int i = 0; i < 2; ++i) {
            const Vec3 c = b.child(i).pos;
            in.opt_core[std::size_t(i)] = c; in.opt_hontai[std::size_t(i)] = {c.x, c.y, c.z + 1};
        }
    }
    void apply(const Boss07Output& o) { for (const auto& c : o.clips) clip(c.section); }
    Boss07Output step() {
        feed();
        Boss07Output o = b.tick(in);
        apply(o);
        advance_clip(sec, frame, cnt);
        if (o.player.pos) in.player_pos = *o.player.pos;
        for (int s : o.player.sections) in.player_section = s;
        return o;
    }
    std::vector<Boss07Output> run(int n) { std::vector<Boss07Output> v; for (int i = 0; i < n; ++i) v.push_back(step()); return v; }
    void attack(int v, std::uint32_t wrap) { Boss07Output o; b.start_attack(v, wrap, o); apply(o); }
    Boss07Block& k() { return b.blk(); }
    // An idle phase whose scheduler does nothing: an open window (bare body), no drift.
    void park(int phase) { k().phase = phase; k().window = 999999; k().barrier = 0; }
};

static Boss07Setup setup(int diff, int chr = 1, bool easy = false, bool ve = false) {
    Boss07Setup s; s.diff_index = diff; s.character = chr; s.diff_easy = easy; s.diff_very_easy = ve;
    return s;
}
static int first_tick(const std::vector<Boss07Output>& v, int se_id, bool se2 = false) {
    for (std::size_t i = 0; i < v.size(); ++i) if (n_se(v[i], se_id, se2)) return int(i);
    return -1;
}
static int sum_se(const std::vector<Boss07Output>& v, int id) { int n = 0; for (const auto& o : v) n += n_se(o, id); return n; }
static int sum_fx(const std::vector<Boss07Output>& v, int id) { int n = 0; for (const auto& o : v) n += n_fx(o, id); return n; }
static int sum_bul(const std::vector<Boss07Output>& v, int kind, int eff) { int n = 0; for (const auto& o : v) n += n_bul(o, kind, eff); return n; }

// ---------------------------------------------------------------------------
static void test_ctor() {
    Rig n, h(setup(1)), nm(setup(2)), toal(setup(0, 3));
    const Boss07Body& d = n.b.body(); const Boss07Block& k = n.b.blk();
    check(veq(d.pos, {0, 4, 0}) && d.hp == 2500 && d.max_hp == 2500 && d.str == 255 && d.def == 999,
          "P1 §3.3 items 1/5/8: y forced to 4.0; EEX rec 6 HP 2500 STR 255; the body's DEF overwritten to 999");
    check(h.b.body().hp == 3125 && nm.b.body().hp == 3750 && toal.b.body().str == 260 && toal.b.part(0).def == 255,
          "P1 §3.3 item 5 / PICTIMOS P12: HP x1.25 Hard, x1.5 Nightmare; Toal STR += 5; the parts keep DEF 255");
    bool parts_ok = true;
    for (int i = 0; i < kParts; ++i) {
        const Boss07Part& p = n.b.part(i);
        parts_ok = parts_ok && p.def == 255 && p.pool == 25000 && p.dmg == 0 && p.radius == 0.5f;
        for (float r : p.resist) parts_ok = parts_ok && r == 0.4f;
    }
    check(parts_ok, "P1 §3.3 item 7: three parts (r 0.5), the stat copy with DEF 255, pool HP x10 = 25000, resists 0.4 x4");
    check(k.cooldown == 60 && k.drift_scale == 1.0f && k.turn_scale == 1.0f && k.cam_slot == 1 && k.fc == 0 &&
          k.phase == 0 && k.mode == 0 && k.barrier == 0 && k.hits == 0,
          "P1 §3.3 items 8/14: blk+0xb0 = 60, +0x1c4 = +0x1d0 = 1.0, +0x1d8 = 1, +0xd0 = +0xd4 = +0xa4 = 0");
    check(k.opt_hp[0] == 1000 && k.opt_hp[1] == 1000 && h.k().opt_hp[0] == 1200 && nm.k().opt_hp[1] == 1400,
          "P1 §3.3 item 10 [s16 0x68bbe8]: option HP 1000 / 1200 / 1400");
    check(veq(n.b.child(kOptR).pos, {0, -995, 0}) && veq(n.b.child(kOptL).pos, {0, -995, 0}) &&
          veq(n.b.child(kWing).pos, {0, -995, 0}), "P1 §3.3 item 9: the three child meshes at (x, y - 999, z)");
    Boss07Setup e = setup(0, 1, true); e.retries = 6;
    Boss07Setup e2 = e; e2.retries = 30;
    Boss07Setup v = setup(0, 1, true, true); v.retries = 4;
    Boss07Setup rush = e; rush.boss_rush = true;
    const float kv = float(std::pow(4.0, 1.25));
    check(Boss07(e).body().str == 252 && Boss07(e).body().def == 996 && Boss07(e2).body().str == 235 &&
          nearf(Boss07(v).body().str, 254 - kv, 1e-3f) && nearf(Boss07(v).body().def, 999 - kv, 1e-3f) &&
          Boss07(rush).body().str == 255 && Boss07(e).part(0).def == 254,
          "P1 §3.3 item 13: retry softening Easy k = n>>1 (n<20) / n-10, VE pow(n,1.25) on STR and the body's 999; not in rush");
    check(barrier_threshold(1, 1, 0) == 25 && barrier_threshold(0, 3, 1) == 35 && barrier_threshold(1, 2, 2) == 45 &&
          barrier_threshold(2, 1, 0) == 10 && barrier_threshold(2, 1, 1) == 14 && barrier_threshold(2, 1, 2) == 18 &&
          barrier_threshold(3, 1, 0) == 15 && barrier_threshold(3, 1, 1) == 21 && barrier_threshold(3, 1, 2) == 27 &&
          barrier_threshold(3, 4, 2) == 0,
          "AB §B.3: T = base + 2(base/5)diff: 25/35/45; W for Hugo 10/14/18, Toal 15/21/27; the dark row 0");
    check(kSectionTable[kNShootR].first == 551 && kSectionTable[kNShootR].last == 589 && !kSectionTable[kNShootR].loop &&
          kSectionTable[kSisLazer].first == 790 && kSectionTable[kSisLazer].loop && kSectionTable[kMeteor].first == 730 &&
          kSectionTable[kN2F].first == 410 && kSectionTable[kKiaiStart].last == 679,
          "P1 §3.2: slot = section; N_SHOOT_R 551..589 stop, SIS_LAZER 790.. loop, METEOR 730.., N2F 410.., KIAI_START ..679");
    float f = 551; std::uint32_t c = 0; int adv = 0;
    while (c == 0) { advance_clip(kNShootR, f, c); ++adv; }
    float f2 = 310; std::uint32_t c2 = 0; int adv2 = 0;
    while (c2 == 0) { advance_clip(kNWait, f2, c2); ++adv2; }
    check(adv == 152 && f == 589 && adv2 == 161 && f2 == 310.25f,
          "AB §0 clip timing: a stop clip a..b is done after 4(b-a) advances (152); a loop first wraps on advance 4(b-a+1)+1 (161)");
}

static void test_shell() {
    Rig r;
    r.in.flag156 = false;
    r.b.body().flash = 1.65f;
    Boss07Output o = r.step();
    check(r.k().fc == 0 && !o.hp_bar && r.b.body().flash == 1.65f && r.draws == 0,
          "AB §A.1: flag 156 clear -> nothing of the boss runs (no counter, no flash decay, no bar)");
    r.in.flag156 = true;
    o = r.step();
    check(r.k().fc == 1 && o.hp_bar && o.hp_bar->hp == 2500 && o.hp_bar->max_hp == 2500 && nearf(r.b.body().flash, 1.575f, 1e-6f),
          "AB §A.1: blk+0xdc++, flash -= 0.075, the HP bar refreshed when D+0x37c != HP (lenScale 9000)");
    o = r.step();
    check(!o.hp_bar && kBarLen == 9000.0f && kEmblem == 7, "AB §A.1: no bar call while D+0x37c == HP; emblem 7 [P1 §3.3 item 14]");
    r.b.body().flash = 0.05f; r.step();
    check(r.b.body().flash == 0.0f, "AB §A.1: the flash floors at 0");
}

static void test_turn_drift() {
    {
        Rig r;
        r.step();
        const Boss07Block& k = r.k();
        check(nearf(k.rot.y, 0.009f, 1e-7f) && nearf(k.rot_target.y, 1.0471976f, 1e-6f) && k.rot.x == 0 && k.fwd0.x == 0 &&
              k.fwd.x != 0 && nearf(k.fwd.z, -1.0f, 1e-3f) && k.turn_scale == 1.0f,
              "AB §A.2: target yaw = cur +/- 60 deg, the yaw steps exactly 0.009 rad; blk+0x1ac loses its x, +0x1b8 keeps it");
    }
    {
        Rig a, b;
        a.k().fwd = {0, 0, -1}; b.k().fwd = {0, 0, -1};
        a.in.player_pos = {5, 4, -10}; b.in.player_pos = {-5, 4, -10};
        a.step(); b.step();
        check(nearf(a.k().rot.y, -0.009f, 1e-7f) && nearf(b.k().rot.y, 0.009f, 1e-7f),
              "AB §A.2: d = wrapPi(yawP - yawF) picks the side: -rate for the player at +x, +rate at -x");
    }
    {
        Rig r;
        r.k().fwd = {0, 0, -1}; r.in.player_pos = {-10, 4, 0};        // 90 deg to the side: no dither for these frames
        r.k().turn_scale = 2.0f; r.k().turn_timer = 3;
        r.step(); r.step(); r.step();
        const float y3 = r.k().rot.y;
        r.step();
        check(nearf(y3, 0.054f, 1e-6f) && r.k().turn_timer == 0 && nearf(r.k().rot.y - y3, 0.009f, 1e-6f) && r.k().turn_scale == 1.0f,
              "AB §A.2: the turn scale applies while blk+0x1d4 runs (rate = s * 0.009), then resets to 1.0");
    }
    auto drift_rig = [](float pz) {
        auto r = std::make_unique<Rig>();
        r->k().phase = 1; r->k().barrier = 1; r->k().cooldown = 9999;
        r->in.player_pos = {0, 0, pz};
        return r;
    };
    {
        auto far = drift_rig(-20), close = drift_rig(-5), mid = drift_rig(-10);
        far->step(); close->step(); mid->step();
        check(nearf(far->b.body().pos.z, -0.06f, 1e-6f) && nearf(close->b.body().pos.z, 0.04f, 1e-6f) &&
              mid->b.body().pos.z == 0.0f && far->b.body().pos.y == 4.0f,
              "AB §A.3: farther than 12 -> approach 0.06 / frame; closer than 8 -> back off 0.04; else hold (XZ only)");
        auto t = drift_rig(-20);
        t->k().drift_scale = 0.5f; t->k().drift_timer = 2;
        t->step(); t->step();
        const float z2 = t->b.body().pos.z;
        t->step();
        check(nearf(z2, -0.06f, 1e-6f) && nearf(t->b.body().pos.z, -0.12f, 1e-6f) && t->k().drift_scale == 1.0f,
              "AB §A.3: the drift scale applies while blk+0x1c8 runs, then resets to 1.0");
    }
    {
        Rig r;
        r.in.player_pos = {0, 0, -30};
        r.fixed = r15(600);
        r.step();                                                  // (frame 1 still has the ctor's scale 1.0 / timer 0)
        const Vec3 p1 = r.b.body().pos;
        r.run(20);
        check(nearf(p1.z, -0.06f, 1e-6f) && veq(r.b.body().pos, p1) && r.k().drift_scale == 0.0f,
              "AB §A.3 / §A.6 phase 0: blk+0x1c4 = 0 / +0x1c8 = 1 every frame -> Dalles holds position (after the ctor's first frame)");
    }
}

static Boss07Output seq(int mode, int sec, std::uint32_t cnt, Rig* keep = nullptr) {
    Rig local;
    Rig& r = keep ? *keep : local;
    r.k().phase = mode == 4 ? 2 : 1; r.k().window = mode == 0 ? 9999 : 0;
    r.k().mode = mode; r.clip(sec); r.frame += 0.25f; r.cnt = cnt;
    return r.step();
}
static void test_sequencer() {
    check(n_clip(seq(0, kKiaiStart, 1), kKiaiLoop) == 1 && n_clip(seq(0, kKiaiLoop, 1), kTunagi) == 1 &&
          n_clip(seq(0, kTunagi, 1), kMeteor) == 1 && n_clip(seq(0, kSisLazerStart, 1), kSisLazer) == 1 &&
          n_clip(seq(0, kSisLazer, 1), kNWait) == 1,
          "AB §A.4 mode 0: KIAI_START -> KIAI_LOOP -> TUNAGI -> METEOR; SIS_LAZER_START -> SIS_LAZER -> N_WAIT on fin");
    check(n_clip(seq(0, kMeteor, 1), kMeteorFromWait) == 1 && n_clip(seq(0, kNShootR, 1), kNWait) == 1 &&
          seq(0, kNShootR, 0).clips.empty() && seq(0, kMeteor, 2).clips.empty() && seq(0, kKiaiLoop, 2).clips.empty(),
          "AB §A.4 mode 0: METEOR fin -> METEOR_FROM_WAIT, any other fin -> N_WAIT; fin is cnt == 1 (a loop at 2 is never left)");
    {
        Rig r; const Boss07Output o = seq(1, kNWait, 0, &r);
        Rig r2; const Boss07Output o2 = seq(1, kWWait, 1, &r2);
        check(n_clip(o, kN2W) == 1 && r.k().mode == 1 && n_clip(seq(1, kNDamage, 0), kN2W) == 1 &&
              n_clip(o2, kNWait) == 1 && r2.k().mode == 0 && n_clip(seq(1, kN2W, 1), kWWait) == 1 &&
              n_clip(seq(1, kWDamage, 1), kWWait) == 1 && n_clip(seq(1, kNShootL, 1), kN2W) == 1 &&
              seq(1, kNShootL, 0).clips.empty() && seq(1, kWWait, 0).clips.empty(),
              "AB §A.4 mode 1: N_WAIT / N_DAMAGE -> N2W at once; N2W / W_DAMAGE fin -> W_WAIT; W_WAIT fin -> N_WAIT, mode 0");
    }
    {
        Rig r2; const Boss07Output o2 = seq(2, kFWait, 1, &r2);
        Rig r3; const Boss07Output o3 = seq(3, kGWait, 1, &r3);
        check(n_clip(seq(2, kNWait, 0), kN2F) == 1 && n_clip(seq(2, kN2F, 1), kFWait) == 1 && n_clip(o2, kNWait) == 1 &&
              r2.k().mode == 0 && n_clip(seq(2, kFDamage, 1), kFWait) == 1 && n_clip(seq(3, kNDamage, 0), kN2G) == 1 &&
              n_clip(seq(3, kN2G, 1), kGWait) == 1 && n_clip(seq(3, kGDamage, 1), kGWait) == 1 && n_clip(o3, kNWait) == 1 &&
              r3.k().mode == 0,
              "AB §A.4 modes 2 / 3: the same with N2F (3) / F_WAIT (7) / 8 and N2G (4) / G_WAIT (9) / 10");
    }
    {
        const Boss07Output a = seq(4, kKiaiStart, 1);
        const Boss07Output b1 = seq(4, kKiaiLoop, 1), b2 = seq(4, kKiaiLoop, 2);
        const Boss07Output c = seq(4, kTunagi, 1);
        const Boss07Output d = seq(4, kMeteor, 1);
        check(n_clip(a, kKiaiLoop) == 1 && a.fader.size() == 1 && bits(*a.fader[0].step) == 0x3c23d70au && *a.fader[0].target == 0 &&
              b1.clips.empty() && n_clip(b2, kTunagi) == 1 && motion(b2, kWing, "METEOR_TO"),
              "AB §A.4 / D23 §6.3 mode 4: KIAI_START fin -> KIAI_LOOP + fader step 0.01 -> 0; KIAI_LOOP needs cnt == 2, wing METEOR_TO");
        check(n_clip(c, kMeteor) == 1 && motion(c, kWing, "METEOR_LOOP") && n_se(c, 353) == 1 && n_se(c, 575) == 1 &&
              n_fx(c, 339) == 1 && n_fx(c, 334) == 1 && n_clip(d, kMeteorFromWait) == 1 && d.fader.size() == 1 &&
              bits(*d.fader[0].step) == 0x3d4ccccdu && *d.fader[0].target == 1.0f,
              "D23 §6.3 / §5: TUNAGI -> METEOR + wing METEOR_LOOP, its f == 730 event the same tick; METEOR fin -> fader 0.05 -> 1");
    }
    {
        Rig r;
        r.k().h_izigen = 77;
        const Boss07Output o = seq(4, kMeteorFromWait, 1, &r);
        check(o.clips.empty() && r.k().mode == 0 && veq(r.b.body().pos, {0, 4, 0}) && o.player.pos &&
              veq(*o.player.pos, {0, 7.5f, -15}) && o.player.flyer && !*o.player.flyer && o.cam_slot &&
              o.cam_slot->first == 4 && o.cam_slot->second == 1.0f && r.k().cam_slot == 4,
              "D23 §6.3 the return: D = (0, 4, 0), P = (0, 7.5, -15), P+0x398 = 0, CamSlot(4, 1.0), blk+0xa4 = 0, no SetClip");
        check(o.skybox_alpha && *o.skybox_alpha == 1.0f && has(o.kill_handles, 77) && r.k().h_izigen == 0 && flag(o, 211, 0) &&
              flag(o, 134, 0) && has(o.player.sections, 9) && motion(o, kWing, "WAIT") && r.b.child(kOptR).pos.y == 1.5f &&
              r.b.child(kOptL).pos.y == 1.5f && bits(*o.fader[0].step) == 0x3d4ccccdu && *o.fader[0].target == 0,
              "D23 §6.3 the return: skybox 1.0, FX 337 killed, flags 211 / 134 = 0, PlayerSection(9), wing WAIT, options y = 1.5");
    }
}

static void test_start_attack() {
    struct Row { int v, dt; float ts; int tt, clip, var; };
    const Row rows[] = {{0, 60, 2.0f, 60, kNShootR, 0}, {1, 60, 2.0f, 60, kNShootR, 1}, {2, 60, 1.75f, 60, kNShootL, 0},
                        {3, 60, 1.75f, 60, kNShootL, 1}, {4, 90, 0.75f, 90, kNImpactR, 1}, {5, 90, 0.75f, 90, kNImpactR, 0},
                        {7, 240, 2.0f, 240, kSisLazerStart, 0}, {8, 500, 1.25f, 500, kKiaiStart, 0}};
    bool ok = true;
    for (const Row& w : rows) {
        Rig r;
        r.k().variant = 9; r.k().cooldown = 0;
        Boss07Output o; r.b.start_attack(w.v, 6, o);
        ok = ok && r.k().drift_scale == 0.0f && r.k().drift_timer == w.dt && r.k().turn_scale == w.ts &&
             r.k().turn_timer == w.tt && o.clips.size() == 1 && o.clips[0].slot == w.clip && r.k().variant == w.var &&
             r.k().cooldown == 110 && r.k().cycle == 1;
    }
    check(ok, "AB §A.5: variants 0..5, 7, 8 — drift 0 / frames, turn scale / frames, clip, blk+0xb4, cooldown 110, cycle++");
    Rig r6; r6.k().next_barrier = 3; r6.k().variant = 1;
    Boss07Output o6; r6.b.start_attack(6, 7, o6);
    check(o6.clips.empty() && r6.k().mode == 3 && r6.k().variant == 1 && r6.k().drift_timer == 60 && r6.k().turn_scale == 1.2f &&
          r6.k().turn_timer == 360 && r6.k().cooldown == 110,
          "AB §A.5 variant 6: no clip, blk+0xb4 unchanged, blk+0xa4 = blk+0xa8, drift 0 / 60, turn 1.2 / 360");
    Rig e(setup(0, 1, true)), h(setup(1)), nm(setup(2)), w;
    Boss07Output o;
    e.b.start_attack(8, 8, o); h.b.start_attack(0, 6, o); nm.b.start_attack(0, 6, o);
    w.k().cycle = 5; w.k().cooldown = 7; w.b.start_attack(9, 6, o);
    check(e.k().turn_scale == 1.0f && h.k().cooldown == 100 && nm.k().cooldown == 90 && w.k().cycle == 0 && w.k().cooldown == 7,
          "AB §A.5: variant 8's turn scale 1.0 on Easy; cooldown 110 / 100 / 90; variant > 8 only steps the cycle (wraps at 6)");
}

static void test_scheduler() {
    int odds = 0;
    for (std::uint32_t v = 0; v < 32768; ++v) odds += (v % 5000) < 500;
    check(odds == 3500, "AB C8: R(5000) < 500 on the 15-bit value is true for 3500 of 32768 words (10.68 %)");
    {   // phase 0
        Rig r;
        r.fixed = r15(500);
        r.run(60);
        const bool counted = r.k().cooldown == 0 && r.draws == 0;
        Boss07Output o = r.step();
        const bool miss = r.draws == 1 && o.clips.empty();
        r.forced.push_back(r15(499));
        o = r.step();
        check(counted && miss && r.draws == 2 && n_clip(o, kNShootR) == 1 && r.k().variant == 0 && r.k().cycle == 1 &&
              r.k().cooldown == 110,
              "AB §A.6 phase 0: 60 eligible frames of cooldown, then 1 draw a frame; R = 500 misses, 499 starts table[0] = variant 0");
        r.clip(kNWait); r.k().refill = true; r.k().cooldown = 0;
        const int d2 = r.draws; r.b.body().hp = r.b.body().max_hp;
        r.step();
        check(r.draws == d2, "AB §A.6: not eligible while the refill byte blk+0xb8 is set (no draw)");
    }
    {
        Rig r;
        std::vector<std::pair<int, int>> got;
        for (int i = 0; i < 7; ++i) {
            r.clip(kNWait); r.k().cooldown = 0; r.forced.push_back(r15(5000));
            const Boss07Output o = r.step();
            got.push_back({o.clips.empty() ? -1 : o.clips[0].slot, r.k().variant});
        }
        const std::vector<std::pair<int, int>> want = {{kNShootR, 0}, {kNShootL, 0}, {kNShootR, 1}, {kNShootL, 1},
                                                       {kNImpactR, 1}, {kNImpactR, 0}, {kNShootR, 0}};
        check(got == want, "AB §A.6 phase 0 [u32 0x69dc60]: the cycle 0, 2, 1, 3, 4, 5 with wrap 6 (R = 5000 -> 0 starts too)");
    }
    {   // phase 1: the raise
        Rig r;
        r.k().phase = 1; r.k().cooldown = 0; r.k().meter = 9;
        r.forced.push_back(r15(12345));
        Boss07Output o = r.step();
        const bool first = r.draws == 1 && r.k().mode == 1 && r.k().next_barrier == 1 && r.k().hit_pause == 59 &&
                           r.k().drift_scale == 0 && r.k().drift_timer == 360 && r.k().turn_scale == 1.2f &&
                           r.k().turn_timer == 360 && r.k().meter == 0 && n_clip(o, kN2W) == 1;
        std::vector<int> modes;
        for (int i = 0; i < 3; ++i) {
            r.k().mode = 0; r.k().barrier = 0; r.clip(kNWait); r.k().cooldown = 0;
            r.step();
            modes.push_back(r.k().mode);
        }
        check(first && modes == std::vector<int>({2, 3, 1}),
              "AB §A.6 phase 1: barrier down -> 1 draw, blk+0xbc = 0, +0x178 = 60, drift 0 / 360, turn 1.2 / 360, W -> F -> E -> W");
    }
    {   // phase 1: dash
        Rig r;
        r.k().phase = 1; r.k().barrier = 1; r.k().cooldown = 0;
        r.forced = {r15(10000), r15(5), r15(7)};
        Boss07Output o = r.step();
        const bool dash = r.draws == 3 && o.move_to && veq(o.move_to->target, {11, 4, 13}) && o.move_to->speed == 0.25f &&
                          o.move_to->radius == 1.0f && r.b.body().move_speed == 0.25f && r.k().turn_scale == 1.8f &&
                          r.k().turn_timer == 60 && r.k().dash_bias == 249;
        r.step();
        check(dash && r.draws == 3,
              "AB §A.6 / C7: dash at 10000 <= r < 11500 - bias: z = 18 - R(36) of the FIRST word, x of the second; 0.25, r 1.0; IsMoving blocks");
        Rig b;
        b.k().phase = 1; b.k().barrier = 1; b.k().cooldown = 0; b.k().dash_bias = 250;
        b.forced = {r15(11250), r15(600)};
        o = b.step();
        Rig c;
        c.k().phase = 1; c.k().barrier = 1; c.k().cooldown = 0;
        c.forced = {r15(11499)};
        c.fixed = r15(600);
        const Boss07Output oc = c.step();
        Rig lo;
        lo.k().phase = 1; lo.k().barrier = 1; lo.k().cooldown = 0;
        lo.forced = {r15(9999), r15(600)};
        const Boss07Output ol = lo.step();
        check(!o.move_to && b.draws == 2 && b.k().dash_bias == 249 && oc.move_to && c.k().dash_bias == 249 && !ol.move_to && lo.draws == 2,
              "AB §A.6 / C8: the dash window is [10000, 11500 - bias): 11499 dashes at bias 0, 11250 does not at bias 250, 9999 never");
    }
    {   // phase 1: the seventh attack and the window
        Rig r;
        r.k().phase = 1; r.k().barrier = 3; r.k().cooldown = 0; r.k().cycle = 6; r.k().next_barrier = 2; r.k().fc = 1;
        r.forced = {r15(0), r15(0)};
        Boss07Output o = r.step();
        check(r.draws == 2 && r.k().mode == 2 && r.k().cycle == 0 && r.k().cooldown == 110 && n_clip(o, kN2F) == 1,
              "AB §A.5 / §A.6 phase 1 [0x69dc44]: cycle step 6 = variant 6 -> the current barrier's raise mode again (wrap 7)");
        Rig w;
        w.k().phase = 1; w.k().window = 5; w.k().cooldown = 0;
        w.step();
        check(w.draws == 0 && w.k().window == 4 && w.k().drift_scale == 0 && w.k().drift_timer == 1,
              "AB §A.6: an open window only counts blk+0xd8 down with drift scale 0 — nothing is started");
    }
    {   // phase 2: the option-shot interval
        auto shot = [](const Boss07Setup& s, bool win) {
            Rig r(s);
            r.k().phase = 2; r.k().barrier = win ? 0 : 4; r.k().window = win ? 50 : 0; r.k().cooldown = 9999; r.k().fc = 1;
            r.forced = {r15(100)};
            r.step();
            return std::make_pair(r.k().shot_timer, r.k().shot * 10 + r.draws);
        };
        check(shot(setup(0), false) == std::make_pair(280, 11) && shot(setup(1), false).first == 270 &&
              shot(setup(2), false).first == 260 && shot(setup(0, 1, true), false).first == 340 &&
              shot(setup(0), true) == std::make_pair(0, 0),
              "AB §A.6 phase 2: blk+0x17c == 0 -> blk+0x180 = 1, interval 180 / 170 / 160 + R(360) (+60 Easy), 1 draw; not in the window");
    }
    {   // phase 2: re-raise + revive
        Rig r;
        r.k().phase = 2; r.k().barrier = 0; r.k().opt_hp[0] = r.k().opt_hp[1] = 0; r.k().shot_timer = 50; r.k().fc = 1;
        r.k().meter = 5; r.k().hits = 3; r.b.child(kOptR).hits_off = r.b.child(kOptL).hits_off = true;
        r.k().drift_scale = 0; r.k().drift_timer = 9;
        const Boss07Output o = r.step();
        check(r.draws == 0 && r.k().barrier == kBarD && r.k().hits == 0 && r.k().meter == 0 && r.k().opt_hp[0] == 1000 &&
              r.k().opt_hp[1] == 1000 && !r.b.child(kOptR).hits_off && !r.b.child(kOptL).hits_off && r.k().rev[0] == 180 &&
              r.k().rev[1] == 180 && r.k().shot_timer == 49 && n_se(o, 319) == 0 && o.clips.empty(),
              "AB §A.6 phase 2: window over -> blk+0xd0 = 4, hits = 0, no SE / clip / draw; both options dead -> REVIVE (HP, flag 0x40)");
        check(vnear(r.b.child(kOptR).pos, {-8.6001f, 1.5f, 6.8584f}, 2e-3f) && vnear(r.b.child(kOptL).pos, {8.6001f, 1.5f, 6.8584f}, 2e-3f),
              "AB C10: revived at D - 11(cos, sin)(theta +/- 0.8976), y - 2.5: behind Dalles, 51.4 deg either side");
    }
    {   // phase 2: dash and attack
        Rig r;
        r.k().phase = 2; r.k().barrier = 4; r.k().cooldown = 0; r.k().shot_timer = 99; r.k().fc = 1;
        r.forced = {r15(10500), r15(0), r15(35)};
        const Boss07Output o = r.step();
        check(o.move_to && veq(o.move_to->target, {-17, 4, 18}) && o.move_to->speed == 0.1875f && o.move_to->radius == 0.75f && r.draws == 3,
              "AB §A.6 phase 2: the dash is 0.1875 with arrival radius 0.75 (x = 18 - 35, z = 18 - 0)");
        auto pick = [](int diff, std::uint32_t t, float hp_frac, bool easy = false) {
            auto q = std::make_unique<Rig>(setup(diff, 1, easy));
            q->k().phase = 2; q->k().barrier = 4; q->k().cooldown = 0; q->k().shot_timer = 99; q->k().fc = 1;
            q->b.body().hp = q->b.body().max_hp * hp_frac;
            q->forced = {r15(0), r15(0), r15(t)};
            q->fixed = r15(600);
            const Boss07Output oo = q->step();
            return std::make_tuple(oo.clips.empty() ? -1 : oo.clips[0].slot, q->draws, n_fx(oo, 402), q->k().rr, q->k().cycle);
        };
        check(pick(0, 0, 1.0f) == std::make_tuple(int(kNShootR), 3, 0, 0, 1u) && std::get<0>(pick(0, 1, 1.0f)) == kSisLazerStart &&
              std::get<0>(pick(0, 2, 1.0f)) == kNImpactR,
              "AB §A.6 phase 2: R(50000), R(5000), R(3) = 3 draws; tables 0x69dc24 / 04 / e4 start 0, 7, 4 (wrap 8)");
        check(pick(1, 0, 0.3f) == std::make_tuple(int(kNShootR), 3, 2, 1, 1u) && std::get<2>(pick(2, 0, 0.3f)) == 2 &&
              std::get<2>(pick(1, 0, 0.36f)) == 0 && std::get<2>(pick(0, 0, 0.3f)) == 0 && std::get<2>(pick(1, 0, 0.3f, true)) == 0,
              "AB §A.6 / D23 §9: Hard / NM below 35 % queue one barrier attack (W first; fired by the executor the same frame); not on Easy / Normal");
    }
    {   // the damage cap and the refill
        auto cap = [](int phase) {
            Rig r;
            r.k().phase = phase; r.k().meter = 875.5f; r.k().window = 100; r.k().cooldown = 77;
            r.step();
            return std::make_tuple(r.k().window, r.k().meter, r.k().cooldown);
        };
        check(cap(1) == std::make_tuple(0u, 0.0f, 0) && cap(2) == std::make_tuple(0u, 0.0f, 60) && std::get<1>(cap(0)) == 875.5f,
              "AB §A.6 tail / C1: damage > 34 % of max HP closes the window, cooldown 0 (phase 1) / 60 (phase 2); not in phase 0");
        Rig r;
        r.fixed = r15(600);
        r.b.body().hp = 1; r.b.body().max_hp = 3750; r.k().refill = true;
        r.run(149);
        const bool before = r.k().phase == 0 && r.b.body().hp == 3726;
        r.step();
        check(before && r.k().phase == 1 && !r.k().refill && r.b.body().hp == 3751,
              "AB §A.6 tail: the refill adds 25 a frame; blk+0xa0++ and the byte clears when HP >= max HP");
    }
}

static void test_damage() {
    {   // the resist table and the classes, through the engine's formula (hit.hpp FUN_004fa550)
        auto resist = [](int barrier) {
            auto r = std::make_unique<Rig>();
            r->k().phase = 1; r->k().barrier = barrier; r->k().cooldown = 9999; r->k().fc = 1;
            r->step();
            return r;
        };
        auto dmg = [](const Boss07Part& p, int cls, float atk) {
            AttackStats a; a.atk = atk;
            EnemyDefense v; v.def = p.def;
            for (int i = 0; i < 4; ++i) v.resist[i] = p.resist[i];
            return enemy_incoming_damage(a, cls, v, PlayerDamageMods{}, [](float) { return false; });
        };
        auto bare = resist(0), w = resist(1), e = resist(2), f = resist(3), d = resist(4);
        const float raw = std::pow(300.0f, 0.66f) * (300.0f - 0.98f * 255.0f + 10.0f) / 22.92f;
        check(w->b.part(0).resist[0] == 0.99f && w->b.part(0).resist[1] == 1.0f && e->b.part(0).resist[2] == 0.99f &&
              e->b.part(0).resist[0] == 1.0f && f->b.part(0).resist[1] == 0.99f && f->b.part(0).resist[2] == 1.0f &&
              d->b.part(0).resist[0] == 1.0f && d->b.part(0).resist[3] == 1.0f && bare->b.part(0).resist[3] == 0.4f &&
              w->b.part(1).resist[0] == 0.4f && w->b.part(0).radius == 1.5f && w->b.part(1).radius == 3.0f,
              "AB §B.1 [table 0x548ff8]: part 0 resists by blk+0xd0 (W 0.99 slot 0, E slot 2, F slot 1, D all 1.0); options keep 0.4");
        check(dmg(bare->b.part(0), 3, 300) == int(std::floor(raw * 0.6f)) && dmg(bare->b.part(0), 0, 300) == dmg(bare->b.part(0), 3, 300) &&
              dmg(bare->b.part(0), 3, 100) == 1,
              "AB §B.2: bare body — every class x0.6; raw 0 (ATK below 0.98 DEF - 10) still deals the min-1");
        check(dmg(w->b.part(0), 0, 300) == 1 && dmg(w->b.part(0), 4, 300) == 1 && dmg(w->b.part(0), 1, 300) == 0 &&
              dmg(w->b.part(0), 2, 300) == 0 && dmg(w->b.part(0), 3, 300) == 0 && dmg(w->b.part(0), 0, 100) == 1,
              "AB §B.2 barrier W: only class 0 / 4 passes, at 1 % floored to 1 (min-1); resist 1.0 = immune, 0 damage");
        check(dmg(e->b.part(0), 2, 300) == 1 && dmg(e->b.part(0), 6, 300) == 1 && dmg(e->b.part(0), 0, 300) == 0 &&
              dmg(f->b.part(0), 1, 300) == 1 && dmg(f->b.part(0), 5, 300) == 1 && dmg(f->b.part(0), 2, 300) == 0 &&
              dmg(d->b.part(0), 0, 300) == 0 && dmg(d->b.part(0), 1, 300) == 0 && dmg(d->b.part(0), 2, 300) == 0 &&
              dmg(d->b.part(0), 3, 300) == 0,
              "AB §B.2: barrier E passes class 2 / 6, F class 1 / 5, D nothing");
    }
    {   // the hit path's gate
        Rig r;
        const Boss07Hit a = r.b.hit_part(0, 0), b = r.b.hit_part(0, 40);
        const bool acc = a == Boss07Hit::kClank && b == Boss07Hit::kLanded && r.b.part(0).dmg == 40 && r.b.part(0).pool == 24960;
        r.b.part(1).pool = 0.5f;
        check(acc && r.b.hit_part(1, 10) == Boss07Hit::kRefused && r.b.part(1).dmg == 0,
              "AB §B.2: dmg <= 0 on a type-6 part = the clank (SE 368), +0x134 += dmg, the pool drains; pool < 1 refuses the hit");
    }
    {   // bare intake
        Rig r;
        r.fixed = r15(600);
        r.b.hit_part(0, 100);
        const Boss07Output o = r.step();
        check(r.b.body().hp == 2400 && r.k().meter == 100 && n_clip(o, kNDamage) == 1 && nearf(r.b.body().flash, 1.575f, 1e-6f) &&
              r.k().hits == 0 && r.b.part(0).dmg == 0 && r.b.mini(0).timer == 0 && vnear(r.b.mini(0).anchor, {0, -1.25f, 0}),
              "AB §B.3: HP -= dmg, blk+0xbc += dmg, flash 1.65, N_WAIT -> N_DAMAGE in phase 0; no barrier count; anchor = core - 5.25");
        r.in.player_hp_mirror = 0;
        r.b.hit_part(0, 100);
        r.step();
        check(r.b.body().hp == 2400 && r.b.part(0).dmg == 100, "AB §B: nothing of FUN_005472e0 runs while the player HP mirror < 1");
    }
    {   // barrier count and break
        Rig r;
        r.k().phase = 1; r.k().barrier = 1; r.k().cooldown = 9999; r.k().hit_pause = 3; r.k().fc = 1;
        r.k().drift_scale = 0; r.k().drift_timer = 99999;
        bool quiet = true;
        for (int i = 0; i < 3; ++i) { r.b.hit_part(0, 1); quiet = quiet && r.step().clips.empty(); }
        const bool paused = r.k().hits == 0 && r.k().hit_pause == 0;
        for (int i = 0; i < 24; ++i) { r.b.hit_part(0, 1); r.b.hit_part(0, 1); quiet = quiet && r.step().clips.empty(); }
        const bool counted = r.k().hits == 24 && r.b.mini(0).timer == 255 && r.b.mini(0).value == 1 && r.b.mini(0).max == 25 &&
                             r.b.body().hp == 2500 - 3 - 48;
        check(paused && counted && quiet,
              "AB §B.3 / 0.1: one count per FRAME with damage, none while blk+0x178 != 0; gauge 0 = T - hits / T; no flinch behind a barrier");
        {   // MINI_GAUGE.md §2 / §3: the shared store, the HUD tick's 255 countdown, the quad
            Rig s;
            WorldGauge store[3];
            s.b.bind_gauges(store);
            s.k().phase = 1; s.k().barrier = 1; s.k().cooldown = 9999; s.k().fc = 1;
            s.k().drift_scale = 0; s.k().drift_timer = 99999;
            s.b.hit_part(0, 1); s.step();
            const bool wrote = store[0].timer == 255 && store[0].value == 24 && store[0].max == 25 && &s.b.mini(0) == &store[0];
            WorldGaugeQuad q{};
            WorldGauge half{255, 50, 100, {}};
            const bool quad = world_gauge_quad(half, 512, 384, 255, &q) && q.x0 == 472 && q.y0 == 284 && q.x1 == 514 && q.y1 == 296 &&
                              q.sx == 288 && q.sy == 24 && q.sw == 42 && q.sh == 12 && q.a == 254;
            WorldGauge over{1, 150, 100, {}};                                // not clamped; alpha (255 x 1) >> 8 = 0
            const bool wide = world_gauge_quad(over, 0, 0, 255, &q) && q.x1 == -40.0f + 126.0f && q.a == 0;
            WorldGauge nomax{255, 1, 0, {}}, off{0, 1, 1, {}};
            const bool gates = !world_gauge_quad(nomax, 0, 0, 255, &q) && !world_gauge_quad(off, 0, 0, 255, &q);
            for (int i = 0; i < 254; ++i) tick_world_gauges(store);
            const bool last = store[0].timer == 1 && store[0].value == 24;
            tick_world_gauges(store);
            const bool zeroed = store[0].timer == 0 && store[0].value == 0 && store[0].max == 0 && store[0].anchor.y == 0 &&
                                s.b.mini(0).timer == 0;
            check(wrote && quad && wide && gates && last && zeroed,
                  "MINI_GAUGE §2 / §3: the brain writes the bound store; quad {472, 284, 514, 296} cell {288, 24, 42, 12} alpha 254; "
                  "unclamped; 255 HUD ticks zero the record (the bar fades out)");
        }
        r.b.hit_part(0, 1);
        r.forced = {kU25, kU50, kU75};
        const int d0 = r.draws;
        const Boss07Output o = r.step();
        const Boss07Effect* e = fx(o, 140);
        check(n_se(o, 320) == 1 && r.k().hits == 0 && r.k().barrier == 0 && r.k().window == 599 && r.k().mode == 0 &&
              o.clips.size() == 1 && o.clips[0].slot == kNDamage && r.b.mini(0).value == 0 && r.b.mini(0).max == 25 &&
              r.draws - d0 == 3,
              "AB §B.3 break: SE 320, hits = 0, blk+0xd0 = 0, window 600 (-1 by the scheduler the same frame), one N_DAMAGE, exactly 3 draws");
        check(e && e->call == Boss07Call::kInline && e->side == 1 && e->stat && e->power == 0 && e->knock == 1.0f && e->cls == -1 &&
              e->or_1dc == 0x100 && e->parent == Boss07Parent::kNode && e->parent_index == kCore && veq(e->at_scale, {2, 2, 2}) &&
              e->at_mode == 2 && vnear(e->rot, {270 * kDeg, 180 * kDeg, 90 * kDeg}, 1e-5f) && veq(e->pos, {0, 4, 0}),
              "AB §B.4 / C4: effect 140 inline on the core node, attach scale (2,2,2) mode 2; w1, w2, w3 -> rot.z, rot.y, rot.x");
    }
    {
        auto window = [](const Boss07Setup& s, int T) {
            Rig r(s);
            r.k().phase = 1; r.k().barrier = 1; r.k().cooldown = 9999; r.k().mode = 2; r.k().fc = 1; r.clip(kNShootR);
            int frames = 0;
            while (r.k().barrier == 1 && frames < 100) { r.b.hit_part(0, 1); r.step(); ++frames; }
            return frames == T ? int(r.k().window) + 1 + 1000 * r.k().mode : -1;
        };
        check(window(setup(0), 25) == 600 && window(setup(1), 35) == 700 && window(setup(2), 45) == 760 &&
              window(setup(0, 1, true), 25) == 660 && window(setup(0, 2), 10) == 600 && window(setup(2, 3), 27) == 760,
              "AB §B.3: the break after 25 / 35 / 45 hit frames (Hugo's W 10, Toal NM 27); window 600 / 700 / 760 (+60 Easy); mode cleared");
    }
}

static void test_phase_ends() {
    {   // phase 0
        Rig r;
        r.b.body().hp = 50;
        r.b.hit_part(0, 100);
        r.forced = {r15(3), r15(8)};
        const Boss07Output o = r.step();
        check(r.b.body().max_hp == 3750 && r.b.body().hp == 26 && r.k().refill && r.k().meter == 0 && r.k().phase == 0 && r.draws == 2 &&
              o.hp_bar && o.hp_bar->max_hp == 3750,
              "AB §B.5 / §A.6: HP < 1 in phase 0 -> clamp 1.0, max HP += 1250, the refill starts the same frame (+25); 2 draws");
        check(o.move_to && veq(o.move_to->target, {12, 4, 17}) && bits(o.move_to->speed) == 0x3e8ccccdu && o.move_to->radius == 1.0f &&
              r.k().hit_pause == 59 && r.k().drift_scale == 0 && r.k().drift_timer == 360 && r.k().turn_scale == 1.2f &&
              r.k().turn_timer == 360 && r.k().mode == 1 && r.k().next_barrier == 1 && o.fog && o.fog->first == 1 &&
              bits(o.fog->second) == 0x3b23d70au,
              "AB §B.6.1: move-to (20 - R(40) of w2, y, 20 - R(40) of w1) at 0.275 r 1.0; +0x178 = 60; mode 1 / next 1; ChangeFog(1, 0.0025)");
        const Boss07Effect* a = fx(o, 440, 0); const Boss07Effect* c = fx(o, 440, 2); const Boss07Effect* m = fx(o, 172);
        check(n_fx(o, 440) == 3 && a && c && m && a->call == Boss07Call::kFx2 && a->local_scale == 1.0f && fx(o, 440, 1)->local_scale == 1.5f &&
              c->local_scale == 2.0f && c->parent == Boss07Parent::kBody && c->at_mode == 2 && m->local_scale == 5.0f &&
              m->call == Boss07Call::kFx2 && m->stat && m->side == 1 && m->power == 0 && veq(m->pos, r.b.body().pos),
              "AB §B.6.3 / C3: FX2 440 x3 with local scale 1.0 / 1.5 / 2.0 and FX2 172 with 5.0, attached to the body matrix, mode 2");
        check(o.cam_slot && o.cam_slot->first == 2 && bits(o.cam_slot->second) == 0x3ccccccdu && r.k().cycle == 0 && r.k().cooldown == 110 &&
              n_se(o, 186, true) == 1 && o.shake && o.shake->first == 120 && veq(o.shake->second, {0.3f, 0.3f, 0.3f}) &&
              o.fader.size() == 1 && *o.fader[0].colour == 0x00aa0000u && bits(*o.fader[0].step) == 0x3c360b61u &&
              *o.fader[0].current == 1.0f && *o.fader[0].target == 0 && o.fader[0].zero_30 && has(o.rumble, 5) && o.skin_body == 2 &&
              !o.skin_options,
              "AB §B.6.3 / §B.6.4: CamSlot(2, 0.025), cycle 0, cooldown, SE2 186, Shake(120, 0.3), red fader 1/90, Rumble(5), skin ver2");
        check(o.clips.size() == 2 && o.clips[0].slot == kNDamage && o.clips[1].slot == kN2W,
              "AB §B.3 / §A.4: the flinch, then the sequencer starts N2W_CHANGE the same frame (mode 1, clip N_DAMAGE)");
        r.fixed = r15(600);
        r.b.hit_part(0, 500);
        const Boss07Output o2 = r.step();
        check(r.b.body().max_hp == 3750 && n_fx(o2, 440) == 3 && n_fx(o2, 172) == 1 && r.b.body().hp == 26,
              "AB §B.5 re-entry: below 1 again during the refill -> the whole block again, only the max-HP increment is latched");
        int frames = 0;
        while (r.k().phase == 0 && frames < 400) { r.step(); ++frames; }
        check(frames == 149 && r.b.body().hp == 3751 && r.k().barrier == kBarW,
              "AB §A.6: 26 + 25 x 149 >= 3750 -> phase 1; meanwhile N2W_CHANGE's frame 361 raised barrier W");
    }
    auto end1 = [](int barrier) {
        auto r = std::make_unique<Rig>();
        r->k().phase = 1; r->k().barrier = barrier; r->k().cooldown = 9999; r->k().fc = 1; r->k().next_barrier = 2; r->k().mode = 0;
        r->b.body().max_hp = 3750; r->b.body().hp = 10;
        r->b.hit_part(0, 50);
        return r;
    };
    {   // phase 1
        auto r = end1(1);
        const Boss07Output o = r->step();
        check(r->draws == 5 && r->b.body().max_hp == 4250 && r->b.body().hp == 26 && r->k().barrier == kBarD && r->k().hits == 0 &&
              n_fx(o, 140) == 1 && n_se(o, 319) == 1 && r->b.mini(0).timer == 0,
              "AB §B.6.2 / C2: max HP += 500; 2 discarded draws + 3 for the spark of the element barrier; blk+0xd0 = 4, SE 319, gauge off");
        check(o.fog && o.fog->first == 2 && o.skin_options && o.skin_body == 3 && o.cam_slot && o.cam_slot->first == 4 &&
              r->k().turn_scale == 1.5f && r->k().turn_timer == 60 && r->k().drift_scale == 0 && r->k().drift_timer == 9999 &&
              r->k().mode == 0 && r->k().next_barrier == 0 && n_fx(o, 440) == 3 && n_fx(o, 172) == 1,
              "AB §B.6.2: CamSlot 4, turn 1.5 / 60, drift 0 / 9999, mode / next = 0, ChangeFog(2), option skin op01, body skin ver3");
        auto q = end1(0);
        q->k().window = 50;
        q->step();
        check(q->draws == 2, "AB §B.6.2: with no element barrier up only the two discarded draws");
        int frames = 0;
        r->fixed = r15(600);
        while (r->k().phase == 1 && frames < 400) { r->step(); ++frames; }
        check(r->k().phase == 2 && r->b.body().hp >= 4250, "AB §A.6: the second refill ends in phase 2 (2500 -> 3750 -> 4250)");
    }
    {   // phase 2: the kill
        Rig r;
        r.park(2);
        r.b.body().hp = 10;
        r.b.hit_part(0, 100);
        const Boss07Output o = r.step();
        check(r.b.body().hp == 0 && flag(o, 226, 1) && o.exp && o.clips.size() == 2 && o.clips[1].slot == kMeteor && r.k().death_fc == 1 &&
              r.b.body().max_hp == 2500 && !r.k().refill,
              "AB §B.5 / §A.1: HP < 1 in phase 2 -> HP = 0 and the death function runs the SAME frame (no alive subsystem)");
    }
}

static void test_options_parts() {
    auto rig = [](const Boss07Setup& s) {
        auto r = std::make_unique<Rig>(s);
        r->k().phase = 2; r->k().barrier = 4; r->k().cooldown = 9999; r->k().shot_timer = 99999; r->k().fc = 1;
        r->k().drift_scale = 0; r->k().drift_timer = 99999;
        r->b.child(kOptR).pos = {-9, 2.5f, 7}; r->b.child(kOptL).pos = {9, 2.5f, 7};
        return r;
    };
    {
        Rig p1;
        p1.k().phase = 1; p1.k().barrier = 1; p1.k().cooldown = 9999;
        p1.b.child(kOptR).pos = {-9, 2.5f, 7};
        p1.step();
        auto r = rig(setup(0));
        r->k().opt_hp[1] = 0;
        r->step();
        const Vec3 pr = r->b.child(kOptR).pos, pl = r->b.child(kOptL).pos;
        check(nearf(p1.b.part(1).pos.y, p1.b.child(kOptR).pos.y - 999, 1e-3f) && veq(r->b.part(1).pos, pr) &&
              nearf(r->b.part(2).pos.y, pl.y - 999, 1e-3f) && vnear(r->b.mini(1).anchor, {pr.x, pr.y - 0.5f, pr.z}) &&
              vnear(r->b.mini(2).anchor, {pl.x, pl.y - 0.5f, pl.z}),
              "AB §B.7: option parts follow the options (r 3.0), 999 lower before phase 2 and when destroyed; anchors = pos - 0.5");
    }
    {
        auto r = rig(setup(0));
        r->b.hit_part(1, 400);
        Boss07Output o = r->step();
        check(r->k().opt_hp[0] == 600 && r->b.child(kOptR).flash_2a0 == 1.65f && nearf(r->b.body().flash, 1.575f, 1e-6f) &&
              r->b.mini(1).timer == 255 && r->b.mini(1).value == 600 && r->b.mini(1).max == 1000 && r->b.mini(0).timer == 255 &&
              r->b.mini(0).value == 1600 && r->b.mini(0).max == 2000 && r->b.body().hp == 2500 && r->b.mini(2).timer == 0,
              "AB §B.7: option HP -= dmg, both flash; Normal option gauge (max literal 1000); the D gauge = hpR + hpL / 2 x option HP");
        r->b.hit_part(1, 700);
        o = r->step();
        const Boss07Effect* e = fx(o, 320);
        check(r->k().opt_hp[0] == 0 && r->b.child(kOptR).hits_off && e && e->call == Boss07Call::kFx2 && n_se(o, 369) == 1 &&
              r->k().barrier == kBarD && n_se(o, 320) == 0,
              "AB §B.7: HP < 1 -> 0, opt+0x298 |= 0x40, FX2 320 kari_sini, SE 369; the dark barrier holds while one option lives");
        r->k().mode = 3;
        r->b.hit_part(2, 1500);
        const int d0 = r->draws;
        o = r->step();
        check(r->k().opt_hp[1] == 0 && n_se(o, 320) == 1 && r->k().barrier == 0 && r->k().hits == 0 && r->k().window == 700 &&
              r->k().cooldown == 110 && r->draws - d0 == 3 && n_fx(o, 140) == 1 && n_clip(o, kNDamage) == 1 && r->k().mode == 3,
              "AB §B.8 / C5: the dark barrier falls the frame the second option dies: SE 320, window 700, cooldown, 3 draws; mode kept");
    }
    auto dark = [&](const Boss07Setup& s) {
        auto r = rig(s);
        r->k().opt_hp[0] = 0;
        r->b.hit_part(2, 5000);
        r->step();
        return std::make_tuple(int(r->k().window) + 1, r->b.mini(2).timer, r->b.mini(0).max, r->k().cooldown);
    };
    check(dark(setup(1)) == std::make_tuple(800, 0, 2400.0f, 100) && dark(setup(2)) == std::make_tuple(860, 0, 2800.0f, 90) &&
          std::get<0>(dark(setup(0, 1, true))) == 760,
          "AB §B.7 / §B.8 [u32 0x68bb34]: dark window 700 / 800 / 860 (+60 Easy); option gauges on the Normal row only; D max 2400 / 2800");
}

static void test_options_and_shot() {
    {
        Rig r;
        const Boss07Output o = r.step();
        check(r.b.child(kOptR).alpha == 0 && r.b.child(kOptL).alpha == 0 && veq(r.b.child(kOptR).pos, {0, -995, 0}) &&
              r.b.child(kWing).alpha == 0 && veq(r.b.child(kWing).pos, r.b.body().pos) && !o.glare && !r.k().behind,
              "CD1 §2.4 / §2.5: phase 0 — options invisible and not moved (y - 999); the wing follows the body with alpha 0");
        r.in.player_pos = {0, 0, 14.5f}; r.in.opt_count = {1, 0};
        const Boss07Output o2 = r.step();
        check(r.k().behind && motion(o2, kOptR, "N_WAIT") && !motion(o2, kOptL, "N_WAIT"),
              "CD1 §2.1 / §2.4: blk+0x15c = the player is > 90 deg off the hontai -> core axis; a finished option clip -> N_WAIT");
    }
    auto hold = [](int phase) {
        auto r = std::make_unique<Rig>();
        r->k().phase = phase; r->k().barrier = phase == 2 ? 4 : 1; r->k().cooldown = 99999; r->k().shot_timer = 999999;
        r->k().drift_scale = 0; r->k().drift_timer = 999999;
        r->fixed = r15(600);
        return r;
    };
    {
        auto r = hold(1);
        r->step();
        const bool a1 = nearf(r->b.child(kOptR).alpha, 0.01f, 1e-7f) && r->b.statics().opt_alpha[0] == r->b.child(kOptR).alpha;
        r->run(400);
        const Vec3 pr = r->b.child(kOptR).pos, pl = r->b.child(kOptL).pos;
        check(a1 && r->b.child(kOptR).alpha == 1.0f && vnear(pr, {-8.6001f, 2.5f, 6.8584f}, 5e-3f) && vnear(pl, {8.6001f, 2.5f, 6.8584f}, 5e-3f),
              "CD1 §2.4 phase 1: alpha +0.01 / tick to 1.0; the orbit converges (rate 0.1) on radius 11 at b +/- 51.43 deg, y = D.y - 1.5");
        check(nearf(r->b.child(kOptR).rot.y, -kHalfPi - (-kHalfPi), 1e-3f) && veq(r->k().aim[0], {0, 0, -1}) &&
              vnear(r->k().aim_s[0], {0, 0, -1}, 1e-4f) && vnear(r->k().fwd_s[1], {0, 0, -1}, 1e-4f),
              "CD1 §2.2 / C1: not behind in phase 1 -> both aim along F; the x0.9 low-pass; yaw = -pi/2 - atan2(aim.z, aim.x)");
        r->k().opt_hp[0] = 0;
        r->run(3);
        check(nearf(r->b.child(kOptR).alpha, 0.85f, 1e-5f) && r->b.child(kOptL).alpha == 1.0f,
              "CD1 §2.4: a destroyed option fades at 0.05 / tick and keeps orbiting");
    }
    {
        auto r = hold(2);
        r->run(2500);
        const Vec3 pr = r->b.child(kOptR).pos, pl = r->b.child(kOptL).pos;
        const float ang = 2.4639943f - kHalfPi;
        check(vnear(pr, {-16.5f * std::cos(ang), 2.5f, -16.5f * std::sin(ang)}, 2e-2f) && nearf(pl.x, -pr.x, 2e-2f) && nearf(pl.z, pr.z, 2e-2f) &&
              pr.z < 0 && vnear(r->k().aim[0], r->in.player_pos - pr, 0.05f),
              "CD1 §2.4 phase 2: radius 16.5 at 141.18 deg (rate 0.0099): in front, 38.8 deg to the side; the aim = P - opt.pos");
        check(r->b.child(kWing).alpha == 1.0f && r->b.statics().wing_n == 2500 * 6 && veq(r->b.child(kWing).rot, r->k().rot),
              "CD1 §2.5: the wing's alpha +0.01 / tick in phase 2; it copies the body's rotation; the counter += 6");
    }
    {
        auto r = hold(2);
        std::vector<int> g;
        for (int i = 0; i < 16; ++i) g.push_back(int(*r->step().glare));
        check(g[0] == 42 && g[15] == 84 && g[5] > 42 && g[5] < 84, "CD1 §2.5 / C4: glare byte 0x7b2e38 = 42 + 42 sin(n deg), n += 6 per tick");
        r->k().mode = kSuper;
        r->in.player_pos = {0, 495, -5};
        r->step();
        check(veq(r->b.child(kOptR).pos, {10, 495, -5}) && veq(r->b.child(kOptL).pos, {-10, 495, -5}) &&
              bits(r->b.child(kOptR).rot.y) == 0xc096cbe4u && bits(r->b.child(kOptL).rot.y) == 0xbfc90fdbu,
              "CD1 §2.4 / C2: in the super the options are parked at P.pos +/- 10 in x, yaw -4.712389 (R) / -1.5707964 (L)");
    }
    {   // the homing shot
        Rig r;
        r.park(2);
        r.k().shot = 1;
        r.in.player_pos = {3.7f, 1.2f, -14.5f};
        r.forced = {r15(1)};
        Boss07Output o = r.step();
        const Boss07Effect* e = fx(o, 440);
        const bool pick = r.draws == 1 && r.k().shot_opt == 1 && n_se(o, 388, true) == 1 && e && e->call == Boss07Call::kFx2 &&
                          e->parent == Boss07Parent::kOption && e->parent_index == 1 && e->at_mode == 2 &&
                          bits(e->at_scale.x) == 0x3f266666u && veq(e->pos, r.b.child(kOptL).pos) && r.k().shot == 2;
        std::vector<int> rings, se326;
        for (int c = 2; c <= 89; ++c) { o = r.step(); if (n_fx(o, 440)) rings.push_back(c); if (n_se(o, 326, true)) se326.push_back(c); }
        r.forced = {r15(4)};
        o = r.step();
        const Boss07Bullet* b = bul(o, 0x25, 54);
        check(pick && rings == std::vector<int>({31, 61}) && se326 == std::vector<int>({65}) && r.draws == 2 && b && r.k().shot == 0,
              "CD1 §4.1: c == 1 picks (1 draw when both live), SE2 388; rings FX2 440 at 1 / 31 / 61 (scale 0.65, mode 2); SE2 326 at 65; fire at 90");
        const Vec3 op = r.b.child(kOptL).pos;
        check(b && !b->raw && b->flag201 && veq(b->pos, {op.x, op.y + 2.25f, op.z}) && veq(b->dir, {0, 1, 0}) && b->f[0] == 0.8f &&
              bits(b->f[1]) == 0xb727c5acu && b->f[2] == 0 && b->f[3] == -5.0f && b->f[4] == 1.0f && bits(b->f[5]) == 53 &&
              bits(b->f[6]) == 2 && bits(b->f[7]) == 2 && bits(b->f[8]) == 0x3e97e9d8u && b->f[9] == 1.0f && b->f[10] == 2.0f &&
              b->f[11] == 60.0f && bits(b->f[12]) == 0x838571u,
              "CD1 §4.1: BUL kind 0x25 effect 54 straight up from +2.25: speed 0.8, accel -1e-5, power -5, trail 53 / 2 / 2, 17 deg, 1 / 2 / 60, packed aim");
        check(motion(o, kOptL, "N_DAMAGE") && r.b.child(kOptL).flash_2a4 == 1.65f,
              "CD1 §4.1: the firing option gets QueueMotion N_DAMAGE and +0x2a4 = 1.65");
        Rig h(setup(1, 1, true));
        h.park(2); h.k().shot = 90; h.k().shot_opt = 0;
        const Boss07Output oh = h.step();
        Rig one;
        one.park(2); one.k().shot = 1; one.k().opt_hp[0] = 0;
        one.step();
        Rig none;
        none.park(2); none.k().shot = 1; none.k().opt_hp[0] = none.k().opt_hp[1] = 0;
        const Boss07Output on = none.step();
        check(bul(oh, 0x25, 54) && bul(oh, 0x25, 54)->f[0] == 0.6f && bits(bul(oh, 0x25, 54)->f[1]) == 0xb58637bdu &&
              one.draws == 0 && one.k().shot_opt == 1 && none.k().shot == 0 && on.sounds.empty(),
              "CD1 §4.1: Easy speed 0.6, Hard accel -1e-6; one option alive -> no draw; both dead -> disarmed");
    }
}

static void test_camera() {
    {
        Rig r;
        r.k().drift_scale = 0; r.k().drift_timer = 99;
        r.in.player_pos = {0, 0, -2};
        Boss07Output o = r.step();
        const bool push = nearf(o.player.push.z, -0.19f, 1e-6f) && o.player.push.x == 0;
        r.in.player_pos = {0, 0, -14.5f};
        o = r.step();
        const Boss07Block& k = r.k();
        check(push && o.player.push.z == 0 && vnear(k.cam_eye, {0, 14, -50.5f}) && vnear(k.cam_look, {0, -0.55f, 2}) &&
              bits(k.cam_fov) == 0x3edf66f3u && k.cam_dist == 0,
              "CD1 §3.1: push-out max(0, (2.95 - d) 0.2) along boss -> player; eye 36 behind the player +14; look-at 2 past D at D.y + 0.1d - 6");
    }
    {
        Rig r;
        r.park(2);
        Boss07Output o = r.step();
        const bool alive = o.cam_slot && o.cam_slot->first == 4 && bits(o.cam_slot->second) == 0x3ccccccdu &&
                           nearf(r.b.statics().cam_d, 0.1f, 1e-7f) && nearf(r.b.statics().cam_h, 0.005f, 1e-7f) &&
                           nearf(r.k().cam_eye.y, 14.005f, 1e-4f);
        o = r.step();
        const bool once = !o.cam_slot;
        r.k().opt_hp[0] = r.k().opt_hp[1] = 0;
        o = r.step();
        check(alive && once && o.cam_slot && o.cam_slot->first == 3 && nearf(r.b.statics().cam_d, 0.1f, 1e-6f) &&
              nearf(r.b.statics().cam_h, 0.015f, 1e-6f) && r.k().cam_slot == 3,
              "CD1 §3.1 phase 2: an option alive -> slot 4, s4 -> 10 (+0.1), s0 -> 2 (+0.005); both dead -> slot 3, s4 -> 0, s0 -> 1");
        r.k().mode = kSuper; r.k().cam_slot = 5;
        o = r.step();
        check(r.b.statics().cam_d == 0 && r.b.statics().cam_h == 0 && r.k().cam_slot == 5,
              "CD1 §3.1: the super zeroes both statics and the slot is never changed while it is 5");
    }
    {
        Rig r;
        r.step();
        Boss07CamRecord rec; rec.eye_fixed = {9, 9, 9}; rec.target_fixed = {8, 8, 8};
        const bool k0 = r.b.camera_hook(0, rec) && veq(rec.eye_fixed, {9, 9, 9});
        const bool k1 = r.b.camera_hook(1, rec) && veq(rec.target_fixed, {0, 0, 0}) && rec.eye_mode == 3 && rec.eye_flags == 3 &&
                        rec.target_mode == 0 && veq(rec.eye_fixed, {9, 9, 9});
        r.k().cam_n = 7;
        const bool k3 = r.b.camera_hook(3, rec) && veq(rec.eye_fixed, r.k().cam_eye) && veq(rec.target_fixed, r.k().cam_look) &&
                        rec.eye_dist == 0 && bits(rec.fov) == 0x3edf66f3u && rec.eye_mode == 0 && r.k().cam_n == 0;
        check(k0 && k1 && k3 && !r.b.camera_hook(6, rec),
              "CD1 §3.2: slot 0 claimed untouched; slot 1 target (0,0,0), eye mode 3, flags 3; slots 2..4 fixed eye / target, fov 25 deg");
        const Vec3 E = r.k().cam_eye, L = r.k().cam_look;
        const bool ret = r.b.camera_hook(5, rec);
        const float t = 0.1f + 0.004f;
        const bool first = !ret && r.k().cam_n == 1 && r.k().cam_t == t && veq(rec.target_fixed, {L.x, 495, L.z}) &&
                           vnear(rec.eye_fixed, {L.x + (E.x - L.x) * t, 495 + 15 * t, L.z + (E.z - 20 - L.z) * t}, 1e-4f);
        r.b.camera_hook(5, rec);
        const bool second = nearf(r.k().cam_t, 0.108f, 1e-6f);
        for (int i = 0; i < 400; ++i) r.b.camera_hook(5, rec);
        check(first && second && nearf(r.k().cam_t, 1.004f, 1e-6f) && nearf(rec.eye_fixed.y, 495 + 15 * 1.004f, 1e-3f),
              "CD1 §3.2 slot 5: t = 0.1 then +0.004 a step (capped 1.0 + 0.004); eye lerps from the look-at; y = 495 + 15t; returns 0");
    }
}

static void test_hands() {
    auto shoot = [](const Boss07Setup& s, int variant, std::optional<std::uint32_t> fixed, int phase = 0) {
        auto r = std::make_unique<Rig>(s);
        r->k().phase = phase;
        r->fixed = fixed;
        r->attack(variant, 6);
        r->k().fc = 0;
        return r;
    };
    {   // N_SHOOT_R, pattern A
        auto r = shoot(setup(0), 0, std::nullopt);
        const auto v = r->run(152);
        int first = -1, last = -1, per_tick_ok = 1;
        for (int t = 0; t < 152; ++t) {
            const int n = int(v[std::size_t(t)].bullets.size());
            if (n) { if (first < 0) first = t; last = t; }
            per_tick_ok = per_tick_ok && n <= 1;
        }
        check(first == 60 && last == 116 && per_tick_ok && sum_bul(v, 2, 409) == 57 && first_tick(v, 311) == 4 && first_tick(v, 312) == 68 &&
              r->draws == 57 * 3 + 55 * 6 && r->b.statics().shoot_n == 2,
              "CD1 §5.2 / §5.45 pattern A: one kind-2 shot per tick for f in [566, 580] (57), 3 words each; SE 311 at +4, 312 at +68");
        check(sum_fx(v, 168) == 22 + 22 && sum_fx(v, 170) == 33 && n_fx(v[0], 168) == 1 && n_fx(v[1], 168) == 0 && n_fx(v[45], 168) == 1 &&
              n_fx(v[44], 170) == 1 && n_fx(v[108], 170) == 1 && n_fx(v[110], 170) == 0 && r->sec == kNShootR && r->cnt == 1,
              "CD1 §5.2 / C5: Spark(168) on even dc while f < 562, Spark(170) on even dc in [562, 578], FX 168 at the core every 3rd dc");
        const Boss07Effect* sp = fx(v[0], 168);
        check(sp && veq(sp->pos, {1, 4, 0}) && sp->scale.x >= 2.0f && sp->scale.x < 3.0f && sp->power == 0 && sp->stat && sp->side == 1,
              "CD1 §5.1: the charge sparkle sits on hand_r with scale u + 2.0 (6 words: 3 scale, 3 rot)");
    }
    {
        auto r = shoot(setup(0), 0, 0u);
        const auto v = r->run(152);
        const Boss07Bullet* b = bul(v[60], 2, 409);
        const float ang = -kHalfPi + (-80.0f) * kDeg;
        check(b && b->raw && b->flag201 && veq(b->pos, {0, 3.2f, 0}) && b->f[0] == 0.23f && b->f[1] == -0.00225f && b->f[3] == -10.0f &&
              b->f[4] == 1.0f && b->f[5] == 0 && b->f[8] == 0 && vnear(b->dir, {std::cos(ang), 0, std::sin(ang)}, 1e-5f),
              "CD1 §5.2 A (U = 0): BULraw kind 2 from the core - 0.8, ang = base - 80 deg, speed 0.23, accel -0.00225, power -10, knock 1");
        auto nm = shoot(setup(2), 0, 0u);
        const auto vn = nm->run(152);
        check(sum_bul(vn, 2, 398) == 11 && sum_bul(vn, 2, 409) == 46 && nm->draws == 57 * 4 + 55 * 6 && bul(vn[60], 2, 409)->f[1] == -0.003f &&
              n_bul(vn[63], 2, 398) == 1,
              "CD1 §5.2 Nightmare: every 5th shot (static counter == 4) is effect 398; pattern A draws 4 words, accel U 0.001 - 0.003");
    }
    {   // pattern B
        auto r = shoot(setup(0), 1, 0u);
        const auto v = r->run(152);
        const Boss07Bullet* b = bul(v[60], 0x27, 409);
        auto r2 = shoot(setup(0), 1, 0x20000u);
        const auto v2 = r2->run(152);
        check(sum_bul(v, 0x27, 409) == 57 && r->draws == 57 * 4 + 55 * 6 && b && b->raw && b->f[0] == 0.275f && b->f[1] == -0.00225f &&
              b->f[3] == -10.0f && b->dir.x < 0 && r2->draws == 57 * 5 + 55 * 6 && bul(v2[60], 0x27, 409)->dir.x > 0,
              "CD1 §5.2 / C6 pattern B: kind 0x27; U, Bit17 [, U], U speed, U accel = 4 or 5 words; the Bit17 lobe is the +40..60 deg side");
        auto nm = shoot(setup(2), 1, 0x20000u);
        const auto vn = nm->run(152);
        const Boss07Bullet* m = bul(vn[63], 0x27, 398);
        const float u = 0.015625f;
        check(nm->draws == 57 * 5 + 11 * 3 + 55 * 6 && m && nearf(m->f[0], 0.25f - u * 0.05f, 1e-6f) && nearf(m->f[1], u * 0.0005f - 0.00325f, 1e-7f),
              "CD1 §5.2: a Nightmare meteor in pattern B draws 3 more (Bit17, speed', accel'): 0.25 - U 0.05 / U 0.0005 - 0.00325");
        auto h = shoot(setup(1), 0, 0u, 1), n = shoot(setup(0), 0, 0u, 1), h0 = shoot(setup(1), 0, 0u, 0);
        h->run(61); n->run(61); h0->run(61);
        check(h->k().arm == 1 && n->k().arm == 0 && h0->k().arm == 0,
              "CD1 §5.1: Hard / Nightmare from phase 1 arm blk+0xcc = 1 on every tick of the [566, 580] window");
    }
    {   // N_SHOOT_L
        auto r = shoot(setup(0), 2, std::nullopt);
        const auto v = r->run(152);
        std::vector<int> ticks;
        for (int t = 0; t < 152; ++t) if (!v[std::size_t(t)].bullets.empty()) ticks.push_back(t);
        const Boss07Bullet* b = bul(v[56], 2, 398);
        check(ticks == std::vector<int>({56, 60, 64, 68, 72, 76, 80}) && r->draws == 152 + 7 + 26 * 6 && first_tick(v, 311) == 12 &&
              first_tick(v, 315) == 80 && sum_fx(v, 168) == 26,
              "CD1 §5.3 / C7: turnA drawn EVERY tick; a meteor every 4th dc for f in [525, 531] (7); Spark(168, hand_l) on even dc, f < 524");
        check(b && b->raw && bits(b->f[0]) == 0x3d343958u && bits(b->f[1]) == 0x3b7f9724u && b->f[3] == -3.0f && b->f[4] == 1.0f &&
              bits(b->f[5]) == 399 && bits(b->f[6]) == 10 && bits(b->f[7]) == 1 && b->f[8] >= 5 * kDeg && b->f[8] < 7 * kDeg &&
              b->f[9] == 1.0f && b->f[10] >= 10 && b->f[10] < 16 && b->f[11] == 60.0f && veq(b->pos, {-1, 4, 0}) && vnear(b->dir, {0, 0, -1}),
              "CD1 §5.3 A: kind 2 effect 398 along the left hand: 0.044, accel 0.0039, power -3, trail 399 / 10, homing 1, 5..7 deg, ages 10..16 / 60");
        auto rb = shoot(setup(0), 3, std::nullopt);
        const auto vb = rb->run(152);
        const Boss07Bullet* c = bul(vb[56], 0x26, 398);
        check(sum_bul(vb, 0x26, 398) == 7 && c && bits(c->f[0]) == 0x3d3851ecu && c->f[1] == 0.004f && c->f[3] == -2.0f &&
              bits(c->f[5]) == 399 && bits(c->f[6]) == 10 && bits(c->f[7]) == 1 && c->f[8] >= 6 * kDeg && c->f[8] < 8 * kDeg &&
              c->f[9] == 1.0f && c->f[10] == 40.0f && c->f[11] == 999.0f && c->f[12] == 0,
              "CD1 §5.3 B: kind 0x26: speed 0.045, accel 0.004, power -2, max turn 6..8 deg, (1, 40, 999, 0)");
        auto nm = shoot(setup(2), 3, std::nullopt);
        const auto vn = nm->run(152);
        check(sum_bul(vn, 0x26, 398) == 10 && nm->draws == 152 * 2 + 10 + 26 * 6 && bul(vn[56], 0x26, 398)->f[1] == 0.006f,
              "CD1 §5.3 Nightmare: two words every tick, the window runs to f = 534 (10 meteors), accB 0.006");
    }
    {   // N_IMPACT_R: ice
        auto r = shoot(setup(0), 5, std::nullopt);
        const auto v = r->run(152);
        const Boss07Effect* a = fx(v[116], 405); const Boss07Effect* c = fx(v[116], 406);
        check(r->draws == 0 && sum_fx(v, 286) == 116 && n_fx(v[115], 286) == 1 && n_fx(v[116], 286) == 0 && first_tick(v, 311) == 20 &&
              first_tick(v, 314) == 116 && sum_fx(v, 405) == 1 && a && c && veq(a->pos, {0, 0, -14.5f}) && a->power == -4.0f &&
              veq(c->pos, {0, -1, -14.5f}) && c->power == -8.0f && a->call == Boss07Call::kFx,
              "CD1 §5.4 / C8 ice: FX 286 at hand_r every tick f < 620; at 620 FX 405 (power -4) at (P.x, 0, P.z), 406 (power -8) at y -1; no RNG");
        check(v[124].shake && v[124].shake->first == 45 && veq(v[124].shake->second, {0.25f, 0.25f, 0}) && has(v[124].rumble, 3) && !v[123].shake,
              "CD1 §5.4 / C10: f == 622 -> Shake(45, 0.25, 0.25, 0) + Rumble(3)");
        auto nm = shoot(setup(2), 5, r15(600));
        auto vn = nm->run(130);
        nm->in.player_pos = {5, 0, -3};
        const auto vn2 = nm->run(50);
        vn.insert(vn.end(), vn2.begin(), vn2.end());
        std::vector<int> pil;
        for (int t = 0; t < 180; ++t) if (n_fx(vn[std::size_t(t)], 405)) pil.push_back(t);
        check(pil == std::vector<int>({116, 134, 154}) && veq(fx(vn[134], 405)->pos, {5, 0, -3}) && nm->k().echo == 0 &&
              veq(fx(vn[154], 406)->pos, {5, -1, -3}),
              "CD1 §5.4 Nightmare echo: blk+0x2b0 counts from the same tick -> pillars again +18 and +38 under the player's CURRENT position");
    }
    {   // N_IMPACT_R: fire
        auto r = shoot(setup(0), 4, kU50);
        const auto v = r->run(152);
        const float at = std::atan2(-14.5f, -1.0f);
        bool fan = sum_fx(v, 407) == 7 && n_fx(v[116], 407) == 7;
        for (int i = 0; fan && i < 7; ++i) {
            const Boss07Effect* e = fx(v[116], 407, i);
            fan = veq(e->pos, {1, 0.75f, 0}) && nearf(e->rot.y, -kHalfPi - (at + float(i) * 25 * kDeg - 75 * kDeg), 1e-5f) && e->power == 0;
        }
        check(fan && r->draws == 1 && first_tick(v, 317) == 20 && first_tick(v, 318) == 116 && sum_fx(v, 133) == 116,
              "CD1 §5.4 / C9 fire: 7 FX 407 25 deg apart about the hand -> player bearing from (hand.x, 0.75, hand.z); one word (jitter)");
        auto nm = shoot(setup(2), 4, kU50);
        const auto vn = nm->run(152);
        check(n_fx(vn[116], 407) == 11 && nearf(fx(vn[116], 407, 1)->rot.y - fx(vn[116], 407, 0)->rot.y, -16 * kDeg, 1e-5f) &&
              nearf(fx(vn[116], 407, 5)->rot.y, -kHalfPi - at, 1e-5f),
              "CD1 §5.4 Nightmare: 11 waves 16 deg apart");
    }
}

static void test_capture() {
    auto spawn = [](const Boss07Setup& s, const Vec3& p) {
        auto r = std::make_unique<Rig>(s);
        r->park(2);
        r->k().drift_scale = 0; r->k().drift_timer = 999999;
        r->in.player_pos = p; r->in.cam_yaw = 0.7f;
        r->clip(kSisLazer);
        return r;
    };
    {
        auto r = spawn(setup(0), {2, 0, -10});
        const Boss07Output o = r->step();
        const Boss07Effect* a = fx(o, 459, 0); const Boss07Effect* b = fx(o, 459, 1); const Boss07Effect* c = fx(o, 333);
        check(n_se(o, 330) == 1 && n_fx(o, 459) == 2 && a && b && a->parent == Boss07Parent::kNode && a->parent_index == kHandR &&
              b->parent_index == kHandL && a->at_mode == 2 && veq(a->at_off, {0, -7.25f, 0}) && veq(a->at_scale, {1, 0.675f, 1}) &&
              veq(a->pos, {0, 0, 0}) && c && veq(c->pos, {2, 0.75f, -10}) && veq(r->k().circle, {2, 0.75f, -10}) && r->k().circle_timer == 349,
              "D23 §2 / R1 / R2: SIS_LAZER f == 790 — SE 330, FX 459 under each hand (mode 2, off -y - 3.25, scale (1, .675, 1)); FX 333 AT THE PLAYER");
        const Boss07Output o2 = r->step();
        check(n_fx(o2, 333) == 0 && r->k().circle_timer == 348, "D23 §2: the event fires on one tick; blk+0x1f8 = 350 counts down from the spawn frame");
    }
    auto capture_j = [&](const Boss07Setup& s) {
        auto r = spawn(s, {2, 0, -10});
        int j = 0;
        for (; j < 400; ++j) { const Boss07Output o = r->step(); if (flag(o, 134, 1)) break; }
        return j;
    };
    check(capture_j(setup(0)) == 120 && capture_j(setup(1)) == 110 && capture_j(setup(2)) == 100 && capture_j(setup(0, 1, true)) == 120,
          "D23 §3 [s16 0x68bbe0]: the circle is live from j = 120 / 110 / 100 frames after the spawn (Easy uses flag144's row)");
    {
        auto suction = [&](int chr, int diff, bool easy) {
            auto r = spawn(setup(diff, chr, easy), {2, 0, -10});
            r->step();
            r->in.player_pos = {12, 0, -10};
            const int delay = kCircleDelay[diff];
            Boss07Output o;
            bool early = true;
            for (int j = 1; j <= delay; ++j) { o = r->step(); if (j < delay) early = early && o.player.suction.x == 0; }
            return early && o.player.suction.z == 0 && !flag(o, 134, 1) ? o.player.suction.x : 99.0f;
        };
        const float k[4][3] = {{0.00375f, 0.00415f, 0.00455f}, {0.00375f, 0.00415f, 0.00455f}, {0.00325f, 0.00345f, 0.00365f},
                               {0.00475f, 0.00575f, 0.00675f}};
        bool ok = true;
        for (int c = 0; c < 4; ++c) for (int d = 0; d < 3; ++d) ok = ok && nearf(suction(c, d, false), -15.0f * k[c][d], 2e-7f);
        check(ok && nearf(suction(1, 0, true), -15.0f * 0.00375f * 0.5f, 2e-7f) && nearf(suction(3, 0, true), -15.0f * 0.00475f * 0.5f, 2e-7f),
              "D23 §3 / R3: suction on knock channel 2 toward the centre, 15 k per frame, k by character and difficulty (12 cells); Easy x0.5");
        auto r = spawn(setup(0), {2, 0, -10});
        r->step();
        r->in.player_section = 5;
        r->run(10);
        const bool frozen = r->k().circle_timer == 349;
        r->in.player_section = 0; r->in.player_pos = {12, 0, -10}; r->in.player_hp = 0.5f;
        r->run(130);
        const Boss07Output o = r->step();
        check(frozen && o.player.suction.x == 0 && r->k().circle_timer == 349 - 131,
              "D23 §3: the player's section 5 freezes the timer; an actor with HP < 1 is not pulled");
    }
    {   // the capture, the countdown, the entry
        auto r = spawn(setup(0, 2), {2, 0, -10});
        r->k().rock_timer = 500; r->b.mini(0).timer = 255;
        Boss07Output o = r->step();
        r->in.player_pos = {5, 0, -10};
        for (int j = 1; j <= 120; ++j) o = r->step();
        const Boss07Effect* g = fx(o, 522);
        check(flag(o, 134, 1) && r->k().capture == 89 && o.player.iframes && *o.player.iframes == 91 && has(o.player.sections, 2) && g &&
              g->side == 0 && !g->stat && g->parent == Boss07Parent::kPlayer && g->at_mode == 2 && veq(g->at_rot, {0, 0.7f, 0}) &&
              nearf(o.player.suction.x, -15.0f * 0.00325f, 2e-7f),
              "D23 §3 / R4: inside 7 units -> blk+0x1fc = 90 (89 after the frame), i-frames >= 91, flag 134, PlayerSection(2), FX 522 on the player (side 0, cam yaw); the suction still runs that frame");
        r->in.player_pos = {40, 0, 40};                            // nothing cancels it
        std::vector<Boss07Output> v;
        for (int i = 1; i <= 89; ++i) { if (i == 89) { r->k().arm = 3; r->k().shot = 5; } v.push_back(r->step()); }
        bool no_pull = true;
        for (const auto& x : v) no_pull = no_pull && x.player.suction.x == 0 && x.player.suction.z == 0;
        const Boss07Output& fl = v[58]; const Boss07Output& en = v[88];
        check(no_pull && fl.fader.size() == 1 && *fl.fader[0].colour == 0xffffffu && bits(*fl.fader[0].step) == 0x3c888889u &&
              *fl.fader[0].target == 1.0f && n_se(fl, 351) == 1 && first_tick(v, 351) == 58 && r->k().mode == kSuper && v[87].clips.empty(),
              "D23 §4 / §7: a fixed 90-frame countdown — white fader 1/60 + SE 351 at c0 + 59, the entry at c0 + 89; no pull meanwhile");
        check(flag(en, 550, 0) && flag(en, 211, 1) && en.player.kill_7e0 && en.player.boost_tick && en.fader.size() == 1 &&
              *en.fader[0].step == 1.0f && *en.fader[0].target == 1.0f && n_clip(en, kKiaiStart) == 1 && en.cam_slot &&
              en.cam_slot->first == 5 && en.cam_slot->second == 1.0f && en.particles_wipe && en.skybox_alpha && *en.skybox_alpha == 0,
              "D23 §4: flag 550 = 0, Kill(P+0x7e0), the forced Boost tick, full white, KIAI_START, blk+0xa4 = 4, CamSlot(5, 1.0), particle wipe, skybox 0, flag 211");
        const Boss07Effect* iz = fx(en, 337);
        check(iz && iz->handle != 0 && iz->handle == r->k().h_izigen && veq(iz->pos, {0, 495, -5}) && n_fx(en, 11) == 1 &&
              has(en.player.sections, 318) && r->k().drift_timer == 360 && r->k().turn_scale == 30.0f && r->k().turn_timer == 10 &&
              r->k().arm == 0 && r->k().rock_timer == 0 && r->k().shot == 0 && r->b.mini(0).timer == 0 && !en.player.pos,
              "D23 §4: FX 337 (handle blk+0x200) + FX 11 at (0, 495, -5); section 318; drift 0 / 360, turn 30 / 10; +0xcc = +0x204 = +0x180 = 0; nobody moved yet");
        auto adol = spawn(setup(0, 0), {2, 0, -10});
        adol->k().capture = 1;
        check(has(adol->step().player.sections, 2), "D23 §4: Adol (flag150 == 0) gets section 2 instead of 318");
        // ---- the super, E = the entry frame
        std::vector<Boss07Output> s;
        int ret = -1;
        Vec3 pin_body{};
        for (int i = 1; i <= 700 && ret < 0; ++i) {
            s.push_back(r->step());
            if (i == 1) pin_body = r->b.body().pos;
            if (flag(s.back(), 211, 0)) ret = i;
        }
        auto at = [&](int i) -> const Boss07Output& { return s[std::size_t(i - 1)]; };
        check(at(1).player.pos && veq(*at(1).player.pos, {0, 495, -5}) && at(1).player.flyer && *at(1).player.flyer &&
              veq(pin_body, {0, 497, 20}) && at(5).player.pos && sum_fx(s, 33) > 150,
              "D23 §6.1 / §6.2: every super frame pins D = (0, 497, 20), P = (0, 495, -5), P+0x398 = 1; FX 33 on the player every 3rd frame");
        const Boss07Effect* amb = nullptr;
        for (const auto& x : s) if (!amb) amb = fx(x, 33);
        const Boss07Effect* tame = fx(at(28), 338);
        check(amb && amb->call == Boss07Call::kInline && amb->side == 0 && !amb->stat && amb->parent == Boss07Parent::kPlayer &&
              amb->at_mode == 2 && amb->or_1dc == 0x100 && n_se(at(28), 352) == 1 && tame && tame->handle != 0 &&
              tame->parent == Boss07Parent::kBody && tame->at_mode == 2 && has(at(28).rumble, 4) && sum_se(s, 328) == 0,
              "D23 §5 / §7: E + 28 (f == 678): SE 352 (super), FX 338 on the body (mode 2, handle blk+0x1e4), Rumble(4)");
        int t531 = -1, tmet = -1, tkill = -1;
        for (int i = 1; i <= int(s.size()); ++i) {
            if (n_se(at(i), 531, true)) t531 = i;
            if (n_fx(at(i), 339)) tmet = i;
            if (tmet > 0 && tkill < 0 && at(i).kill_handles.size() == 1 && i > tmet) tkill = i;
        }
        const bool fade = t531 == 240 && tmet > 0;
        const Boss07Output& m = at(tmet);
        const Boss07Effect* beam = fx(m, 339); const Boss07Effect* sk = fx(m, 334);
        check(fade && n_se(m, 353) == 1 && n_se(m, 575) == 1 && has(m.rumble, 5) && beam && beam->parent == Boss07Parent::kBody &&
              beam->at_mode == 1 && beam->stat && sk && sk->parent == Boss07Parent::kPlayer && sk->at_mode == 2 && sk->handle != 0 &&
              m.zoom && m.zoom->first == 0.5f && bits(m.zoom->second) == 0xbb03126fu && has(m.kill_handles, tame->handle) &&
              motion(m, kWing, "METEOR_LOOP") && tmet > 350 && tmet < 380,
              "D23 §5 / R6 / §6.4: SE2 531 at E + 240; METEOR f == 730: SE 353 + 575, Rumble(5), FX 339 on the body (mode 1), FX 334 on the player, ZoomBlur(0.5, -0.002), FX 338 killed");
        const Boss07Output& rt = at(ret);
        check(tkill == ret - 4 && has(at(tkill).kill_handles, sk->handle) && ret > 540 && ret < 580 && has(rt.kill_handles, iz->handle) &&
              has(rt.player.sections, 9) && flag(rt, 134, 0) && r->k().mode == 0 && veq(r->b.body().pos, {0, 4, 0}) &&
              r->b.child(kOptR).alpha == 0.0f,
              "D23 §5 / §6.3 / §7: METEOR_FROM_WAIT f == 778 kills FX 334; the return (~E + 572) kills FX 337, section 9, flags clear; options faded out");
    }
    {   // the normal KIAI -> METEOR
        Rig r;
        r.attack(8, 8);
        const auto v = r.run(260);
        int tmet = -1;
        for (int i = 0; i < 260; ++i) if (n_fx(v[std::size_t(i)], 401)) tmet = i;
        check(first_tick(v, 328) == 28 && sum_se(v, 352) == 0 && tmet == 32 + 161 + 16 && n_se(v[std::size_t(tmet)], 329) == 1 &&
              sum_fx(v, 339) == 0 && sum_fx(v, 334) == 0 && v[std::size_t(tmet)].zoom && fx(v[std::size_t(tmet)], 401)->at_mode == 1 &&
              v[std::size_t(tmet)].kill_handles.size() == 1,
              "D23 §5 / R6 / R7: the normal KIAI: SE 328, KIAI_LOOP once, METEOR: SE 329, FX 401 on the body (mode 1), ZoomBlur in both cases");
    }
}

static void test_barrier_events() {
    auto raise = [](int next, const Boss07Setup& s = Boss07Setup{}) {
        auto r = std::make_unique<Rig>(s);
        r->k().phase = 1; r->k().cooldown = 0; r->k().next_barrier = next; r->k().fc = 1;
        r->forced = {r15(0)};
        r->fixed = r15(600);
        return r;
    };
    {   // W
        auto r = raise(0);
        const auto v = r->run(330);
        int back = -1;
        for (int t = 1; t < 330 && back < 0; ++t) if (n_clip(v[std::size_t(t)], kNWait)) back = t;
        const Boss07Effect* e = fx(v[152], 402, 1);
        check(n_clip(v[0], kN2W) == 1 && first_tick(v, 319) == 4 && first_tick(v, 321) == 72 && sum_fx(v, 402) == 2 && e &&
              e->parent == Boss07Parent::kOption && e->parent_index == 1 && veq(e->at_off, {0, 0.75f, -6}) && e->at_mode == 1 && e->flag201 &&
              e->stat && veq(e->pos, {0, 0, 0}),
              "D23 §9: N2W_CHANGE f == 361 -> SE 319, blk+0xd0 = 1; 378 -> SE 321; 398 -> FX 402 on each live option (off (0, .75, -6), mode 1, +0x201)");
        check(first_tick(v, 322) == 156 && back == 317 && r->k().mode == 0 && r->k().barrier == kBarW,
              "D23 §9 / §12 (code order): blk+0x2a8 = 5 is counted from the event frame -> SE 322 at +4; AB §A.4: the raise takes 156 + 161 ticks");
        check(n_fx(v[15], 341) == 1 && fx(v[15], 341)->parent == Boss07Parent::kNode && fx(v[15], 341)->at_mode == 1 &&
              veq(fx(v[15], 341)->rot, {0, 0, 0}) && n_fx(v[16], 341) == 0 && r->draws >= 1,
              "D23 §15: aura W = FX 341 at the core (attached, mode 1) every 16th frame, no draw");
    }
    {   // F
        auto r = raise(1);
        std::vector<int> ticks, dr;
        std::vector<Boss07Output> v;
        for (int t = 0; t < 160; ++t) {
            const int d0 = r->draws;
            v.push_back(r->step());
            if (!v.back().bullets.empty()) { ticks.push_back(t); dr.push_back(r->draws - d0); }
        }
        const Boss07Bullet* b = bul(v[72], 2, 403, 1);
        const Vec3 op = r->b.child(kOptL).pos;
        check(n_clip(v[0], kN2F) == 1 && first_tick(v, 319) == 4 && r->k().barrier == kBarF && first_tick(v, 324) == 72 &&
              ticks == std::vector<int>({72, 112, 152}) && dr == std::vector<int>({10, 10, 10}) && sum_bul(v, 2, 403) == 6,
              "D23 §10: N2F f == 411 -> barrier F; the F attack at 428 / 438 / 448, 5 RNG words per live option");
        check(b && b->raw && b->flag201 && veq(b->dir, {0, 1, 0}) && nearf(b->pos.x, op.x, 0.05f) && b->f[0] >= 0.1f && b->f[0] < 0.15f &&
              b->f[1] >= 1e-4f && b->f[1] < 2e-4f && b->f[3] == 0 && b->f[4] == 1.0f && bits(b->f[5]) == 399 && bits(b->f[6]) == 8 &&
              bits(b->f[7]) == 1 && b->f[8] >= 1 * kDeg && b->f[8] < 3 * kDeg && b->f[9] == 0 && b->f[10] >= 30 && b->f[10] < 70 &&
              b->f[11] >= 250 && b->f[11] < 320,
              "D23 §10 / R9: BULraw kind 2 effect 403 straight up from +1.5: speed .1..15, accel 1e-4..2e-4, trail 399 / 8, homing 1, 1..3 deg, ages 30..70 / 250..320");
        Rig q;
        q.park(2); q.k().q_f = true; q.k().opt_hp[0] = 0;
        q.forced = {kU75, kU50, kU25, 0u, kU50};
        const Boss07Output o = q.step();
        const Boss07Bullet* qb = bul(o, 2, 403);
        check(o.bullets.size() == 1 && q.draws == 5 && !q.k().q_f && n_se(o, 319) == 0 && q.k().barrier == 0 && qb &&
              nearf(qb->f[11], 0.75f * 70 + 250, 1e-4f) && nearf(qb->f[10], 0.5f * 40 + 30, 1e-4f) && nearf(qb->f[8], (0.25f * 2 + 1) * kDeg, 1e-6f) &&
              qb->f[1] == 1e-4f && nearf(qb->f[0], 0.5f * 0.05f + 0.1f, 1e-6f) && q.b.child(kOptR).flash_2a4 == 1.65f,
              "D23 §10: a queued F (blk+0xc4) fires once, at once, per LIVE option; u1..u5 -> f11, f10, f8, f1, f0; both options flash");
    }
    {   // E and the rock bullets
        auto r = raise(2);
        r->b.child(kOptR).pos = {-9, 2.5f, 7}; r->b.child(kOptL).pos = {9, 2.5f, 7};
        r->k().drift_scale = 0; r->k().drift_timer = 99999;
        std::vector<Boss07Output> v;
        int rock = -1;
        for (int t = 0; t < 260; ++t) { v.push_back(r->step()); if (rock < 0 && n_bul(v.back(), 4, 400)) rock = t; }
        const Boss07Effect* e = fx(v[152], 335, 0);
        check(n_clip(v[0], kN2G) == 1 && first_tick(v, 319) == 4 && first_tick(v, 321) == 72 && sum_fx(v, 335) == 2 && e && e->flag201 &&
              e->parent == Boss07Parent::kOption && e->at_mode == 1 && veq(e->at_off, {0, 0.75f, -6}) && first_tick(v, 322) == 156,
              "D23 §11.1: N2G f == 461 -> barrier E (blk+0xd0 = 2), 478 -> SE 321, 498 -> FX 335 on each live option, blk+0x204 = 80");
        const Boss07Bullet* b = rock >= 0 ? bul(v[std::size_t(rock)], 4, 400, 0) : nullptr;
        check(rock == 152 + 79 && sum_bul(v, 4, 400) == 2 && b && !b->raw && b->flag201 && bits(b->f[0]) == 0x3e19999au && b->f[1] == 0 &&
              b->f[3] == 0 && b->f[4] == 1.0f && bits(b->f[5]) == 396 && bits(b->f[6]) == 30 && bits(b->f[7]) == 201 &&
              bits(b->f[8]) == 0x40c90fdbu && b->f[11] == 600.0f && b->dir.y == 0 && nearf(b->dir.x * b->dir.x + b->dir.z * b->dir.z, 1.0f, 1e-5f),
              "D23 §11.2 / R10 (code order: the timer counts from the event frame -> +79): BUL kind 4 effect 400, 0.15, trail 396 / 30, homing 201, 2 pi, 600");
        const Vec3 c = r->in.opt_core[0];
        check(b && nearf(b->pos.y, c.y + 2.5f, 0.05f) && b->dir.x > 0 && b->dir.z < 0,
              "D23 §11.2: from the option's core + 4.5 norm(core - hontai) + (0, 2.5, 0), aimed horizontally at the player");
    }
    {   // the aura's draws
        auto aura = [](int barrier, int phase) {
            Rig r;
            r.k().phase = phase; r.k().barrier = barrier; r.k().cooldown = 9999; r.k().shot_timer = 9999; r.k().fc = 16;
            r.forced = {kU25, kU50, kU75};
            const Boss07Output o = r.step();
            static const int ids[5] = {0, 341, 395, 340, 10};
            const Boss07Effect* e = fx(o, ids[barrier]);
            return e ? std::make_pair(r.draws, e->rot) : std::make_pair(-1, Vec3{});
        };
        const Vec3 want{90 * kDeg, 180 * kDeg, 270 * kDeg};
        check(aura(1, 1).first == 0 && aura(2, 1).first == 3 && vnear(aura(2, 1).second, want, 1e-5f) && aura(3, 1).first == 3 &&
              aura(4, 2).first == 3 && vnear(aura(4, 2).second, want, 1e-5f),
              "D23 §15: FX 341 / 395 / 340 / 10 by blk+0xd0; 3 Rf(0, 360) for E / F / D (x, y, z in draw order), none for W");
    }
    auto opts = [](const Boss07Setup& s = Boss07Setup{}) {
        Boss07Setup t = s; t.statics.opt_alpha[0] = t.statics.opt_alpha[1] = 1.0f;
        auto r = std::make_unique<Rig>(t);
        r->k().phase = 1; r->k().barrier = 1; r->k().cooldown = 9999;
        r->k().drift_scale = 0; r->k().drift_timer = 99999;
        r->b.child(kOptR).pos = {-9, 2.5f, 7}; r->b.child(kOptL).pos = {9, 2.5f, 7};
        r->fixed = 0u;
        return r;
    };
    {   // the volley
        auto r = opts();
        r->attack(0, 7); r->k().fc = 0;
        std::vector<Boss07Output> v;
        Vec3 core70{};
        for (int t = 0; t < 152; ++t) { v.push_back(r->step()); if (t == 70) core70 = r->in.opt_core[0]; }
        std::vector<int> ticks;
        for (int t = 0; t < 152; ++t) if (n_bul(v[std::size_t(t)], 4, 408)) ticks.push_back(t);
        const Boss07Output& o = v[70];
        check(ticks.size() == 10 && ticks.front() == 70 && ticks.back() == 115 && sum_bul(v, 4, 408) == 60 && sum_se(v, 313) == 1 &&
              n_se(o, 313) == 1 && r->k().volley_latch == 0 && r->b.child(kOptR).flash_2a4 == 1.65f,
              "D23 §13: both option alphas == 1.0; f in [568, 580], every 5th frame, 3 kind-4 bullets (408) per live option; SE 313 latched");
        const Boss07Bullet* b0 = bul(o, 4, 408, 0); const Boss07Bullet* b1 = bul(o, 4, 408, 1); const Boss07Bullet* b2 = bul(o, 4, 408, 2);
        const Boss07Bullet* l0 = bul(o, 4, 408, 3); const Boss07Bullet* l2 = bul(o, 4, 408, 5);
        check(b0 && l2 && !b0->raw && b0->flag201 && bits(b0->f[0]) == 0x3ecccccdu && b0->f[3] == -10.0f && b0->f[4] == 1.0f && b0->f[7] == 0 &&
              nearf(b0->pos.x, core70.x, 1e-4f) && nearf(b0->pos.z - core70.z, 1.25f, 1e-4f) && nearf(b0->pos.y, core70.y - 0.8f, 1e-4f) &&
              vnear(b0->dir, {0.38268f, 0, -0.92388f}, 1e-4f) && vnear(b1->dir, {0, 0, -1}, 1e-5f) && vnear(b2->dir, {0.70711f, 0, -0.70711f}, 1e-4f) &&
              vnear(l0->dir, {-0.38268f, 0, -0.92388f}, 1e-4f) && vnear(l2->dir, {-0.70711f, 0, -0.70711f}, 1e-4f),
              "D23 §13 / R12: speed 0.4, power -10, no homing, from core - 1.25 n - (0, .8, 0); R fans +22.5 / +45 deg, L -22.5 / -45, plus straight");
        auto q = opts();
        q->attack(0, 7); q->k().fc = 4;
        const auto vq = q->run(152);
        check(sum_se(vq, 313) == 2 && q->k().volley_latch == 1 && n_bul(vq[116], 4, 408) == 6,
              "D23 §13: the latch is cleared on the f == 580 tick BEFORE the fire test — a fire tick there replays SE 313 and leaves it set");
        Rig p0;
        p0.attack(0, 6); p0.k().fc = 0;
        check(sum_bul(p0.run(152), 4, 408) == 0, "D23 §13: no volley in phase 0 (option alphas are 0)");
    }
    {   // the barrage
        auto r = opts();
        r->in.player_pos = {0, 0, 14.5f};
        r->fixed.reset();
        r->k().fc = 12;
        const int d0 = r->draws;
        const Boss07Output o = r->step();
        bool slow = o.bullets.size() == 6;
        for (const auto& b : o.bullets) slow = slow && b.kind == 4 && b.effect == 408 && b.f[0] == 0.2f && b.f[3] == -10.0f && b.flag201;
        const Boss07Output o2 = r->step();
        check(r->k().behind && slow && n_se(o, 355) == 2 && r->draws - d0 == 12 && o2.bullets.empty() && !r->k().aimed,
              "D23 §14 / R11: player behind -> every 12th frame SE 355 + 3 bullets per live option; scattered (blk+0x164 == 0): 6 Rf each, speed 0.2");
        auto a = opts();
        a->in.player_pos = {0, 0, 14.5f};
        a->attack(4, 7); a->k().fc = 12;
        const int d1 = a->draws;
        const Boss07Output oa = a->step();
        check(oa.bullets.size() == 6 && a->draws == d1 && a->k().aimed && oa.bullets[0].f[0] == 0.4f &&
              vnear(oa.bullets[0].dir, {-0.38268f, 0, -0.92388f}, 1e-4f) && vnear(oa.bullets[2].dir, {0.38268f, 0, -0.92388f}, 1e-4f) &&
              vnear(oa.bullets[3].dir, {0.38268f, 0, -0.92388f}, 1e-4f),
              "D23 §14: inside the fire-wave window (N_IMPACT f in [591, 629], blk+0xb4 != 0) the barrage is the aimed +/-22.5 deg fan at 0.4, no draw");
        auto h = opts(setup(1));
        h->k().arm = 2; h->k().fc = 13;
        h->step();
        check(h->k().arm == 1 && !h->k().behind, "D23 §14: a pending blk+0xcc (armed by the N_SHOOT windows) is counted down a frame at a time");
    }
}

static void test_death() {
    auto dying = [](bool rush) {
        Boss07Setup s; s.boss_rush = rush;
        auto r = std::make_unique<Rig>(s);
        r->park(2);
        r->b.body().hp = 10;
        r->b.hit_part(0, 100);
        return r;
    };
    {
        auto r = dying(false);
        const int d0 = r->draws;
        Boss07Output o = r->step();
        const int fired = n_fx(o, 95) + int(o.sprites.size());
        check(flag(o, 194, 0) && flag(o, 226, 1) && flag(o, 529, 0) && flag(o, 532, 0) && o.exp && n_clip(o, kMeteor) == 1 &&
              r->k().death_started && !o.player.iframes && !flag(o, 156, 0),
              "D23 §8 / R14: the first death frame — flag 194 = 0, flags 529..532 = 0, flag 226 = 1, EXP, SetClip(METEOR), the latch");
        check(n_se(o, 186, true) == 1 && n_se(o, 575, true) == 1 && n_se(o, 187, true) == 1 && has(o.rumble, 5) &&
              bits(r->b.body().alpha_step) == 0x3ada740eu && r->b.body().alpha_target == 0 && bits(r->b.child(kWing).alpha_step) == 0x3bb60b61u &&
              bits(r->b.child(kOptR).alpha_step) == 0x3bb60b61u && r->k().death_fc == 1 && r->k().fc == 1,
              "D23 §8: k == 0: SE2 186 + 575 + 187, Rumble(5); alpha fades 1/600 (body) and 1/180 (children); blk+0x2a0++");
        check(r->draws - d0 == 4 * kBodyNodes + 4 * fired + int(o.sprites.size()),
              "D23 §8 / §21: 4 draws per node (171) every debris frame, +4 per firing node, +1 on its sprite path");
        o = r->step();
        const Boss07Effect* on = fx(o, 97);
        check(!o.exp && !flag(o, 226, 1) && o.clips.empty() && n_se(o, 576, true) == 1 && on && on->call == Boss07Call::kFx2 &&
              veq(on->scale, {1.25f, 1.25f, 1.25f}) && on->rtype == -1 && veq(on->pos, r->b.body().pos) && n_se(o, 186, true) == 0,
              "D23 §8: EXP once (living player); k == 1: SE2 576 + FX2 97 (scale 1.25, rtype -1) at D.pos");
        int exps = 0, f544 = 0, first_rec = -1, destroy_k = -1, yotyou = n_fx(o, 95), sprites = int(o.sprites.size()), bad_node = 0;
        std::vector<int> rumbles;
        Boss07Output k120;
        for (int k = 2; k < 800 && destroy_k < 0; ++k) {
            r->b.body().alpha = std::max(0.0f, r->b.body().alpha - 1.0f / 600.0f);       // the host's alpha stepper
            const int dd = r->draws;
            o = r->step();
            exps += o.exp; f544 += flag(o, 544, 0);
            if (o.kill_record && first_rec < 0) first_rec = r->b.body().alpha <= 0.2f ? k : -2;
            if (has(o.rumble, 5)) rumbles.push_back(k);
            if (k == 120) k120 = o;
            yotyou += n_fx(o, 95); sprites += int(o.sprites.size());
            for (const auto& e : o.effects) if (e.id == 95) bad_node += e.rtype != -1 || e.scale.x < 0.33f - 1e-4f || e.scale.x > 1.1f * 0.8f;
            if (o.destroy) { destroy_k = k; bad_node += r->draws != dd || !o.effects.empty(); }
        }
        int latched = 0;
        for (std::uint8_t b : r->k().debris) latched += b;
        check(n_se(k120, 577, true) == 1 && n_se(k120, 586, true) == 1 && n_fx(k120, 96) == 1 && fx(k120, 96)->rtype == -1 &&
              rumbles.size() >= 9 && rumbles[0] == 60 && rumbles[1] == 120,
              "D23 §8: k == 120: SE2 577 + 586 + FX2 96; Rumble(5) every 60th k");
        check(exps == 0 && f544 == 1 && first_rec > 60 && destroy_k > 590 && destroy_k < 610 && r->b.body().removed && r->b.child(kWing).removed &&
              r->b.child(kWing).translucent,
              "D23 §8: alpha <= 0.2 -> buff timers 544..546 cleared once + FUN_00403cd0 every frame; alpha <= 0 -> bar hidden, four meshes removed");
        check(bad_node == 0 && latched == fired + yotyou + sprites && latched <= kDebrisLatches && yotyou > sprites,
              "D23 §8 / R13: each of nodes 0..99 fires once (FX2 95 scaled 1.1 x (0.3..0.79), rtype -1, or the 1/8 dust sprite); none on the removal frame");
    }
    {   // boss rush
        auto r = dying(true);
        Boss07Output o = r->step();
        const bool head = !flag(o, 226, 1) && o.player.iframes && *o.player.iframes == 9999 && o.exp && r->k().death_started;
        r->b.body().alpha = 0.15f; r->k().death_fc = 61;
        o = r->step();
        check(head && flag(o, 156, 0) && !o.kill_record && !flag(o, 544, 0),
              "D23 §8 / P1 D8: boss rush — no flag 226, i-frames 9999, and flag 156 is cleared once alpha <= 0.2");
    }
    {   // the player dead at the kill
        Rig r;
        r.park(2);
        r.b.body().hp = 0; r.in.player_hp_mirror = 0;
        const Boss07Output a = r.step(), b = r.step();
        check(a.exp && b.exp && !flag(a, 226, 1) && a.sounds.empty() && r.draws == 0 && !r.k().death_started && r.k().death_fc == 2,
              "D23 §8: a dead player — the head repeats every frame (EXP call included), nothing below the latch runs, no flag 226");
    }
}

int main() {
    test_ctor();
    test_shell();
    test_turn_drift();
    test_sequencer();
    test_start_attack();
    test_scheduler();
    test_damage();
    test_phase_ends();
    test_options_parts();
    test_options_and_shot();
    test_camera();
    test_hands();
    test_capture();
    test_barrier_events();
    test_death();
    std::printf("[boss07] %d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
