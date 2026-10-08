// Headless tests for src/cleria/boss08 (Darm) against build/boss_re/DARM.md (P1), DARM_A.md (A), DARM_B1.md (B1),
// DARM_B23.md (B23), DARM_CE.md (CE) and DARM_D.md (D). Every expectation is a number of those specs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "cleria/boss08.hpp"

using namespace cleria;
using namespace cleria::boss08;

static int fails = 0, total = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss08] %-112s %s\n", what, ok ? "OK" : "FAIL");
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

static const float kDeg = 0.017453292f, kHalfPi = 1.5707964f, kPi = 3.1415927f;
static bool nearf(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
static bool vnear(const Vec3& a, const Vec3& b, float tol = 1e-4f) {
    return nearf(a.x, b.x, tol) && nearf(a.y, b.y, tol) && nearf(a.z, b.z, tol);
}
static bool veq(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static std::uint32_t bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
static float fbits(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
static std::uint32_t r15(std::uint32_t v) { return v << 17; }            // a word whose (w >> 17) is v
static const std::uint32_t kU25 = 0x200000, kU50 = 0x400000, kU75 = 0x600000;   // words whose U() is .25 / .5 / .75

static int n_fx(const Boss08Output& o, int id) { int n = 0; for (const auto& e : o.effects) n += e.id == id; return n; }
static const Boss08Effect* fx(const Boss08Output& o, int id, int nth = 0) {
    for (const auto& e : o.effects) if (e.id == id && nth-- == 0) return &e;
    return nullptr;
}
static int n_se(const Boss08Output& o, int id, bool se2 = false) {
    int n = 0; for (const auto& s : o.sounds) n += s.id == id && s.se2 == se2; return n;
}
static int n_clip(const Boss08Output& o, int mesh, int sec) {
    int n = 0; for (const auto& c : o.clips) n += c.mesh == mesh && c.section == sec; return n;
}
static bool flag(const Boss08Output& o, int f, int v) {
    for (const auto& p : o.flags) if (p.first == f && p.second == v) return true;
    return false;
}
static bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
static const Boss08Motion* motion(const Boss08Output& o, int mesh, const char* name) {
    for (const auto& m : o.motions) if (m.mesh == mesh && std::strcmp(m.name, name) == 0) return &m;
    return nullptr;
}
static int n_bul(const Boss08Output& o, int kind, int effect) {
    int n = 0; for (const auto& b : o.bullets) n += b.kind == kind && b.effect == effect; return n;
}
static const Boss08Bullet* bul(const Boss08Output& o, int kind, int effect, int nth = 0) {
    for (const auto& b : o.bullets) if (b.kind == kind && b.effect == effect && nth-- == 0) return &b;
    return nullptr;
}
static bool shake_is(const Boss08Output& o, int n, float ax, float ay) {
    return o.shake && o.shake->frames == n && o.shake->ax == ax && o.shake->ay == ay;
}
static bool zoom_is(const Boss08Output& o, float s, float r) { return o.zoom && o.zoom->first == s && o.zoom->second == r; }
static bool grey_flash(const Boss08Output& o) {
    for (const auto& f : o.fader)
        if (f.colour && *f.colour == 0xaaaaaa && f.step && bits(*f.step) == 0x3d088889u && f.current && *f.current == 1.0f &&
            f.target && *f.target == 0.0f) return true;
    return false;
}

// The host stand-in: the clip model of DALLES_AB §0 for B / T / W / A / Y (advance_clip, one 0.25 advance a tick;
// a SetClip restarts at the first frame; a motion request is resolved by name), fixed world node positions, the
// RNG (forced raw words, else a fixed word, else xorshift).
struct Rig {
    Boss08 b;
    Boss08Input in;
    XorShift rng;
    std::deque<std::uint32_t> forced;
    std::optional<std::uint32_t> fixed;
    int draws = 0;
    int sec[5] = {0, 0, 2, 0, 0};
    float fr[5] = {10, 10, 20, 5, 320};
    std::uint32_t cnt[5] = {0, 0, 0, 0, 0};
    explicit Rig(const Boss08Setup& s = Boss08Setup{}) : b(s) {
        in.rng = [this]() -> std::uint32_t {
            ++draws;
            if (!forced.empty()) { const std::uint32_t v = forced.front(); forced.pop_front(); return v; }
            if (fixed) return *fixed;
            return rng();
        };
        in.player_pos = {0.0f, 0.0f, -10.0f};                      // Chr_Pos(13, 0, 0, -10) [P1 §1.1]
        for (int k = 0; k < kBodyNodes; ++k) in.b_node[std::size_t(k)] = {float(k), 10.0f + float(k), -float(k)};
        for (int k = 0; k < kTailNodes; ++k) in.t_local[std::size_t(k)] = {0.0f, 0.0f, 3.0f};
        in.t_eye = {0.0f, 6.0f, 14.0f};
        in.w_ora = {1.0f, 5.0f, 2.0f};
    }
    Rig(const Rig&) = delete;
    static const ClipInfo& info(int mesh, int s) {
        switch (mesh) {
            case kB: return kBodyClipTable[s];
            case kT: return kTailClipTable[s];
            case kW: return kCoreClipTable[s];
            case kA: return kArmClipTable[s];
            default: return kFloorClipTable[s];
        }
    }
    void clip(int mesh, int k) { sec[mesh] = k; fr[mesh] = float(info(mesh, k).first); cnt[mesh] = 0; }
    void at(int mesh, int k, float frame, std::uint32_t c = 0) { sec[mesh] = k; fr[mesh] = frame; cnt[mesh] = c; }
    void feed() {
        in.b_section = sec[kB]; in.b_count = cnt[kB]; in.b_frame = fr[kB];
        in.t_section = sec[kT]; in.t_count = cnt[kT]; in.t_frame = fr[kT];
        in.w_section = sec[kW]; in.y_section = sec[kY]; in.y_count = cnt[kY];
    }
    void apply(const Boss08Output& o) {
        for (const auto& c : o.clips) clip(c.mesh, c.section);
        for (const auto& m : o.motions) { const int i = clip_by_name(m.mesh, m.name); if (i >= 0) clip(m.mesh, i); }
    }
    Boss08Output step() {
        feed();
        Boss08Output o = b.tick(in);
        apply(o);
        for (int m = 0; m < 5; ++m) advance_clip(info(m, sec[m]), fr[m], cnt[m]);
        if (o.player.pos) in.player_pos = *o.player.pos;
        return o;
    }
    std::vector<Boss08Output> run(int n) { std::vector<Boss08Output> v; for (int i = 0; i < n; ++i) v.push_back(step()); return v; }
    // One bank tick with the body clip at an exact frame.
    Boss08Output bank(int clip_, float frame, std::uint32_t c = 0) { at(kB, clip_, frame, c); return step(); }
    Boss08BodyBlock& k() { return b.body(); }
    void form(int f) {                                             // a settled form: the blocks and the floor's loop
        static const int kLoop[4] = {0, 2, 1, 3};
        static const int kWait[4] = {0, 9, 19, 29};
        static const int kTWait[4] = {0, 2, 6, 18};
        k().form = k().form_req = f; b.tail().form = f;
        clip(kY, kLoop[f]); clip(kB, kWait[f]); clip(kT, kTWait[f]);
    }
};

static Boss08Setup setup(int diff, int chr = 3, bool easy = false, bool ve = false) {
    Boss08Setup s; s.diff_index = diff; s.character = chr; s.diff_easy = easy; s.diff_very_easy = ve;
    return s;
}
static int sum_se(const std::vector<Boss08Output>& v, int id, bool se2 = false) { int n = 0; for (const auto& o : v) n += n_se(o, id, se2); return n; }
static int sum_fx(const std::vector<Boss08Output>& v, int id) { int n = 0; for (const auto& o : v) n += n_fx(o, id); return n; }
static int sum_bul(const std::vector<Boss08Output>& v, int kind, int eff) { int n = 0; for (const auto& o : v) n += n_bul(o, kind, eff); return n; }
static int first_se(const std::vector<Boss08Output>& v, int id, bool se2 = false) {
    for (std::size_t i = 0; i < v.size(); ++i) if (n_se(v[i], id, se2)) return int(i);
    return -1;
}

// ---------------------------------------------------------------------------
static void test_tables_ctor() {
    check(kBodyClipTable[6].first == 303 && kBodyClipTable[6].last == 310 && kBodyClipTable[6].loop &&
          kBodyClipTable[35].first == 1360 && kBodyClipTable[35].last == 1405 && kBodyClipTable[38].last == 1421 &&
          kTailClipTable[25].first == 1720 && kTailClipTable[25].last == 1800 && kTailClipTable[19].first == 1480 &&
          kCoreClipTable[4].first == 42 && kArmClipTable[2].last == 160 && kFloorClipTable[3].first == 2460,
          "P1 §3.2 [DATA]: clip tables (B 07_Loop 303..310, 36_DoubleLariat 1360..1405; T 26_SwingTail 1720..1800; W / A / Y)");
    check(clip_by_name(kT, "21_Guard_Start") == 20 && clip_by_name(kT, "26_swingtail") == 25 && clip_by_name(kY, "d") == 3 &&
          clip_by_name(kB, "30_OnBackWait") == 29 && clip_by_name(kW, "04_Wait>>Swim") == 3 && clip_by_name(kA, "01_Clap") == 0 &&
          clip_by_name(kT, "nope") == -1,
          "CE §0 SetClipName: FUN_005f5c10's stricmp walk -> the section index; no match = nothing");
    {
        float f = 309.75f; std::uint32_t c = 0;
        for (int i = 0; i < 6; ++i) advance_clip(kBodyClipTable[6], f, c);
        float g = 1410.0f; std::uint32_t d = 0;
        for (int i = 0; i < 8; ++i) advance_clip(kBodyClipTable[36], g, d);
        check(c == 1 && f == 303.25f && g == 1411.0f && d == 1,
              "DALLES_AB §0 clip timing: a loop wraps past last + 1 (count++), a stop clip holds its last frame with count 1");
    }
    Rig n, h(setup(1)), nm(setup(2)), e(setup(0, 3, true)), ve(setup(0, 3, true, true));
    const Boss08Mesh &B = n.b.mesh(kB), &T = n.b.mesh(kT), &W = n.b.mesh(kW), &A = n.b.mesh(kA);
    check(veq(B.pos, {0, 0, 8.5f}) && B.str == 259 && B.def == 275 && B.touch && B.collide && n.b.mesh(kW).max_hp == 9999,
          "P1 §3.3 items 2/6, D2: EEX rec 7 (9999 / 258 / 275), STR += 1.0; the body at (0, 0, 8.5) with Touch_Tail");
    check(T.def == 999 && T.hp == 0 && T.max_hp == 0 && T.str == 259 && veq(T.pos, {0, -999, 8.5f}),
          "P1 §3.3 kind 1: the tail DEF 999, maxHP = HP = 0, created at (B.x, B.y - 999, B.z)");
    check(W.def == 999 && W.hp == 9999 && veq(W.pos, {10, 2, -10}) && n.b.core().alpha == 0 && n.b.core().alpha_t == 0 &&
          n.b.core().weak == 0 && A.no_hit && veq(A.pos, {0, -999, 8.5f}),
          "P1 §3.3 kinds 2/3: the core DEF 999 at (10, 2, -10), alpha / target 0, weak 0; the arms +0x298 |= 0x40 at y - 999");
    check(h.b.mesh(kW).hp == 12498.75f && nm.b.mesh(kW).hp == 14998.5f && e.b.mesh(kB).def == 274 &&
          ve.b.mesh(kB).def == 273 && ve.b.mesh(kB).str == 258 && nearf(ve.b.mesh(kW).hp, 7499.25f, 1e-2f),
          "D §2.4 / §7: the loader: HP x1.25 Hard, x1.5 Nightmare, x0.75 VE; DEF - 1 Easy, - 2 VE");
    bool parts = true;
    for (int i = 0; i < kBodyParts; ++i) parts = parts && n.b.part(i).radius == kPartRadius[i] && n.b.part(i).pool == 99990 && n.b.part(i).power == 1.0f;
    check(parts && kPartRadius[0] == 1.6f && kPartRadius[1] == 1.2f && kPartRadius[2] == 4.8f && kPartRadius[3] == 3.8f &&
          kPartRadius[4] == 2.2f && kPartRadius[5] == 2.5f && kPartRadius[7] == 2.5f && n.b.part(8).radius == 1.6f &&
          n.b.part(8).pool == 99990,
          "P1 §3.3 item 5 [f32 0x69ee98]: 8 parts r 1.6 1.2 4.8 3.8 2.2 2.5 2.2 2.5, pool HP x10, +0xfc 1.0; the core's r 1.6");
    const Boss08BodyBlock& k = n.b.body();
    check(k.cooldown == 180 && k.y_step == 0.04f && k.eye == 1 && k.chest == 1 && k.chest_t == 1 && k.leg_l == 1 && k.leg_r_t == 1 &&
          k.hand_l == 1 && k.hand_r_t == 1 && k.thunder_l == 0 && k.thunder_r_t == 0 && k.form == 0 && k.cam_idx == 1,
          "P1 §3.3 item 6: db+0x48 = 180, +0x84 = 0.04, +0xc4 = 1.0, the ten limb alphas / targets 1.0; CamIdx 1");
    Boss08Setup r1 = setup(0, 3, true); r1.retries = 6;
    Boss08Setup r2 = r1; r2.retries = 30;
    Boss08Setup r3 = setup(0, 3, true, true); r3.retries = 4;
    Boss08Setup r4 = r1; r4.boss_rush = true;
    const float kv = float(std::pow(4.0, 1.25));
    check(Boss08(r1).mesh(kB).str == 256 && Boss08(r1).mesh(kB).def == 271 && Boss08(r1).mesh(kT).def == 996 &&
          Boss08(r2).mesh(kB).str == 239 && nearf(Boss08(r3).mesh(kB).def, 273 - kv, 1e-3f) &&
          Boss08(r4).mesh(kB).str == 259,
          "P1 §3.3 item 15: retry softening on each object's own STR / DEF: Easy n>>1 (n<20) / n-10, VE pow(n,1.25); not in rush");
    check(kLimbStep[0] == 0.03f && bits(kLimbStep[1]) == 0x3ccccccdu && bits(kLimbStep[2]) == 0x3cb851ecu &&
          kPushOthers[1] == -0.12f && kPushOthers[2] == -0.14f && kPushToal[1] == -0.15f && kPushToal[2] == -0.2f &&
          kHomingExtra[0] == 90 && kHomingBase[1] == 390 && kBarrierHits[2] == 80 && kWindow[0] == 900 && kWindow[2] == 600 &&
          kSwingCd[0] == 440 && kSwingCd[2] == 340 && kAttackCd[0] == 180 && kAttackCd[1] == 140 && kAttackCd[2] == 100 &&
          kExposure == 600,
          "P1 §3.4: the difficulty tables (limb step, the two push tables, homing, barrier hits, window, cooldowns, exposure 600)");
    check(veq(kBitSpawn[0][1], {-15, 20, 0}) && veq(kBitTarget[0][2], {7.5f, 1.5f, -10}) && veq(kBitSpawn[1][0], {10, 20, 0}) &&
          veq(kBitTarget[1][3], {-5.5f, 1.5f, -8}) && veq(kBitTarget[1][5], {-5.5f, 1.5f, 0}),
          "B1 §5: the four Bit tables S0 / T0 / S1 / T1");
    check(veq(kVortex[2], {3.6f, 0.1f, -7.88f}) && veq(kVortex[6], {-3.2f, 0.1f, -8.88f}) && veq(kVortex[7], {-1.08f, 0.1f, -9.95f}) &&
          tentacle_track(0) == 4 && tentacle_track(3) == 7 && tentacle_track(4) == 0 && tentacle_track(7) == 3 &&
          std::strcmp(kTentacleNode[0], "Bone_LeftTentacle1-1") == 0 && std::strcmp(kTentacleNode[5], "Bone_RightTentacle2-1") == 0,
          "CE §3.2: V[k] (k 6 is not the mirror of k 2); name(k) / trk(k): left 1..4 -> tracks 4..7, right 1..4 -> 0..3");
    check(veq(kIntroCams[0].eye, {-20.8f, -3.695f, -30.651f}) && veq(kIntroCams[1].target, {6.507f, 1.505f, 2.835f}) &&
          kIntroCams[2].fov == 0.296706f && kEmblem == 8 && kBarLen == 9999.0f && kBitKind == 0x28,
          "P1 §3.3 items 13/16: the intro cameras of slots 7 / 8 / 9, the emblem 8, the bar scale 9999");
}

static void test_tracks() {
    Track t;
    check(t.speed == 1.0f && t.weight == 0 && !t.owns, "CE §2.1: a record starts zeroed with rec+0x10 = 1.0");
    track_bind(t, 5, 23);
    check(t.weight == 1.0f && t.section == 23 && t.count == 0 && t.frame == 1670.0f && t.root == 5 && t.owns && !t.releasing,
          "CE §2.2 bind: weight 1.0 (blend 0.05 < 1), first frame, count 0, the sub-tree owned, not releasing");
    for (int i = 0; i < 19; ++i) track_step(t);
    const float w19 = t.weight, f19 = t.frame;
    track_step(t); track_step(t);
    check(w19 > 0 && nearf(w19, 0.05f, 1e-5f) && f19 == 1674.75f && t.weight == 0 && t.frame == 1675.25f,
          "CE §2.3: the weight falls dt*0.05 a tick (gone in 20 ticks) while the cursor advances 0.25 a tick");
    for (int i = 0; i < 60; ++i) track_step(t);
    check(t.frame == 1689.0f && t.count == 1, "CE §2.3: a next -1 clip stops on its last frame with count 1 (24_Stab ends after 76 ticks)");
    track_release(t);
    check(t.weight == 1.0f && t.releasing && t.owns, "CE §2.2 release: weight 1.0, the releasing byte; the nodes still on the track");
    for (int i = 0; i < 22; ++i) track_step(t);
    check(!t.releasing && !t.owns && t.weight == 0, "CE §2.3 ZERO: when the release fade ends the sub-tree goes back to the main clip");
    Track l; track_bind(l, 0, 0);
    for (int i = 0; i < 325; ++i) track_step(l);
    check(l.count == 1 && l.frame == 11.25f, "CE §2.3: a next -2 clip wraps past last + 1 and counts (01_NormalWait 10..89)");
    Track u;
    track_step(u);
    check(u.frame == 0.25f && !u.owns, "CE §2.3: an idle record (weight 0, not releasing) still advances its cursor");
}

static void test_mover() {
    Vec3 p{0, 0, 0}; float s = 0.2f;
    mover_step(p, {0, 0, 1}, 0.2f, s);
    check(nearf(p.z, 0.2f) && s == 0.2f, "A §0 mover: pos += (d / L) * speed");
    for (int i = 0; i < 3; ++i) mover_step(p, {0, 0, 1}, 0.2f, s);
    check(s == 0 && nearf(p.z, 0.8f), "A §0 mover: stops when radius >= the remaining distance (no snap for radius >= 0.01)");
    Vec3 q{0, 0, 0}; float s2 = 0.3f;
    mover_step(q, {0, 0, 0.1f}, 0.001f, s2);
    check(s2 == 0 && veq(q, {0, 0, 0.1f}), "A §0 mover: an overshoot stops; radius < 0.01 snaps onto the target");
    Vec3 r{1, 2, 3}; float s3 = 0;
    mover_step(r, {9, 9, 9}, 1, s3);
    check(veq(r, {1, 2, 3}), "A §0 mover: speed 0 = not moving");
}

static void test_motion() {
    {
        Rig r;
        const Boss08Output o = r.step();
        check(nearf(r.b.mesh(kB).pos.z, 8.45f, 1e-5f) && r.b.mesh(kB).pos.x == 0 && r.k().yaw == 0 && r.k().move_base == 0 &&
              r.k().move_base_t == 1 && o.light && *o.light == 0xffaa8080u,
              "A §A.2 first-frame quirk: rate 1.0, d = 108.5 > 7.75 -> z 8.5 - 0.05; then the picker pins pair0 = (0, 1)");
        r.step(); r.step();
        check(nearf(r.b.mesh(kB).pos.z, 8.45f, 1e-5f), "A §A.2: the drift is 0 on every frame but the first (pair0 value 0)");
    }
    {
        Rig r; r.in.flag156 = false;
        r.step();
        Rig d; d.b.mesh(kW).hp = 0.5f;
        d.step();
        check(r.b.mesh(kB).pos.z == 8.5f && d.b.mesh(kB).pos.z == 8.5f, "A §A.2 gates: flag 156 and W.HP >= 1");
    }
    {
        Rig r; r.form(kHigh); r.in.player_pos = {20, 0, 8.5f};
        r.k().turn_over = 3.0f; r.k().turn_over_t = 2; r.k().turn_base = 9.0f; r.k().turn_base_t = 5;
        r.k().cooldown = 99999;
        r.step();
        // dx = -20, dz = 0: tgt = pi/2 - atan2(0, -20) = pi/2 - pi = -pi/2; the override rate wins
        check(nearf(r.k().yaw, -3.0f * 0.0035f, 1e-6f) && r.k().turn_over_t == 1,
              "A §A.2: in High the aim is the player; turn override (value, timer) wins: yaw steps r * 0.0035");
        r.k().turn_over_t = 0; r.k().turn_base = 9.0f; r.k().turn_base_t = 5;
        const float y0 = r.k().yaw;
        r.in.flag156 = true;
        r.b.core().exposure = 0;
        // run only motion's part: the picker rewrites pair1 after motion, so read the yaw step of this frame
        r.step();
        check(nearf(r.k().yaw - y0, -9.0f * 0.0035f, 1e-6f), "A §A.2: without an override the base pair's value is the rate");
    }
    {
        Rig r; r.form(kHigh); r.in.player_pos = {0, 0, 4.5f};      // d = 4 < 7.5: back away along +z
        r.k().move_over = 2.0f; r.k().move_over_t = 3; r.k().cooldown = 99999;
        r.step();
        check(nearf(r.b.mesh(kB).pos.z, 8.5f + 2.0f * 0.04f, 1e-5f) && r.k().move_over_t == 2,
              "A §A.2: closer than 7.5 -> back away by m * 0.04 (move override pair)");
        Rig f; f.form(kHigh); f.in.player_pos = {0, 0, -3.5f};     // d = 12 > 7.75: approach
        f.k().move_over = 1.0f; f.k().move_over_t = 3; f.k().cooldown = 99999;
        f.step();
        check(nearf(f.b.mesh(kB).pos.z, 8.5f - 0.05f, 1e-5f), "A §A.2: farther than 7.75 -> approach by m * 0.05");
    }
    {
        Rig r; r.b.mesh(kB).pos = {0, 0, 30};
        r.step();
        const Vec3 p = r.b.mesh(kB).pos;
        check(nearf(std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), 21.0f, 1e-3f), "A §A.2: the position is clamped to a sphere of radius 21");
    }
}

static void test_pushout() {
    {
        Rig r; r.in.flag156 = false; r.in.player_pos = {0, 0, 12.0f};      // the centre is (0, 0, 10): d = 2
        const Boss08Output o0 = r.step();                          // db+0x5c is still the ctor's 0: chain() runs after
        const Boss08Output o = r.step();
        check(o0.player.push.z == 0 && nearf(o.player.push.z, (4.0f - 2.0f) * 0.2f) && o.player.push.x == 0,
              "A §A.3: centre 1.5 ahead (local +z), r 4.0: P+0x340 += n.z * (r - d) * 0.2 while the form is settled; not gated");
        r.clip(kY, 4); r.step();                                   // (the push reads the previous frame's db+0x5c)
        const Boss08Output o2 = r.step();
        r.clip(kY, 0); r.k().restore = false; r.k().down = true; r.step();
        const Boss08Output o3 = r.step();
        r.in.player_pos = {0, 0, 11.0f};
        const Boss08Output o4 = r.step();
        check(o2.player.push.z == 0 && o3.player.push.z == 0 && nearf(o4.player.push.z, (2.0f - 1.0f) * 0.2f),
              "A §A.3: no push while the form is not settled; Down: radius 2.0");
    }
    {
        Rig r; r.in.flag156 = false; r.form(kHigh); r.in.player_pos = {0, 1, 0};   // n = (0, 0, -1)
        r.step();
        const Boss08BodyBlock& k = r.k();
        check(vnear(k.cam_eye, {0, 26, -46}) && vnear(k.cam_target, {0, 2, 1}) && k.cam_fov == 0.523599f && veq(k.cam_off, {0, 5.55f, 0}) &&
              k.cam_dist == 0,
              "A §A.3 High camera block: eye = P + 46n + (0, 25, 0), target = P - n + (0, 1, 0), fov 30 deg, offset (0, 5.55, 0)");
        Rig n; n.in.flag156 = false; n.step();
        check(n.k().cam_fov == 0, "A §A.3: the block is written in form 2 only");
    }
}

static void test_chain() {
    auto run1 = [](int form, int clip_, std::uint32_t c, int breath = 0) {
        Rig r; r.in.flag156 = false; r.form(form); r.k().breath = breath; r.k().thunder_l_t = r.k().thunder_r_t = 1.0f;
        r.at(kB, clip_, float(kBodyClipTable[clip_].first) + 1.0f, c);
        Boss08Output o = r.step();
        return std::make_pair(o, r.k().thunder_l_t + r.k().thunder_r_t);
    };
    check(n_clip(run1(0, 5, 1).first, kB, 6) == 1 && n_clip(run1(0, 5, 0).first, kB, 6) == 0,
          "A §A.4 form 0: 06_Breath_Start -> 07_Loop at 1 completion");
    check(n_clip(run1(0, 6, 2, 0).first, kB, 7) == 1 && n_clip(run1(0, 6, 1, 0).first, kB, 7) == 0 &&
          n_clip(run1(0, 6, 1, 1).first, kB, 7) == 1 && n_clip(run1(0, 6, 8, 0x63).first, kB, 7) == 1 &&
          n_clip(run1(0, 6, 2, 0x63).first, kB, 7) == 0 && n_clip(run1(0, 6, 1, 7).first, kB, 7) == 1,
          "A §A.4 / C8: the loop leaves at cnt == 2 (variant 0), 1 (variant 1), 8 (0x63), 1 (anything else)");
    {
        const auto a = run1(0, 1, 1), b = run1(0, 36, 1), c = run1(0, 1, 0);
        check(n_clip(a.first, kB, 0) == 1 && a.second == 0 && b.first.clips.empty() && b.second == 2 && c.first.clips.empty(),
              "A §A.4 form 0: any other finished clip -> 01_NormalWait and both thunder targets 0; 37_NormalDown holds");
    }
    check(n_clip(run1(1, 14, 1).first, kB, 15) == 1 && n_clip(run1(1, 15, 30).first, kB, 16) == 1 &&
          n_clip(run1(1, 15, 29).first, kB, 16) == 0 && n_clip(run1(1, 11, 1).first, kB, 9) == 1 && run1(1, 37, 1).first.clips.empty(),
          "A §A.4 form 1: Concentration Start -> Loop at 1, Loop -> End at 30; else -> 10_LowWait; 38_LowDown holds");
    check(n_clip(run1(2, 20, 1).first, kB, 21) == 1 && n_clip(run1(2, 21, 4).first, kB, 22) == 1 && n_clip(run1(2, 21, 3).first, kB, 22) == 0 &&
          n_clip(run1(2, 24, 1).first, kB, 25) == 1 && n_clip(run1(2, 25, 20).first, kB, 26) == 1 && run1(2, 38, 1).first.clips.empty(),
          "A §A.4 form 2: RotateSpear loop 4 passes, QuakeTheWing loop 20 passes; 39_HighDown holds");
    {
        const auto a = run1(2, 23, 1), b = run1(2, 22, 1);
        check(n_clip(a.first, kB, 19) == 1 && a.second == 2 && n_clip(b.first, kB, 19) == 1 && b.second == 0,
              "A §A.4 / C7: 24_MachineGunStab -> 20_HighWait WITHOUT zeroing the thunder targets; the other clips zero them");
    }
    check(n_clip(run1(3, 30, 1).first, kB, 29) == 1 && n_clip(run1(3, 19, 1).first, kB, 29) == 1 && n_clip(run1(3, 29, 1).first, kB, 29) == 0,
          "A §A.4 / C7 form 3: every finished clip != 29 -> 30_OnBackWait");
    {
        static const int kLoop[4] = {0, 2, 1, 3};
        static const char* const kName[4] = {"a", "c", "b", "d"};
        bool ok = true;
        for (int f = 0; f < 4; ++f) {
            Rig r; r.in.flag156 = false; r.form(f);
            r.step(); ok = ok && r.k().settled;
            r.at(kY, 4 + f, 100.0f, 0);
            const Boss08Output o = r.step(); ok = ok && !r.k().settled && o.motions.empty();
            r.at(kY, 4 + f, 100.0f, 1);
            const Boss08Output o2 = r.step();
            const Boss08Motion* m = motion(o2, kY, kName[f]);
            ok = ok && m && m->flag && r.sec[kY] == kLoop[f];
        }
        check(ok, "A §A.4 SETTLE: db+0x5c = 1 iff the floor is in the form's loop (a / c / b / d); a finished one-shot requests it");
    }
}

static void test_form_change() {
    struct Row { int old, nf, clip; const char* tail; const char* floor; bool fflag; float z; };
    static const Row kRows[] = {
        {1, 0, 17, "05_Low>>Normal", "k", true, 8.5f},   {2, 0, 27, "17_High>>Normal", "h", true, 8.5f},
        {0, 1, 8, "02_Normal>>Low", "f", true, -1.0f},   {2, 1, 8, "17_High>>Normal", "i", true, -1.0f},
        {0, 2, 18, "06_Normal>>High", "e", true, 0.0f},  {1, 2, 18, "05_Low>>Normal", "l", true, 0.0f},
        {0, 3, 28, "18_Normal>>OnBack", "g", false, 10.0f}, {1, 3, 28, "05_Low>>Normal", "m", false, 10.0f},
        {2, 3, 28, "17_High>>Normal", "j", false, 10.0f},
    };
    bool pair = true, common = true, latch = true;
    for (const Row& w : kRows) {
        Rig r; r.in.flag156 = false; r.form(w.old); r.k().form_req = w.nf; r.fixed = r15(0);
        r.k().cooldown = 5; r.b.tail().swing_cd = 7;
        const Boss08Output o = r.step();
        const Boss08Motion *mt = motion(o, kT, w.tail), *my = motion(o, kY, w.floor);
        pair = pair && n_clip(o, kB, w.clip) == 1 && mt && mt->flag && my && my->flag == w.fflag &&
               r.k().form == w.nf && r.b.tail().form == w.nf;
        const Boss08Mesh& B = r.b.mesh(kB);
        common = common && n_se(o, 345) == 1 && veq(B.move_target, {0, B.move_target.y, w.z}) && B.move_radius == 0.2f &&
                 r.k().turn_over == 6.0f && r.k().turn_over_t == 360 && o.cam_slot && o.cam_slot->first == 6 &&
                 bits(o.cam_slot->second) == 0x3ca3d70au && zoom_is(o, 0.5f, -0.01f) && r.k().cooldown == 180 &&
                 r.k().homing_timer == 180 && r.b.tail().tentacle_cd == 180 && r.b.tail().swing_cd == 180 &&
                 o.mat_mode.size() == 1 && o.mat_mode[0] == 2 && r.draws == (w.nf == 3 ? 0 : 1);
        latch = latch && r.k().low_latch == (w.nf == 1);
    }
    check(pair, "A §A.5 pairing table: body clip by target form, tail motion and floor one-shot by (old, new); OnBack floor flag 0");
    check(common, "A §A.5: SE 345, MoveTo (0, y, z) r 0.2, turn override (6.0, 360), CamIdx 6 @ 0.02, ZoomBlur, 180 x4, MatMode 2, 1 draw");
    check(latch, "A §A.5: db+0x74 = 1 on a change into Low from Normal / High");
    {
        bool ok = true;
        const int kExp[3] = {1, 2, 0};
        for (std::uint32_t w = 0; w < 3; ++w) {
            Rig r; r.in.flag156 = false; r.k().form_req = 1; r.fixed = r15(w); r.b.core().weak = 9; r.b.core().alpha_t = 0.5f;
            r.step();
            ok = ok && r.b.core().weak == kExp[w] && r.b.core().alpha_t == 0 && r.k().eye == 1.0f;
        }
        check(ok, "A §A.5: the commit's R(3) is the next weak point (0 -> 1, 1 -> 2, 2 -> 0); w+0x1ac = 0; db+0xc4 = 1.0");
    }
    {
        Rig r; r.in.flag156 = false; r.k().form_req = 3; r.b.core().weak = 2;
        r.k().chest_t = r.k().leg_l_t = r.k().leg_r_t = r.k().hand_l_t = r.k().hand_r_t = 0.25f;
        r.step();
        const Boss08BodyBlock& k = r.k();
        check(k.eye == 0 && k.chest_t == 1 && k.leg_l_t == 1 && k.leg_r_t == 1 && k.hand_l_t == 1 && k.hand_r_t == 1 &&
              r.b.core().alpha_t == 1.0f && r.b.core().weak == 0 && r.draws == 0 && !k.low_latch,
              "A §A.5 -> OnBack: db+0xc4 = 0, every limb target 1.0, the core visible (w+0x1ac = 1), weak 0, no draw");
    }
    {
        Rig r; r.in.flag156 = false; r.k().form_req = 2; r.b.mesh(kW).hp = 0.5f;
        const Boss08Output o = r.step();
        Rig f; f.in.flag156 = false; f.k().form_req = 2; f.k().fall = 5;
        const Boss08Output of = f.step();
        Rig g; g.in.flag156 = false; g.k().form_req = 2;
        g.step();
        const Boss08Output o2 = g.step();
        check(o.clips.empty() && r.k().form == 0 && n_se(o, 345) == 0 && !(of.cam_slot && of.cam_slot->first == 6) &&
              f.k().form == 2 && n_se(o2, 345) == 0,
              "A §A.5: pending while W.HP < 1; no camera write while the player falls; the function runs once per change");
    }
    {
        Rig r; r.in.flag156 = false; r.k().form_req = 1; r.fixed = r15(0);
        r.step();
        const Vec3 p0 = r.b.mesh(kB).pos;
        r.step();
        check(nearf(p0.z, 8.5f - 0.2f) && nearf(r.b.mesh(kB).pos.z, 8.5f - 0.4f), "A §A.5 timeline: the mover starts at the end of frame N, 0.2 per frame");
    }
}

static void test_shell() {
    {
        Rig r; r.in.flag156 = false;
        Boss08BodyBlock& k = r.k();
        k.thunder_l_t = 1; k.leg_l_t = 0; k.hand_r = 0.5f; k.chest = 0.3f; k.chest_t = 0.303f; k.leg_r = 0.002f; k.leg_r_t = 0;
        r.b.core().weak = 1;                                       // (left leg: not at 0 yet)
        r.step();
        check(nearf(k.thunder_l, 0.025f, 1e-7f) && nearf(k.leg_l, 0.995f, 1e-6f) && nearf(k.hand_r, 0.51f, 1e-6f) &&
              k.chest == 0.303f && k.leg_r == 0,
              "A §A.1.1 block 1: thunders +-0.025; limbs -0.005 / +0.01 toward the target, clamped onto it");
    }
    {
        Rig r; r.in.flag156 = false; r.b.core().weak = 0;
        Boss08BodyBlock& k = r.k();
        k.leg_l = 0; k.leg_l_t = 0;                                // a broken group that is NOT the weak point
        r.step();
        const bool started = k.regrow[1] == 1 && !k.down;
        r.run(599);
        const bool at600 = k.regrow[1] == 600 && k.leg_l_t == 0;
        r.step();
        const bool regrown = k.regrow[1] == 1 && k.leg_l_t == 1.0f;       // (block 4 re-arms it: the alpha is still 0)
        Rig e; e.in.flag156 = false; e.b.core().weak = 0; e.k().leg_l = 0; e.k().leg_l_t = 0; e.k().regrow[1] = 600;
        e.b.core().exposure = 5; e.b.core().dropped = true;
        e.step();
        check(started && at600 && regrown && e.k().leg_l_t == 0 && e.k().regrow[1] == 601,
              "A §A.1.1 blocks 2/4: alpha 0 starts the timer; > 600 with the core hidden -> target 1.0; held while exposed");
    }
    {
        Rig r; r.in.flag156 = false; r.b.core().weak = 2;
        Boss08BodyBlock& k = r.k();
        k.leg_l = 0.5f; k.leg_l_t = 0.495f; k.hand_l = 0.95f; k.hand_l_t = 0.945f; k.chest = 0.4f; k.chest_t = 0.395f;
        k.thunder_l = 0.5f; k.thunder_l_t = 0.5f; k.thunder_r = 0.25f; k.thunder_r_t = 0.25f;
        r.step();
        const auto& a = r.b.mat_alpha();
        const bool normal = nearf(a[25], 0.595f, 1e-6f) && a[26] == a[25] && a[27] == a[25] && nearf(a[28], 0.945f, 1e-6f) &&
                            a[31] == 1.0f && a[34] == 1.0f && a[0] == 1.0f && nearf(a[12], 0.495f, 1e-6f) && a[19] == a[12] &&
                            a[20] == 0.5f && a[21] == 0.25f;
        r.form(kLow);
        r.step();
        const bool low = nearf(r.b.mat_alpha()[0], r.b.mat_alpha()[12], 1e-7f) && r.b.mat_alpha()[2] == r.b.mat_alpha()[0];
        r.form(kOnBack); k.eye = 0;
        r.step();
        check(normal && low && r.b.mat_alpha()[18] == 0 && r.b.mat_alpha()[7] == 0,
              "A §A.1.1 block 3: shown = a + 0.1 when 0 < a < 0.9 and tgt != 1; chest mats only in Low; mats 20 / 21; OnBack eyes = db+0xc4");
    }
    {
        Rig r; r.in.flag156 = false; r.b.core().weak = 0;
        Boss08BodyBlock& k = r.k();
        k.leg_l = 0.5f; k.leg_l_t = 0.4f; k.hand_r = 0.95f; k.hand_r_t = 0.5f; k.chest = 0.5f; k.chest_t = 1.0f;
        r.step();
        const auto& d = r.b.mat_damaged();
        const bool n = d[25] && d[26] && d[27] && !d[28] && !d[34] && !d[12] && !d[0] && r.b.chest_flags_zero();
        k.chest_t = 0.4f; r.form(kLow);
        r.step();
        check(n && r.b.mat_damaged()[12] && r.b.mat_damaged()[17] && r.b.mat_damaged()[0] && r.b.mat_damaged()[2] && !r.b.chest_flags_zero(),
              "A §A.1.3: bit 0x20000 = (alpha < 0.9 && target != 1.0); mats 0..2 only in Low (else their flag word is zeroed)");
    }
    {
        bool ok = true;
        static const char* const kTail[3] = {"01_NormalWait", "03_LowWait", "07_HighWait"};
        for (int f = 0; f < 3; ++f) {
            Rig r; r.in.flag156 = false; r.form(f); r.b.core().weak = 1; r.k().leg_l = 0.005f; r.k().leg_l_t = 0;
            r.k().thunder_l_t = r.k().thunder_r_t = 1;
            const Boss08Output o = r.step();
            ok = ok && n_clip(o, kB, 36 + f) == 1 && motion(o, kT, kTail[f]) && n_se(o, 578) == 1 && r.k().down &&
                 !r.b.mesh(kB).touch && o.mat_mode.size() == 1 && o.mat_mode[0] == 1 && r.b.core().alpha_t == 1.0f &&
                 r.b.core().exposure >= 1 && r.k().thunder_l_t == 0 && r.k().thunder_r_t == 0;
            const Boss08Output o2 = r.step();
            ok = ok && n_se(o2, 578) == 0;
        }
        Rig w; w.in.flag156 = false; w.b.core().weak = 2; w.k().leg_l = 0; w.k().leg_l_t = 0;
        const Boss08Output ow = w.step();
        check(ok && !w.k().down && n_se(ow, 578) == 0,
              "A §A.1.1 block 5 DOWN: the weak group's alpha == 0 and the core hidden -> Down clip 36/37/38, tail wait, touch off, MatMode 1, SE 578");
    }
    {
        Rig r; r.in.flag156 = false;
        Boss08Mesh& W = r.b.mesh(kW);
        r.b.core().refill = true; r.b.core().band = 2; W.hp = 0.6f * W.max_hp - 30.0f;
        r.step();
        const float h1 = W.hp;
        r.step();
        check(nearf(h1, 0.6f * 9999 - 5.0f, 1e-2f) && r.b.core().refill == false && nearf(W.hp, 0.6f * 9999 + 20.0f - 50.0f, 1e-2f),
              "A §A.1.1 block 6 / P1 D5: +25 HP a frame while refilling; above the band line -> off and -50");
    }
    {
        Rig r; r.k().cooldown = 99999; r.clip(kY, 4);              // the floor in a one-shot: not settled
        r.b.mesh(kB).touch = false; r.k().down = true; r.k().cam_idx = 6;
        std::vector<Boss08Output> v = r.run(120);
        const bool trans = r.k().restore && r.k().cam_back && sum_se(v, 338) == 2 && n_se(v[0], 338) == 1 && n_se(v[60], 338) == 1;
        r.clip(kY, 0);
        const Boss08Output o = r.step();
        check(trans && o.cam_slot && o.cam_slot->first == 1 && o.cam_slot->second == 0.025f && r.b.mesh(kB).touch && !r.k().down &&
              !r.k().restore && !r.k().cam_back && has(o.mat_mode, 0) && zoom_is(o, 0.5f, -0.01f) && n_se(o, 186) == 1,
              "A §A.1.1 block 7: in transition SE 338 every 60 counted frames; settled: CamIdx 1 + form @ 0.025, touch on, MatMode 0, ZoomBlur, SE 186");
    }
}

static void test_fall() {
    Rig r; r.in.flag156 = false;
    r.in.player_pos = {3, -1.3f, 2};
    Boss08Output o = r.step();
    const bool sink = o.player.pos && nearf(o.player.pos->y, -2.0f);
    r.in.player_grounded = false;
    o = r.step();
    const bool nosink = !o.player.pos;
    check(sink && nosink, "A §A.1.2: at y <= -1.3 a grounded player sinks 0.7 a frame through the rim");
    r.in.player_pos = {3, -19, 2}; r.in.player_hp = 0.5f;
    o = r.step();
    check(o.player.dead_stop && o.player.flyer && r.k().fall == 0, "A §A.1.2: a dead player: pushes zeroed, P+0x398 = 1 under y -18; no sequence");
    r.in.player_hp = 100;
    o = r.step();
    check(r.k().fall == 1 && r.k().fall_tint == 200 && o.player.fall_start && has(o.player.sections, 2) && o.cam_slot &&
          o.cam_slot->first == 5 && o.cam_slot->second == 1.0f && shake_is(o, 220, 0.2f, 0.2f) && n_se(o, 339) == 1 &&
          n_se(o, 340) == 1 && flag(o, 518, 0) && flag(o, 519, 0) && !o.fader.empty() && *o.fader[0].colour == 0xaa0000 &&
          bits(*o.fader[0].step) == 0x3bfc0fc1u && *o.fader[0].target == 1.0f,
          "A §A.1.2 trigger (y < -18, alive): Fade(0xaa0000, 1/130, 1), section 2, CamIdx 5 @ 1.0, Shake(220, 0.2), tint 200, SE 339 + 340");
    o = r.step();
    check(o.player.pos && veq(*o.player.pos, {0, 495, 0}) && o.player.tint && *o.player.tint == (0xff000000u | 199u * 0x010101u) &&
          r.k().fall == 2,
          "A §A.1.2 falling: the player pinned at (0, 495, 0), the tint counts down from 200");
    std::vector<Boss08Output> v = r.run(27);
    o = r.step();                                                  // db+0x24 == 30
    const Boss08Effect* e = fx(o, 517);
    check(sum_fx(v, 517) == 0 && e && veq(e->pos, {0, 495, -1}) && e->stat == Boss08Stat::kBody && e->power == 0 && e->knock == 1 &&
          e->parent == Boss08Parent::kNone && !o.fader.empty() && !o.fader.back().colour && *o.fader.back().target == 0,
          "A §A.1.2 / §A.1.4: at db+0x24 == 30 effect 517 at (0, 495, -1), stat B, power 0; fader target 0");
    v = r.run(188);
    const bool pinned = r.k().fall == 218 && !v.back().player.iframes;
    o = r.step();
    check(pinned && r.k().fall == 0 && o.player.pos && veq(*o.player.pos, {0, 19, -6}) && o.player.iframes && *o.player.iframes == 180 &&
          has(o.player.sections, 9) && o.player.fall_end && *o.player.tint == 0xffffffffu && flag(o, 134, 0) && o.cam_slot &&
          o.cam_slot->first == 1 && o.cam_slot->second == 1.0f && *o.fader.back().colour == 0xffffff &&
          bits(*o.fader.back().step) == 0x3c088889u,
          "A §A.1.2: frame 218 (db+0x24 > 218): back at (0, 19, -6), 180 i-frames, section 9, white Fade 1/120, CamIdx 1 + form, flag 134 = 0");
    Rig u; u.in.flag156 = false; u.clip(kY, 4); u.k().fall = 218;
    o = u.step();
    check(o.cam_slot && o.cam_slot->first == 6, "A §A.1.2: after the fall the camera goes to slot 6 while the form is not settled");
}

static void test_core() {
    {
        Rig r; r.k().cooldown = 99999;
        r.step();
        const bool belly = veq(r.b.mesh(kW).pos, r.in.b_node[kBerryMouthMark]) && r.b.mesh(kW).no_hit;
        r.b.core().weak = 1; r.step();
        const Vec3 l = r.in.b_node[kLeftLeg], lf = r.in.b_node[kLeftFoot];
        const bool left = vnear(r.b.mesh(kW).pos, {(l.x + lf.x) / 2, (l.y + lf.y) / 2, (l.z + lf.z) / 2});
        r.b.core().weak = 2; r.step();
        const Vec3 rl = r.in.b_node[kRightLeg], rf = r.in.b_node[kRightFoot];
        check(belly && left && vnear(r.b.mesh(kW).pos, {(rl.x + rf.x) / 2, (rl.y + rf.y) / 2, (rl.z + rf.z) / 2}),
              "A §A.1.5 seq 0: the core is glued to BerryMouthMark / the leg-foot midpoints (u32 0x69ed84 = 19, 5, 3); hidden: +0x298 |= 0x40");
    }
    {
        Rig r; r.k().cooldown = 99999;
        r.in.b_node[kBerryMouthMark] = {0, 3, 6};                  // (so the 0.03 sink to y 1.5 ends inside the window)
        Boss08CoreBlock& w = r.b.core();
        w.exposure = 1; w.alpha_t = 1.0f;
        const Boss08Output o = r.step();
        const Boss08Effect* e = fx(o, 514);
        const Boss08Mesh& W = r.b.mesh(kW);
        check(w.exposure == 2 && e && e->call == Boss08Call::kFx && e->src == 1 && e->stat == Boss08Stat::kCore &&
              e->parent == Boss08Parent::kCoreOra && e->at_mode == 1 && w.dropped && W.move_target.y == 1.5f &&
              W.move_speed == 0.03f && !W.no_hit && nearf(w.alpha, 0.02f, 1e-7f),
              "A §A.1.5 EXPOSED: counter 2 -> FX 514 on OraMarker (src 1, stat W), dropped, MoveTo y 1.5 @ 0.03, hittable; alpha +0.02");
        std::vector<Boss08Output> v = r.run(597);
        const bool open = w.exposure == 599 && r.k().hand_l_t == 1 && sum_fx(v, 514) == 0 && w.alpha == 1.0f && w.was_exposed;
        r.k().leg_l_t = r.k().leg_r_t = r.k().chest_t = r.k().hand_l_t = r.k().hand_r_t = 0;
        const Boss08Output oc = r.step();                          // the 599th W frame after Down: db+0x1b4 reaches 600
        check(open && w.exposure == 0 && !w.dropped && w.alpha_t == 0 && w.seq == 1 && r.k().leg_l_t == 1 && r.k().leg_r_t == 1 &&
              r.k().chest_t == 1 && r.k().hand_l_t == 1 && r.k().hand_r_t == 1 && !W.no_hit && n_fx(oc, 514) == 0,
              "A §A.1.5 / C11: the exposure ends when db+0x1b4 reaches 600: every limb target 1.0, seq 1, alpha target 0");
        w.was_exposed = false;                                     // (keep the next form out of this test)
        const float y0 = W.pos.y;
        const Boss08Output o1 = r.step();
        const bool up = w.seq == 2 && nearf(W.move_target.y, y0 + 10.0f) && W.move_radius == 0.17f && motion(o1, kW, "04_Wait>>Swim") &&
                        W.no_hit && nearf(w.alpha, 1.0f - 2 * 0.0055f, 1e-6f);
        int n = 0;
        while (W.move_speed != 0 && n < 500) { r.step(); ++n; }
        const std::vector<Boss08Output> hv = r.run(30);
        const bool hover = w.seq == 2 && w.seq_wait == 30;
        const Boss08Output o2 = r.step();
        const bool back = w.seq == 3 && w.seq_wait == 0 && veq(W.move_target, r.in.b_node[kSpine2]) && W.move_radius == 0.08f &&
                          motion(o2, kW, "02_Swim>>Wait");
        while (W.move_speed != 0 && n < 2000) { r.step(); ++n; }
        r.step();
        check(up && hover && back && w.seq == 0 && hv.size() == 30,
              "A §A.1.5 seq 1..3: fly up (+10 @ 0.17 in Normal, 04_Wait>>Swim), hover 31 frames, return to Bone_Spine2 @ 0.08 (02_Swim>>Wait)");
    }
    {
        bool ok = true;
        const float kUp[3] = {10.0f, 7.75f, 14.0f}, kV[3] = {0.17f, 0.14f, 0.2f}, kBack[3] = {0.08f, 0.1f, 0.08f};
        for (int f = 0; f < 3; ++f) {
            Rig r; r.in.flag156 = false; r.form(f); r.b.core().seq = 1;
            r.step();
            const Boss08Mesh& W = r.b.mesh(kW);
            ok = ok && nearf(W.move_target.y - 2.0f, kUp[f]) && W.move_speed == kV[f];        // from where the core is
            r.b.mesh(kW).move_speed = 0; r.b.core().seq_wait = 30;
            r.step();
            Vec3 p = r.in.b_node[kSpine2]; if (f == 1) p.y += 0.5f;
            ok = ok && veq(W.move_target, p) && W.move_speed == kBack[f];
        }
        Rig e(setup(0, 3, true)); e.in.flag156 = false; e.b.core().exposure = 658;
        e.step(); const bool e659 = e.b.core().exposure == 659;
        e.step();
        check(ok && e659 && e.b.core().exposure == 0,
              "A §A.1.5: per form up 10 / 7.75 / 14 @ 0.17 / 0.14 / 0.2, back @ 0.08 / 0.1 / 0.08 (+0.5 y in Low); Easy window 660");
    }
    {
        Rig r; r.in.flag156 = false;
        r.clip(kW, 3); Boss08Output o = r.step(); const bool a = n_clip(o, kW, 0) == 1;
        r.clip(kW, 1); o = r.step(); const bool b = n_clip(o, kW, 2) == 1;
        r.clip(kW, 4); o = r.step(); const bool c = n_clip(o, kW, 2) == 1;
        r.clip(kW, 0); o = r.step(); const bool d = o.clips.empty();
        r.in.flag156 = true; r.k().cooldown = 99999; r.b.tail().tentacle_cd = 99999;
        r.form(kOnBack); r.clip(kW, 2); o = r.step(); const bool e = n_clip(o, kW, 5) == 1;
        o = r.step();
        check(a && b && c && d && e && n_clip(o, kW, 5) == 0 && veq(r.b.mesh(kW).pos, r.in.t_eye),
              "A §A.7 / C12: 04_Wait>>Swim -> 01_Swim, 02 / 05 / 06 -> 03_Wait at once; OnBack: 06_NoMove, the core on the tail's Bone_eye");
    }
    {
        Rig r; r.b.core().alpha = r.b.core().alpha_t = 1.0f; r.b.core().exposure = 5; r.b.core().dropped = true; r.k().cooldown = 99999;
        std::vector<Boss08Output> v = r.run(119);
        const Boss08Effect* e = fx(v[0], 454);
        const bool aura = sum_fx(v, 454) == 3 && e && e->call == Boss08Call::kInline && e->stat == Boss08Stat::kCore &&
                          e->parent == Boss08Parent::kCoreOra && e->at_mode == 1 && e->or_1dc == 0x100 && n_fx(v[59], 454) == 1 &&
                          n_fx(v[118], 454) == 1;
        Rig f; f.b.core().alpha = f.b.core().alpha_t = 1.0f; f.b.core().exposure = 5; f.in.fx_pool_full = true;
        Rig h; h.b.core().alpha = h.b.core().alpha_t = 0.98f; h.b.core().exposure = 5; h.b.core().alpha_t = 0.98f;
        check(aura && n_fx(f.step(), 454) == 0 && n_fx(h.step(), 454) == 0,
              "A §A.8: the aura 454 (inline, on OraMarker, stat W) every 59th W frame while alpha == 1.0; refused by a full pool");
    }
    {
        Rig r; r.in.flag156 = false;
        const float y0 = 2.0f;
        r.b.core().dropped = true;                                 // keep the glue out of the way
        r.b.mesh(kW).pos.y = y0;
        r.step(); r.step();
        Rig d; d.in.flag156 = false; d.b.core().dropped = true; d.k().dead = true; d.b.mesh(kW).pos.y = y0;
        d.step();
        check(r.b.mesh(kW).pos.y == y0 + 6.0f && d.b.mesh(kW).pos.y == y0,
              "A §A.1.6: with flag 156 closed the core drifts up 3.0 a frame (not once Darm is dead)");
    }
    {
        Rig r; r.k().cooldown = 99999;
        const Boss08Output o = r.step();
        const Boss08Output o2 = r.step();
        r.b.mesh(kW).hp -= 10;
        const Boss08Output o3 = r.step();
        check(o.hp_bar && o.hp_bar->hp == 9999 && o.hp_bar->max_hp == 9999 && !o2.hp_bar && o3.hp_bar && o3.hp_bar->hp == 9989 &&
              r.b.mesh(kW).alpha == r.b.core().alpha && r.b.core().fc == 3 && r.b.tail().fc == 3 && r.b.arms().fc == 3 && r.k().fc == 3,
              "A §A.1.6: the HP bar is W's (refreshed when W+0x37c != HP); W+0x418 = db+0x1b0; every kind counts db+0x70");
    }
}

static void test_damage_body() {
    {
        Rig r; r.k().cooldown = 99999;
        r.step();
        const auto& N = r.in.b_node;
        auto lerp = [](const Vec3& b, const Vec3& o, float k) { return Vec3{(o.x - b.x) * k + b.x, (o.y - b.y) * k + b.y, (o.z - b.z) * k + b.z}; };
        bool pos = veq(r.b.part(0).pos, lerp(N[kHead], N[kSkirt1], 0.275f)) && veq(r.b.part(1).pos, N[kHead]) &&
                   vnear(r.b.part(2).pos, lerp(N[kSkirt1], N[kSkirt2], 2.0f)) && veq(r.b.part(3).pos, lerp(N[kSkirt1], N[kMaekake], 0.1f)) &&
                   veq(r.b.part(4).pos, lerp(N[kLeftLeg], N[kLeftFoot], 0.75f)) && veq(r.b.part(5).pos, N[kLeftToe11]) &&
                   veq(r.b.part(6).pos, lerp(N[kRightLeg], N[kRightFoot], 0.75f)) && veq(r.b.part(7).pos, N[kRightToe11]);
        bool open = true;
        for (int i = 0; i < 8; ++i) open = open && r.b.part(i).def == 998 && !r.b.part(i).armor;
        check(pos && open, "D §2.2 / C1: part centres (base table {2,2,0,0,5,5,3,3}, k 0.275 / 2 / 0.1 / 0.75); open = DEF 998, armor 0");
        auto state = [&](int i) {                                  // 0 open, 1 closed, 2 sunk
            const Boss08Part& p = r.b.part(i);
            return p.pos.y < -500 ? 2 : p.armor && p.def == 999 ? 1 : p.def == 998 && !p.armor ? 0 : -1;
        };
        auto all = [&](int lo, int hi, int s) { bool ok = true; for (int i = lo; i < hi; ++i) ok = ok && state(i) == s; return ok; };
        r.b.core().exposure = 5; r.b.core().dropped = true; r.step(); const bool exposed = all(0, 8, 1);
        r.b.core().exposure = 0; r.b.core().dropped = false; r.b.core().was_exposed = false; r.b.core().seq = 3; r.b.mesh(kW).move_speed = 1; r.b.mesh(kW).move_target = {0, 500, 0};
        r.step(); const bool flying = all(0, 8, 1);
        r.b.core().seq = 0; r.b.mesh(kW).move_speed = 0;
        r.k().down = true; r.step(); const bool down = all(0, 8, 2);
        r.k().down = false; r.clip(kY, 4); r.step(); const bool unsettled = all(0, 8, 2);
        r.clip(kY, 0); r.k().leg_l_t = 0; r.k().leg_l = 0.5f; r.b.core().weak = 0; r.step();
        const bool group = all(0, 4, 0) && all(4, 6, 2) && all(6, 8, 0);
        r.form(kOnBack); r.k().leg_l_t = 1; r.step();
        check(exposed && flying && down && unsettled && group && all(0, 4, 2) && all(4, 8, 1),
              "D §2.3 truth table: closed (DEF 999, armor) while the core is out; sunk 999 when Down / target 0 / not settled; OnBack 0..3 sunk, 4..7 closed");
    }
    {
        bool ok = true;
        const int kFrames[3] = {34, 41, 45};
        for (int d = 0; d < 3; ++d) {
            Rig r(setup(d)); r.k().cooldown = 99999; r.b.core().weak = 2;
            int n = 0;
            while (r.k().chest_t > 0 && n < 100) { r.b.hit_part(kB, 0, 1); r.step(); ++n; }
            ok = ok && n == kFrames[d];
        }
        check(ok, "D §4.1: the step 0.03 / 0.025 / 0.0225 takes a group target from 1.0 to 0 in 34 / 41 / 45 damage frames");
    }
    {
        Rig r; r.k().cooldown = 99999; r.b.core().weak = 0;
        r.b.hit_part(kB, 4, 1); r.b.hit_part(kB, 4, 7); r.b.hit_part(kB, 4, 3);     // three hits, one part, one frame
        r.step();
        const bool one = nearf(r.k().leg_l_t, 0.97f, 1e-6f) && r.k().smoke[1] >= 1 && r.k().last_group == 1 && r.b.part(4).dmg == 0 &&
                         r.b.mesh(kT).flash == r.b.mesh(kB).flash && nearf(r.b.mesh(kB).flash, 1.65f - 0.075f, 1e-6f);
        r.b.hit_part(kB, 6, 1); r.b.hit_part(kB, 7, 1);            // two parts of one group
        r.step();
        const Boss08MiniGauge& g1 = r.b.mini(1); const Boss08MiniGauge& g2 = r.b.mini(2);
        check(one && nearf(r.k().leg_r_t, 0.94f, 1e-6f) && g1.timer == 255 && nearf(g1.value, 0.97f, 1e-6f) && g1.max == 1.0f &&
              veq(g1.anchor, {r.in.b_node[kLeftFoot].x, r.in.b_node[kLeftFoot].y - 4, r.in.b_node[kLeftFoot].z}) &&
              g2.timer == 255 && nearf(g2.value, 0.94f, 1e-6f) && nearf(g2.anchor.y, r.in.b_node[kRightFoot].y - 4),
              "D C3 / §4.1: one part-frame with damage = one step (hits on two parts = two); smoke counter, flash 1.65, the group's sub-gauge");
        r.b.hit_part(kB, 1, 1); r.step();
        const float y = r.b.mini(0).anchor.y;
        r.in.b_node[kSkirt1].y += 2.0f; r.step();
        check(nearf(y, r.in.b_node[kSkirt1].y - 2.0f - 7.0f) && nearf(r.b.mini(0).anchor.y, y + 2.0f),
              "D §2.1 / §4.1: gauge 0 anchored 4 + 3 under Bone_Skirt1, refreshed every frame while its timer runs");
        Rig h(setup(1)); h.k().cooldown = 99999; h.b.hit_part(kB, 0, 1); h.step();
        Rig dd; dd.k().cooldown = 99999; dd.in.player_hp_mirror = 0; dd.b.hit_part(kB, 0, 1); dd.step();
        Rig z; z.k().cooldown = 99999; z.b.hit_part(kB, 0, 0); z.step();
        check(h.b.mini(0).timer == 0 && nearf(h.k().chest_t, 0.975f, 1e-6f) && dd.k().chest_t == 1 && dd.b.part(0).dmg == 1 &&
              z.k().chest_t == 1 && z.b.mesh(kB).flash == 0,
              "D §4.1 / §2: sub-gauges on Normal and below only; nothing runs while the player is dead; a 0-damage hit is no step");
    }
    {
        Rig r;
        r.b.part(3).pool = 0.5f;
        const Boss08Hit a = r.b.hit_part(kB, 3, 5), b = r.b.hit_part(kB, 2, 5), c = r.b.hit_part(kB, 2, 0), d = r.b.hit_part(kW, 0, 9);
        check(a == Boss08Hit::kRefused && b == Boss08Hit::kLanded && c == Boss08Hit::kZero && d == Boss08Hit::kLanded &&
              r.b.part(2).pool == 99985 && r.b.part(2).dmg == 5 && r.b.part(8).dmg == 9,
              "D §4.3: the hit path: refused when the part's pool < 1; +0x134 accumulates; the pool drains by the damage");
    }
}

static void test_damage_core() {
    {
        Rig r; r.k().cooldown = 99999;
        r.step();
        const bool hidden = r.b.part(8).pos.y < -500 && r.b.part(8).def == 275;
        r.b.core().exposure = 5; r.b.core().dropped = true; r.b.mesh(kW).pos = {1, 1.5f, 2};
        r.b.hit_part(kW, 0, 100);
        const Boss08Output o = r.step();
        check(hidden && veq(r.b.part(8).pos, {1, 1.5f, 2}) && r.b.part(8).def == 275 && r.b.part(8).radius == 1.6f && r.b.core().was_exposed &&
              n_clip(o, kW, 4) == 1 && r.b.mesh(kW).hp == 9899 && nearf(r.b.mesh(kW).flash, 1.65f - 0.075f, 1e-6f) && o.hp_bar,
              "D §2.4 / §4.2: hidden = sunk; exposed = DEF B.DEF, r 1.6; a hit: SetClip(W, 05_Damage), W.HP -= d, flash");
        r.b.hit_part(kW, 0, 20000); r.step();
        check(r.b.mesh(kW).hp == 1.0f && !r.k().dead, "D §4.2: W.HP is floored at 1.0 while the band < 4");
    }
    {
        // the end of an exposure: latch set, counter 0, core alpha 0
        Rig r; r.k().cooldown = 99999; r.b.core().was_exposed = true; r.b.mesh(kW).hp = 0.7f * 9999;
        r.forced = {r15(0), r15(0), r15(2), r15(1)};               // discarded, = current (0), 2, weak 1 -> 2
        r.b.mesh(kW).move_speed = 0;
        r.step();
        const bool one = r.b.core().band == 1 && r.b.core().refill && r.k().form_req == 2 && r.b.core().weak == 2 && r.draws == 4 &&
                         !r.b.core().was_exposed && r.k().cooldown == 300 && r.k().homing_timer == 300 && r.b.tail().tentacle_cd == 300 &&
                         r.b.tail().swing_cd == 300;
        Rig n; n.k().cooldown = 77777; n.b.core().was_exposed = true;         // nothing crossed
        n.forced = {r15(1), r15(1), r15(0)};
        n.step();
        const bool none = n.b.core().band == 0 && !n.b.core().refill && n.k().form_req == 1 && n.b.core().weak == 1 && n.draws == 3 &&
                          n.k().cooldown == 77776;
        Rig a; a.k().cooldown = 99999; a.b.core().was_exposed = true; a.b.core().alpha = 0.3f; a.b.core().alpha_t = 0.3f;
        a.step();
        check(one && none && a.b.core().was_exposed && a.draws == 0,
              "D §3: one band at most (thr > ratio), refill + the four 300s; R(3) discarded, R(3) until != form, R(3) weak; waits for alpha == 0");
        Rig f; f.k().cooldown = 99999; f.b.core().was_exposed = true; f.b.core().band = 3; f.b.mesh(kW).hp = 0.1f * 9999;
        f.forced = {r15(2)};
        f.step();
        Rig g; g.k().cooldown = 99999; g.b.core().was_exposed = true; g.b.core().band = 1; g.b.core().refill = true; g.b.mesh(kW).hp = 10;
        g.fixed = r15(1);
        g.step();
        check(f.b.core().band == 4 && f.k().form_req == 3 && f.draws == 1 && f.b.core().weak == 0 && g.b.core().band == 1 && g.k().cooldown == 300,
              "D §3: band 4 -> form 3 with only the weak-point draw; a running refill skips the band test and re-arms the 300s");
    }
    {
        Rig r(setup(0)); r.form(kOnBack); r.k().cooldown = 99999; r.b.tail().tentacle_cd = 99999;
        r.step();
        const Boss08Part& p = r.b.part(8);
        const bool up = p.def == 998 && p.radius == 4.5f && p.power == 1.0f && veq(p.pos, r.in.t_eye);
        r.in.player_section = 0x13d; r.step(); const bool upb = p.def == 290;
        r.k().window = 500; r.step(); const bool openb = p.def == 280 && p.radius == 2.5f;
        r.in.player_section = 0; r.step();
        check(up && upb && openb && p.def == 295,
              "D §2.4 form 3: barrier up DEF 998 r 4.5 (burst B.DEF + 15); window open B.DEF + 20 r 2.5 (burst + 5)");
    }
    {
        bool ok = true;
        const std::uint32_t kLimit[3] = {60, 70, 80};
        for (int d = 0; d < 3; ++d) {
            Rig r(setup(d)); r.form(kOnBack); r.k().cooldown = 99999; r.b.tail().tentacle_cd = 99999;
            r.step();                                              // the barrier effect appears
            const float hp0 = r.b.mesh(kW).hp;
            int n = 0;
            Boss08Output o;
            while (r.k().window == 0 && n < 200) { r.b.hit_part(kW, 0, 1); o = r.step(); ++n; }
            ok = ok && n == int(kLimit[d]) + 1 && r.b.mesh(kW).hp == hp0 - float(n) && r.k().window == kWindow[d] - 0 && r.k().barrier_hits == 0;
        }
        Rig e(setup(0, 3, true)); e.form(kOnBack); e.k().cooldown = 99999; e.b.tail().tentacle_cd = 99999; e.step();
        int n = 0; while (e.k().window == 0 && n < 200) { e.b.hit_part(kW, 0, 1); e.step(); ++n; }
        check(ok && n == 41 && e.k().window == 1080,
              "D §4.2 / C4 / C6: the break is damage frame limit + 1 (61 / 71 / 81; Easy 41); every hit still costs HP; window 900 / 720 / 600 (+180)");
    }
    {
        Rig r; r.form(kOnBack); r.k().cooldown = 99999; r.b.tail().tentacle_cd = 99999; r.step();
        r.b.hit_part(kW, 0, 1); r.step();
        const Boss08MiniGauge& g = r.b.mini(0);
        const bool cnt = r.k().barrier_hits == 1 && g.timer == 255 && g.value == 59 && g.max == 60 &&
                         veq(g.anchor, {r.in.w_ora.x, r.in.w_ora.y - 7, r.in.w_ora.z});
        r.in.player_section = 0x13d;
        r.b.hit_part(kW, 0, 1); r.step(); const bool b16 = r.k().barrier_hits == 16;
        for (int i = 0; i < 3; ++i) { r.b.hit_part(kW, 0, 1); r.step(); }
        check(cnt && b16 && r.k().barrier_hits == 60 && g.value == 0,
              "D §4.2: the barrier count +1 a damage frame (+14 more under Toal's burst, clamped); gauge 0 = limit - count at OraMarker - 7");
        const int h = r.b.core().h_barrier;
        r.in.player_section = 0;
        r.b.hit_part(kW, 0, 1);
        const Boss08Output o = r.step();
        bool six = n_fx(o, 140) == 6;
        for (int k = 0; six && k < 6; ++k) {
            const Boss08Effect* e = fx(o, 140, k);
            six = e->call == Boss08Call::kFx2 && e->stat == Boss08Stat::kCore && e->parent == Boss08Parent::kCoreOra && e->at_mode == 2 &&
                  e->at_scale.x == 6.0f + float(k) && e->at_scale.z == 6.0f + float(k);
        }
        const Boss08Effect* e514 = fx(o, 514);
        check(h != 0 && has(o.kill_handles, h) && six && e514 && e514->call == Boss08Call::kInline && e514->or_1dc == 0x108 &&
              e514->stat == Boss08Stat::kCore && e514->at_mode == 1 && motion(o, kT, "23_Guard_End") && n_se(o, 320, true) == 1 &&
              n_se(o, 579, true) == 1 && !r.b.mesh(kB).touch && !r.b.mesh(kB).collide && r.k().window == 900 &&
              r.b.core().h_barrier == 0 && r.draws == 0,
              "D §4.2.1 / C5: the break: kill the barrier fx, six FX2 140 (scale 6..11, mode 2), inline 514 (|= 0x108), 23_Guard_End, SE2 320 + 579, body disarmed");
    }
    {
        Rig r; r.form(kOnBack); r.k().cooldown = 99999; r.k().barrier_hits = 60; r.in.flag156 = true;
        r.b.core().h_barrier = 0; r.b.tail().form = 0;             // (no tail decision: no barrier effect spawned)
        r.b.hit_part(kW, 0, 1);
        const Boss08Output o = r.step();
        check(n_fx(o, 140) == 0 && n_fx(o, 514) == 0 && n_se(o, 320, true) == 0 && r.k().window == 900 && !r.b.mesh(kB).touch,
              "D C5: with the barrier handle 0 the FX / Guard_End / SE block is skipped; the window, DEF and disarm writes still run");
    }
    {
        Rig r; r.form(kOnBack); r.k().cooldown = 99999; r.k().window = 500; r.b.core().band = 4; r.b.mesh(kW).hp = 50;
        r.b.tail().swing_cd = 99999;
        r.step();
        r.b.hit_part(kW, 0, 60);
        const Boss08Output o = r.step();
        check(r.k().dead && r.b.mesh(kW).hp == 0 && flag(o, 215, 0) && motion(o, kB, "30_OnBackWait") && motion(o, kT, "19_OnBackWait") &&
              grey_flash(o) && n_clip(o, kW, 4) == 0 && o.hp_bar && o.hp_bar->hp == 0,
              "D §4.2.2 the kill (band 4, HP < 1): db+0x20c, flag 215 = 0, W.HP = 0, B 30_OnBackWait, T 19_OnBackWait, grey Fade 1/30");
    }
}

static void test_death() {
    {
        Rig r; r.form(kOnBack); r.k().dead = true; r.in.flag156 = false; r.b.mesh(kW).hp = 0;
        r.k().thunder_l_t = r.k().thunder_r_t = 1; r.b.mini(0).timer = 255; r.b.core().h_barrier = 7; r.b.mesh(kW).alpha = 0.2f;
        const Boss08Output o = r.step();
        const Boss08Effect* e = fx(o, 140);
        check(r.k().thunder_l_t == 0 && r.b.mini(0).timer == 0 && has(o.kill_handles, 7) && n_fx(o, 140) == 6 && e->stat == Boss08Stat::kBody &&
              fx(o, 514)->stat == Boss08Stat::kBody && motion(o, kT, "23_Guard_End") && n_se(o, 579, true) == 1 && r.k().window == 9999 &&
              r.b.mesh(kW).alpha == 1.0f && o.kill_bit_particles && flag(o, 227, 1) && flag(o, 529, 0) && flag(o, 532, 0) &&
              r.k().death_t == 0 && !r.k().death_started && !flag(o, 194, 0),
              "D §5 story wait: thunders / gauge 0 off, the barrier broken with stat B, W+0x418 = 1, window 9999, Bit particles killed, flag 227 = 1, 529..532 = 0");
        const Boss08Output o2 = r.step();
        r.in.player_hp_mirror = 0;
        const Boss08Output o3 = r.step();
        check(flag(o2, 227, 1) && n_fx(o2, 140) == 0 && !flag(o3, 227, 1) && o3.kill_bit_particles,
              "D §5: the wait block repeats every frame (227 = 1 again); a dead player: no flag 227");
    }
    {
        Rig r; r.form(kOnBack); r.k().dead = true; r.in.flag156 = false; r.in.player_pos = {4, -16, 4};
        const Boss08Output o = r.step();
        check(o.player.pos && veq(*o.player.pos, {0, 0.95f, 0}) && !o.fader.empty() && *o.fader[0].colour == 0xffffff,
              "D §5: a falling player (y < -15, alive) is put back at (0, 0.95, 0) with a white Fade");
    }
    {
        Rig r; r.form(kOnBack); r.k().dead = true; r.in.flag156 = false; r.in.flag227 = 2; r.b.mesh(kW).hp = 0;
        const Boss08Output o = r.step();
        const Boss08Effect* e = fx(o, 609);
        check(r.k().death_started && r.k().death_t == 1 && n_se(o, 186, true) == 1 && o.death_light && flag(o, 194, 0) && e &&
              e->call == Boss08Call::kFx2 && e->src == 1 && veq(e->pos, r.b.mesh(kB).pos) && has(o.rumble, 5) && !flag(o, 227, 1) &&
              !o.kill_bit_particles && *o.fader.back().colour == 0xffffff && bits(*o.fader.back().step) == 0x3c088889u,
              "D §5 release (flag 227 >= 2), t = 0: SE2 186, the light, white Fade 1/120, FX2 609 (src 1) at B.pos, Rumble(5), flag 194 = 0");
        std::vector<Boss08Output> v = r.run(200);
        const bool pre = r.b.mesh(kB).alpha == 1.0f && r.k().death_t == 201;
        int rum = 0; for (const auto& x : v) rum += has(x.rumble, 5);
        const Boss08Output f1 = r.step();                          // t = 201: the first fade frame
        const bool first = nearf(r.b.mesh(kB).alpha, 0.995f, 1e-6f) && nearf(r.b.mesh(kT).alpha, 0.005f, 1e-6f) && !f1.gauge_hide;
        r.step();
        const bool second = nearf(r.b.mesh(kT).alpha, 0.005f - 0.99f, 1e-5f) && r.b.mesh(kB).alpha_target == r.b.mesh(kB).alpha;
        v = r.run(198);
        const bool not_yet = r.b.mesh(kB).alpha > 0 && !v.back().gauge_hide && !v.back().kill_record;
        const Boss08Output z = r.step();                           // t = 401
        const Boss08Output z2 = r.step();
        check(pre && rum == 3 && first && second && not_yet && r.b.mesh(kB).alpha == 0 && z.gauge_hide && z.kill_record && flag(z, 544, 0) &&
              flag(z, 546, 0) && z2.kill_record && !flag(z2, 544, 0) && r.b.mesh(kT).alpha < -90 && !flag(z2, 156, 0),
              "D §5.1: fade frames t 201..401 (-0.005), the tail loses the body's NEW alpha; at 401 HP bar off, flags 544..546 once, KillRecord every frame");
    }
    {
        Boss08Setup s = setup(0, 0); s.boss_rush = true;
        Rig r(s); r.form(kOnBack); r.k().dead = true; r.b.mesh(kW).hp = 0;
        const Boss08Output o = r.step();
        const bool t0 = r.k().death_started && !flag(o, 227, 1) && o.player.iframes_min && *o.player.iframes_min == 9999 &&
                        o.kill_bit_particles && flag(o, 194, 0) && n_fx(o, 609) == 1;
        std::vector<Boss08Output> v = r.run(660);
        bool early = false, kr = false;
        for (const auto& x : v) { early = early || flag(x, 156, 0); kr = kr || x.kill_record || x.kill_bit_particles; }
        const Boss08Output o2 = r.step();
        check(t0 && !early && !kr && flag(o2, 156, 0), "D §5.3 boss rush: no flag 227, i-frames >= 9999, flag 156 = 0 from t 661, no KillRecord");
    }
    {
        Rig r; r.form(kOnBack); r.k().dead = true; r.in.flag156 = false; r.in.flag227 = 2; r.in.player_hp_mirror = 0;
        const Boss08Output o = r.step(); r.step();
        r.in.player_hp_mirror = 5;
        const Boss08Output o2 = r.step();
        check(!o.kill_bit_particles && r.k().death_t == 3 && r.k().death_started && n_se(o, 186, true) == 0 && n_se(o2, 186, true) == 0 &&
              n_fx(o2, 609) == 0 && flag(o, 194, 0),
              "D C7: with the player dead at the release the counter still runs, so the t == 0 one-shots are lost for good");
    }
}

static void test_pickers() {
    {
        const std::uint32_t kLight[4] = {0xffaa8080u, 0xffaaaa80u, 0xff8080aau, 0xffaa80aau};
        bool ok = true;
        for (int f = 0; f < 4; ++f) { Rig r; r.form(f); r.k().cooldown = 99999; const Boss08Output o = r.step(); ok = ok && o.light && *o.light == kLight[f]; }
        Rig d; d.b.mesh(kW).hp = 0.5f; d.k().cooldown = 50;
        const Boss08Output od = d.step();
        check(ok && !od.light && d.k().cooldown == 50, "CE §4.1: the scene light per form; nothing while W.HP < 1");
    }
    {
        Rig r; r.k().cooldown = 99999; r.b.core().exposure = 5; r.b.core().dropped = true; r.k().move_base = 9; r.k().turn_base = 9;
        r.step();
        Rig o; o.form(kOnBack); o.k().cooldown = 99999; o.b.tail().tentacle_cd = 99999; o.b.core().exposure = 5; o.b.core().seq = 2;
        o.step();
        check(r.k().move_base == 0 && r.k().move_base_t == 1 && r.k().turn_base == 0 && r.k().turn_base_t == 1 && r.k().cooldown == 99998 &&
              o.b.core().exposure == 0 && o.b.core().seq == 0 && o.k().turn_base == 1.25f,
              "CE §4.1: core exposed / flying -> pair0 = pair1 = (0, 1); form 3 zeroes w+0x1b4 / w+0x1bc first; OnBack pair1 = (1.25, 1)");
    }
    {
        bool ok = true;
        const std::uint32_t kN[3] = {420, 390, 360}, kO[3] = {510, 450, 360};
        for (int d = 0; d < 3; ++d) {
            Rig r(setup(d)); r.k().cooldown = 99999; r.b.mesh(kW).hp = 0.39f * r.b.mesh(kW).max_hp;
            r.step(); ok = ok && r.k().homing_req && r.k().homing_timer == kN[d];
            Rig o(setup(d)); o.form(kOnBack); o.k().cooldown = 99999; o.b.tail().tentacle_cd = 99999; o.b.mesh(kW).hp = 0.39f * o.b.mesh(kW).max_hp;
            o.step(); ok = ok && o.k().homing_timer == kO[d];
        }
        Rig e(setup(0, 3, true)); e.k().cooldown = 99999; e.b.mesh(kW).hp = 100; e.step();
        Rig eo(setup(0, 3, true)); eo.form(kOnBack); eo.k().cooldown = 99999; eo.b.tail().tentacle_cd = 99999; eo.b.mesh(kW).hp = 100; eo.step();
        Rig hi; hi.k().cooldown = 99999; hi.b.mesh(kW).hp = 0.4f * 9999 + 1; hi.step();
        Rig t; t.k().cooldown = 99999; t.b.mesh(kW).hp = 100; t.k().homing_timer = 2; t.step();
        Rig u; u.k().cooldown = 99999; u.b.mesh(kW).hp = 100; u.clip(kY, 4); u.step();
        check(ok && e.k().homing_timer == 450 && eo.k().homing_timer == 570 && !hi.k().homing_req && !t.k().homing_req &&
              t.k().homing_timer == 1 && !u.k().homing_req,
              "CE §4.1 / B1 B8: ratio < 0.4, settled, timer 0 -> db+0x54 = 1; re-arm 420 / 390 / 360 (+30), form 3 510 / 450 / 360 (+60)");
    }
    // ---- Normal
    auto normal = [](std::uint32_t roll, int diff = 0, bool easy = false) {
        auto r = std::make_unique<Rig>(setup(diff, 3, easy));
        r->k().cooldown = 0; r->forced = {r15(roll), r15(7)};
        return r;
    };
    {
        auto g = normal(0); g->k().cooldown = 2;
        g->step(); const bool dec = g->k().cooldown == 1 && g->draws == 0;
        auto c = normal(0); c->at(kB, 1, 70.0f); c->step();
        auto s = normal(0); s->clip(kY, 4); s->step();
        check(dec && c->draws == 0 && s->draws == 0 && s->k().cooldown == 0,
              "CE §4.2 gates: the wait clip, the cooldown (decrement and return), db+0x5c == 1");
    }
    {
        auto l = normal(0); l->in.player_pos = {2, 0, -10};
        const Boss08Output ol = l->step();
        auto r = normal(0); r->in.player_pos = {-2, 0, -10};
        const Boss08Output orr = r->step();
        auto x = normal(0); x->in.player_pos = {2, 0, -10}; x->k().hand_l = 0; x->k().hand_l_t = 0;
        const Boss08Output ox = x->step();
        check(n_clip(ol, kB, 1) == 1 && l->k().move_over == 0 && l->k().move_over_t == 150 && l->k().cooldown == 187 && l->draws == 2 &&
              n_clip(orr, kB, 3) == 1 && ox.clips.empty() && x->k().cooldown == 187 && x->draws == 2,
              "CE §4.2 roll 0: P.x > 0 -> left swing else right (leg + hand alpha > 0), pair2 = (0, 150); the cooldown R(60) + 180 even on a no-op");
    }
    {
        auto a = normal(1); a->b.core().weak = 2; a->step();
        auto b = normal(1); b->b.core().weak = 1; b->step();
        auto c = normal(1); c->b.core().weak = 0; c->step();
        auto d = normal(1); d->b.core().weak = 1; d->k().leg_r_t = 0; d->step();
        auto e = normal(1); e->b.core().weak = 0; e->k().beat_l = 50; e->step();
        check(a->k().beat_l == 1 && a->k().move_over_t == 120 && b->k().beat_r == 1 && c->k().clap == 1 && d->k().beat_r == 0 &&
              e->k().clap == 0 && a->sec[kB] == 0,
              "CE §4.2 roll 1 / B23 §6.4: weak 2 -> LeftBeat, 1 -> RightBeat, 0 -> Clap (leg targets > 0, no timer running); the body keeps its wait");
    }
    {
        auto a = normal(2); const Boss08Output oa = a->step();
        auto b = normal(3); b->step();
        auto c = normal(4); const Boss08Output oc = c->step();
        check(n_clip(oa, kB, 5) == 1 && a->k().breath == 0 && a->k().move_over_t == 360 && b->k().breath == 1 && n_clip(oc, kB, 35) == 1 &&
              motion(oc, kT, "27_DoubleLariat") && c->k().move_over_t == 180 && c->k().turn_over == 0 && c->k().turn_over_t == 180,
              "CE §4.2 rolls 2 / 3 / 4: the breath variants 0 / 1 (pair2 360); DoubleLariat + the tail's, pair2 = pair3 = (0, 180)");
    }
    {
        auto a = normal(5); a->b.core().weak = 2; const Boss08Output oa = a->step();
        auto b = normal(5); b->b.core().weak = 1; const Boss08Output ob = b->step();
        auto c = normal(5); c->b.core().weak = 0; const Boss08Output oc = c->step();
        check(n_clip(oa, kB, 4) == 1 && a->k().thunder_r_t == 1 && a->k().turn_over_t == 120 && n_clip(ob, kB, 2) == 1 && b->k().thunder_l_t == 1 &&
              n_clip(oc, kB, 5) == 1 && c->k().breath == 0x63 && c->k().turn_over == 4.0f && c->k().turn_over_t == 360 && c->k().move_over_t == 360,
              "CE §4.2 roll 5: weak 2 -> 05_RightHandRaise (+0xbc = 1), 1 -> 03_LeftHandRaise (+0xc0 = 1), 0 -> the cannon (0x63, pair3 (4.0, 360))");
    }
    {
        auto h = normal(2, 1), n = normal(2, 2), e = normal(2, 0, true);
        h->step(); n->step(); e->step();
        check(h->k().cooldown == 147 && n->k().cooldown == 107 && e->k().cooldown == 247, "CE §4.6: the cooldown base 180 / 140 / 100 (+60 on Easy) + R(60)");
    }
    // ---- Low
    auto low = [](std::uint32_t roll) {
        auto r = std::make_unique<Rig>();
        r->form(kLow); r->k().cooldown = 0; r->forced = {r15(roll), r15(0)};
        return r;
    };
    {
        bool ok = true;
        for (std::uint32_t i = 0; i < 4; ++i) { auto r = low(i); const Boss08Output o = r->step(); ok = ok && n_clip(o, kB, 11 + int(i)) == 1 && r->k().cooldown == 180; }
        auto j = low(4); const Boss08Output oj = j->step();
        auto k = low(4); k->k().jump_lock = 5; const Boss08Output ok2 = k->step();
        auto l = low(0); l->k().low_latch = true; const Boss08Output ol = l->step();
        check(ok && n_clip(oj, kB, 10) == 1 && motion(oj, kT, "04_Jump") && j->k().jump_lock == 599 && ok2.clips.empty() && k->k().jump_lock == 4 &&
              k->k().cooldown == 180 && ol.clips.empty() && !l->k().low_latch && l->k().jump_lock == 599 && l->draws == 0,
              "CE §4.3: rolls 0..3 -> clips 11..14; 4 -> 11_Jump + tail 04_Jump unless locked; the +0x74 latch arms the 600 lock; TAIL counts it down");
    }
    // ---- High
    auto high = [](std::uint32_t roll) {
        auto r = std::make_unique<Rig>();
        r->form(kHigh); r->k().cooldown = 0; r->forced = {r15(roll), r15(0)};
        return r;
    };
    {
        auto a = high(0); const Boss08Output oa = a->step();
        auto b = high(1); const Boss08Output ob = b->step();
        auto c = high(2); const Boss08Output oc = c->step();
        auto d = high(3); const Boss08Output od = d->step();
        auto e = high(4); const Boss08Output oe = e->step();
        auto f = high(5); const Boss08Output of = f->step();
        check(n_clip(oa, kB, 20) == 1 && a->k().turn_base == 2.0f && a->k().turn_base_t == 1 && n_clip(ob, kB, 23) == 1 &&
              b->k().turn_base == 4.0f && b->k().turn_base_t == 180 && n_clip(oc, kB, 24) == 1 && motion(od, kT, "08_LeftRotate_Start") &&
              od.clips.empty() && motion(oe, kT, "11_RightRotate_Start") && motion(of, kT, "14_Right&LeftRotate_Start") && f->sec[kT] == 13,
              "CE §4.4: rolls 0 / 1 / 2 -> RotateSpear / MachineGunStab (pair1 (4.0, 180)) / QuakeTheWing; 3 / 4 / 5 -> the tail's rotates");
        auto g = high(0); g->clip(kT, 8); g->k().cooldown = 9; g->step();
        auto u = high(0); u->clip(kY, 4); u->step();
        check(g->k().cooldown == 9 && g->draws == 0 && u->k().turn_over == 1.0f && u->k().turn_over_t == 1 && u->k().turn_base_t == 0,
              "CE §4.4 / C10: the tail must be in 07_HighWait (the gate sits before the cooldown); unsettled: the override pair3 = (1.0, 1)");
    }
    // ---- OnBack
    auto onback = [](std::uint32_t roll, std::uint32_t window = 0) {
        auto r = std::make_unique<Rig>();
        r->form(kOnBack); r->k().cooldown = 0; r->k().window = window; r->b.tail().tentacle_cd = 99999; r->b.tail().swing_cd = 99999;
        r->forced = {r15(roll), r15(0)};
        return r;
    };
    {
        auto a = onback(0); const Boss08Output oa = a->step();
        auto b = onback(1); const Boss08Output ob = b->step();
        auto c = onback(2); const Boss08Output oc = c->step();
        auto d = onback(3); const Boss08Output od = d->step();
        auto e = onback(4); const Boss08Output oe = e->step();
        auto f = onback(4, 300); const Boss08Output of = f->step();
        auto g = onback(4, 300); g->b.tail().laser = 50; g->step();
        check(n_clip(oa, kB, 30) == 1 && a->k().thunder_l_t == 1 && a->k().thunder_r_t == 0 && n_clip(ob, kB, 31) == 1 && b->k().thunder_l_t == 0 &&
              n_clip(oc, kB, 32) == 1 && c->k().thunder_r_t == 1 && n_clip(od, kB, 33) == 1 && n_clip(oe, kB, 34) == 1 &&
              e->k().thunder_l_t == 1 && e->k().thunder_r_t == 1 && n_clip(of, kB, 34) == 0 && f->b.tail().laser == 2 && g->b.tail().laser == 51 &&
              f->k().cooldown == 180,
              "CE §4.5 / C9: rolls 0..3 -> thunders / points (+0xc0 / +0xbc); 4: barrier up -> 35_DoubleHandThunder (both), down -> the eye laser");
    }
}

static void test_bank_hands() {
    auto rig = [](int diff = 0, int chr = 3) { auto r = std::make_unique<Rig>(setup(diff, chr)); r->in.flag156 = false; r->in.player_pos = {4, 0, -6}; return r; };
    {
        auto r = rig();
        Boss08Output o = r->bank(1, 61.0f);
        const Boss08Mesh& B = r->b.mesh(kB);
        const bool lunge = vnear(B.move_target, {0.9f * 4, 0, ((-6 + 8.5f) - 8.5f) * 0.9f + 8.5f}) && B.move_radius == 0.2f;
        o = r->bank(1, 72.0f);
        const Boss08Effect* t = fx(o, 450);
        bool swing = t && t->parent == Boss08Parent::kBody && t->flag201 && t->at_mode == 1 && n_se(o, 332) == 1 && n_fx(o, 490) == 3;
        const int kN[3] = {kLeftToe31, kLeftFoot, kLeftLeg};
        for (int i = 0; swing && i < 3; ++i) {
            const Boss08Effect* h = fx(o, 490, i);
            swing = h->parent == Boss08Parent::kBodyNode && h->parent_index == kN[i] && h->knock == 10.0f && h->rtype == 2 && h->power == 0 &&
                    h->flag201 && h->stat == Boss08Stat::kBody;
        }
        o = r->bank(1, 76.0f);
        const bool hit = shake_is(o, 30, 0.1f, 0.1f) && zoom_is(o, 0.1f, -0.001f) && o.player.ext && veq(o.player.ext->v, {0, 0, -0.1f}) &&
                         o.player.ext->ticks == 30 && o.bullets.empty();
        o = r->bank(1, 80.0f);
        check(lunge && swing && hit && veq(B.move_target, {0, B.move_target.y, 8.5f}) && B.move_radius == 0.05f && o.bullets.empty() &&
              kLeftToe31 == 18 && kLeftFoot == 6 && kLeftLeg == 5,
              "B1 §2 clip 1: f61 lunge 90 % toward (P.x, P.z + 8.5) @ 0.2; f72 FX 450 on B + SE 332 + 3 x 490 (nodes 18 / 6 / 5, knock 10, rtype 2); f76 Shake / Zoom / Push; f80 home @ 0.05");
    }
    {
        auto r = rig(2);
        Boss08Output o = r->bank(3, 182.0f);
        const bool mir = n_fx(o, 451) == 1 && fx(o, 490, 0)->parent_index == 17 && fx(o, 490, 1)->parent_index == 4 && fx(o, 490, 2)->parent_index == 3;
        o = r->bank(3, 186.0f);
        const bool pt = o.player.ext && o.player.ext->v.z == -0.2f;
        o = r->bank(3, 190.0f);
        auto a = rig(2, 0);
        Boss08Output oa = a->bank(1, 76.0f);
        const bool po = oa.player.ext->v.z == -0.14f;
        oa = a->bank(1, 80.0f);
        auto h = rig(1, 0);
        check(mir && pt && n_bul(o, 0x25, 467) == 4 && n_se(o, 336, true) == 1 && po && n_bul(oa, 0x25, 467) == 2 &&
              h->bank(1, 80.0f).bullets.empty() && h->bank(1, 76.0f).player.ext->v.z == -0.12f,
              "B1 §2 clip 3 mirror (451; nodes 17 / 4 / 3); Push Toal -0.1 / -0.15 / -0.2, others -0.1 / -0.12 / -0.14; NM only: Bits(4) (Toal 4, others 2)");
    }
    {
        auto r = rig(); r->k().fc = 12;
        r->forced.assign(32, kU50); r->forced[0] = kU25; r->forced[1] = kU75;
        Boss08Output o = r->bank(2, 126.0f);
        const Boss08Bullet* b0 = bul(o, 7, 519);
        const Vec3 p{r->in.b_node[kLeftToe23Mark].x, 1.5f, r->in.b_node[kLeftToe23Mark].z};
        const float a = float(std::atan2(double(-6 - p.z), double(4 - p.x))) + ((0.25f * 360.0f) - 180.0f) * kDeg;
        const float f1 = 0.003f - 0.75f * fbits(0x3b03126eu);
        const bool ring = n_bul(o, 7, 519) == 16 && r->draws == 32 && b0->raw && veq(b0->pos, p) &&
                          vnear(b0->dir, {float(std::cos(double(a))), 0, float(std::sin(double(a)))}, 1e-6f) && b0->f[0] == 0 &&
                          bits(b0->f[1]) == bits(f1) && b0->f[3] == -10.0f && b0->f[4] == 1.0f && bits(b0->f[7]) == 2 && b0->flag201;
        auto m = rig(2); m->k().fc = 6; m->fixed = kU50;
        Boss08Output om = m->bank(4, 239.75f);
        const bool nm = n_bul(om, 7, 519) == 32 && m->draws == 64 && bits(om.bullets[0].f[1]) == bits(0.0035f - 0.5f * fbits(0x3b03126fu)) &&
                        om.bullets[0].pos.x == m->in.b_node[kRightToe23Mark].x;
        auto g = rig(); g->k().fc = 12;
        const bool strict = g->bank(2, 125.0f).bullets.empty() && g->bank(2, 130.0f).bullets.empty();
        g->k().fc = 13;
        check(ring && nm && strict && g->bank(2, 127.0f).bullets.empty(),
              "B1 §3: 125 < f < 130 and db+0x70 % 6 == 0: 16 (NM 32) raw kind-7 519 from the hand at y 1.5, 2 draws each; f1 = 0.003 - u*0.002 (NM 0.0035, k one bit off)");
        Boss08Output ol = g->bank(2, 125.0f), orr = g->bank(4, 235.0f);
        const Boss08Effect *el = fx(ol, 459), *er = fx(orr, 459);
        check(n_se(ol, 356) == 1 && el && el->parent == Boss08Parent::kBody && veq(el->at_off, {3, 0, -6.5f}) && el->flag201 &&
              shake_is(ol, 12, 0.25f, 0.25f) && grey_flash(ol) && er && veq(er->at_off, {-3, 0, -6.5f}),
              "B1 §3 / B3: f == 125 [235]: SE 356, FX 459 on the BODY at (+-3, 0, -6.5), Shake(12, 0.25), Fade(0xaaaaaa, 1/30)");
    }
    {
        auto r = rig(); r->k().breath = 0; r->k().fc = 8; r->fixed = kU50;
        Boss08Output o = r->bank(6, 305.0f);
        const Boss08Bullet* b0 = bul(o, 2, 518);
        Vec3 p = r->in.b_node[kBerryMouthMark]; p.y -= 1.5f;
        const float acc = 0.5f * fbits(0x3a03126cu) - fbits(0x3b1374bcu), spd = 0.5f * fbits(0x3d4cccccu) + fbits(0x3e6b851fu);
        const bool fan = n_bul(o, 2, 518) == 6 && r->draws == 13 && b0->raw && veq(b0->pos, p) && vnear(b0->dir, {0, 0, -1}, 1e-6f) &&
                         bits(b0->f[0]) == bits(spd) && bits(b0->f[1]) == bits(acc) && b0->f[3] == -5.0f && b0->f[7] == 0;
        r->k().fc = 9;
        const bool skip = r->bank(6, 305.0f).bullets.empty();
        auto e = rig(); e->k().breath = 0; e->k().fc = 0; e->forced = {kU50, 0u, kU50};     // u = 0: the -115 deg edge
        o = e->bank(6, 305.0f);
        const float a = -kHalfPi + (0.0f - 115.0f) * kDeg;
        const bool edge = vnear(o.bullets[0].dir, {float(std::cos(double(a))), 0, float(std::sin(double(a)))}, 1e-5f);
        auto n = rig(2); n->k().breath = 0; n->k().fc = 4; n->forced = {kU25, kU50, r15(0)}; n->fixed = kU50;
        o = n->bank(6, 305.0f);
        const bool nm = n_bul(o, 2, 515) == 12 && n->draws == 27 && bits(o.bullets[0].f[1]) == bits(0.5f * 0.0035f - fbits(0x3b8b4396u));
        auto n2 = rig(2); n2->k().breath = 0; n2->k().fc = 4; n2->forced = {kU25, kU50, r15(1)}; n2->fixed = kU50;
        check(fan && skip && edge && nm && n_bul(n2->bank(6, 305.0f), 2, 518) == 12,
              "B1 §4.2 fan: every 4th tick 6 (NM 12) raw kind-2 518 about world -z +-115 deg; 13 (27) draws; NM: 1 in 3 volleys is 515");
        auto c = rig(); c->k().breath = 1; c->k().fc = 2; c->forced = {kU50, 0u, kU50}; c->fixed = kU50;
        o = c->bank(6, 305.0f);
        const float ac = -kHalfPi + (0.0f - 30.0f) * kDeg;
        const bool cone = n_bul(o, 2, 518) == 3 && c->draws == 7 && vnear(o.bullets[0].dir, {float(std::cos(double(ac))), 0, float(std::sin(double(ac)))}, 1e-5f);
        c->k().fc = 3;
        auto cn = rig(2); cn->k().breath = 1; cn->k().fc = 2; cn->fixed = kU50;
        const Boss08Output ocn = cn->bank(6, 305.0f);
        check(cone && c->bank(6, 305.0f).bullets.empty() && ocn.bullets.size() == 6 && cn->draws == 15,
              "B1 §4.3 cone: every 2nd tick 3 (NM 6) within +-30 deg; 7 (15) draws");
    }
    {
        bool ok = true;
        const float kT[3] = {0.075f, 0.0915f, 0.108f}, kO[3] = {0.025f, 0.0305f, 0.036f};
        for (int d = 0; d < 3; ++d) {
            auto t = rig(d, 3); t->k().breath = 0x63; t->in.player_pos = {4, 0, -6};
            auto a = rig(d, 1); a->k().breath = 0x63; a->in.player_pos = {-4, 0, -6};
            const Boss08Output ot = t->bank(6, 305.0f), oa = a->bank(6, 305.0f);
            ok = ok && nearf(ot.player.knock2.x, -kT[d], 2e-5f) && ot.player.knock2.z == 0 && nearf(oa.player.knock2.x, kO[d], 2e-5f) &&
                 ot.bullets.empty() && t->draws == 0;
        }
        auto dd = rig(); dd->k().breath = 0x63; dd->in.player_hp = 0.5f;
        check(ok && dd->bank(6, 305.0f).player.knock2.x == 0,
              "B1 §4.1 the cannon's pull toward x = 0: Toal 0.075 / 0.0915 / 0.108 a tick, others x0.3333; not on a dead player");
    }
    {
        auto r = rig(); r->k().breath = 0x63;
        Boss08Output o = r->bank(5, 292.0f);
        const Boss08Effect* g = fx(o, 460);
        const int h = r->k().h_beam;
        const bool beam = g && g->parent == Boss08Parent::kBodyNode && g->parent_index == kBerryMouthMark && g->flag201 && h != 0 &&
                          g->handle == h && n_se(o, 331) == 1;
        o = r->bank(5, 296.0f);
        const bool z = zoom_is(o, 0.5f, -0.001f) && has(o.rumble, 5);
        o = r->bank(5, 302.0f);
        const bool s = shake_is(o, 300, 0.3f, 0.3f) && n_se(o, 312) == 0 && o.bullets.empty();
        o = r->bank(6, 304.0f); const bool keep6 = o.kill_handles.empty();
        o = r->bank(7, 312.0f); const bool keep7 = o.kill_handles.empty();
        o = r->bank(0, 12.0f);
        check(beam && z && s && keep6 && keep7 && has(o.kill_handles, h) && r->k().h_beam == 0,
              "B1 §4.4 variant 0x63: f292 FX 460 on BerryMouthMark (handle) + SE 331; f296 ZoomBlur 0.5 + Rumble 5; f302 Shake 300; killed outside clips 5..7");
        auto v1 = rig(); v1->k().breath = 1;
        const Boss08Output o1 = v1->bank(5, 302.0f);
        auto v0 = rig(); v0->k().breath = 0;
        const Boss08Output o0 = v0->bank(5, 302.0f);
        auto a1 = rig(0, 0); a1->k().breath = 1;
        check(n_bul(o1, 0x25, 467) == 6 && n_se(o1, 312) == 1 && !o1.shake && o0.bullets.empty() && n_se(o0, 312) == 1 &&
              n_bul(a1->bank(5, 302.0f), 0x25, 467) == 4,
              "B1 §4.4 / B5: f302: variant 1 Bits(6) (Toal 6, others 4) + SE 312; variant 0 SE 312 only");
    }
}

static void test_bits_homing() {
    {
        Rig r; r.in.flag156 = false; r.k().breath = 1; r.in.bits_deployed = 7;
        const Boss08Output o = r.bank(5, 302.0f);
        r.in.bits_deployed = 6;
        r.forced = {r15(3), r15(0), r15(4), r15(1), r15(2), r15(0)};
        const Boss08Output o2 = r.bank(5, 302.0f);
        const Boss08Bullet* b0 = bul(o2, 0x25, 467);
        const Boss08Bullet* b1 = bul(o2, 0x25, 467, 1);
        const Boss08Bullet* b2 = bul(o2, 0x25, 467, 2);
        check(o.bullets.empty() && n_se(o, 336, true) == 0 && r.k().bit_layout == 1 && n_bul(o2, 0x25, 467) == 6 && r.draws == 6 &&
              veq(b0->pos, {15, 20, 0}) && veq(b0->dir, {0, -1, 0}) && bits(b0->f[0]) == 0x3e99999au && bits(b0->f[1]) == 0xba83126fu &&
              b0->f[3] == -6.0f && bits(b0->f[5]) == 487 && bits(b0->f[6]) == 3 && bits(b0->f[7]) == 4 && bits(b0->f[8]) == 0x3e97e9d8u &&
              b0->f[9] == 1.0f && b0->f[10] == 2.0f && b0->f[11] == 60.0f && !b0->raw && b0->flag201 &&
              bits(b0->f[12]) == ((135u << 16) | (132u << 8) | 123u) &&          // 7.5 -> 7, 1.5 + 3 -> 4, -5
              bits(b1->f[12]) == ((120u << 16) | (129u << 8) | 123u) &&          // -7.5 -> -8 (trunc of 120.5), r 0
              bits(b2->f[12]) == ((135u << 16) | (133u << 8) | 118u),
              "B1 §5: no call spawns with 7+ Bits deployed; kind 0x25 / 467 block (0.3, -0.001, power -6, trail 487 / 3, homing 4, 17 deg, 1 / 2 / 60); packed byte target, one R(5) each");
        r.fixed = r15(0);
        const Boss08Output o3 = r.bank(5, 302.0f);
        const Boss08Bullet* c0 = bul(o3, 0x25, 467);
        check(veq(c0->pos, {10, 20, 0}) && bits(c0->f[12]) == ((133u << 16) | (129u << 8) | 124u) && r.k().bit_layout == 0,
              "B1 §5: the layout toggles on every call that passed the cap (inner columns 10 / 5.5, rows -4 / -8 / 0)");
        Rig l; l.in.flag156 = false; l.form(kLow); l.fixed = r15(0);
        const Boss08Output ol = l.bank(11, 480.0f);
        Rig ob; ob.in.flag156 = false; ob.form(kOnBack); ob.fixed = r15(0);
        const Boss08Output oo = ob.bank(34, 1316.0f);
        check((bits(bul(ol, 0x25, 467)->f[12]) & 0xff) == 128 - 17 && (bits(bul(oo, 0x25, 467)->f[12]) & 0xff) == 128 - 8 &&
              (bits(bul(oo, 0x25, 467, 2)->f[12]) & 0xff) == 128 - 13,
              "B1 §5: the target z shifts -12 in Low, -3 in OnBack (homed-on -17 / -8 / -13)");
    }
    {
        Rig r; r.in.flag156 = false; r.k().homing_req = true; r.in.player_pos = {3.7f, 0.2f, -6.4f};
        r.forced = {r15(1), r15(4), r15(2)};
        const Boss08Output o = r.step();
        const Boss08Bullet *a = bul(o, 0x25, 484), *b = bul(o, 0x25, 484, 1);
        check(!r.k().homing_req && n_se(o, 350, true) == 1 && r.draws == 3 && n_bul(o, 0x25, 484) == 2 && veq(a->pos, r.in.b_node[kLStigma2]) &&
              veq(b->pos, r.in.b_node[kRStigma2]) && veq(a->dir, {0.5f, 0.5f, 0}) && veq(b->dir, {-0.5f, 0.5f, 0}) &&
              bits(a->f[0]) == 0x3f99999au && bits(a->f[1]) == 0xba83126fu && a->f[3] == -6.0f && bits(a->f[5]) == 483 && bits(a->f[6]) == 2 &&
              bits(a->f[7]) == 2 && bits(a->f[8]) == 0x3e97e9d8u && a->f[11] == 60.0f &&
              bits(a->f[12]) == ((131u << 16) | (132u << 8) | 121u) && bits(b->f[12]) == ((131u << 16) | (130u << 8) | 121u),
              "B1 §7 the homing volley: latch cleared, SE2 350, R(3) picks a stigma pair (9,12 / 10,13 / 11,14), un-normalised (+-0.5, 0.5, 0), R(5) in the packed y");
        const Boss08Output o2 = r.step();
        check(o2.bullets.empty(), "B1 §7: one volley per request");
    }
}

static void test_bank_spear() {
    auto rig = [](int diff = 0, int chr = 3, int form = kLow) {
        auto r = std::make_unique<Rig>(setup(diff, chr)); r->in.flag156 = false; r->form(form); r->in.player_pos = {3, 0, 4.5f}; r->fixed = r15(0);
        return r;
    };
    {
        auto r = rig();
        Boss08Output o = r->bank(11, 476.0f);
        const bool tr = fx(o, 452) && fx(o, 452)->parent == Boss08Parent::kBody && fx(o, 452)->flag201 && n_se(o, 342) == 1;
        o = r->bank(11, 480.0f);
        const bool hit = shake_is(o, 36, 0.1f, 0.1f) && zoom_is(o, 0.1f, -0.001f) && veq(o.player.ext->v, {0, 0, -0.1f}) && n_bul(o, 0x25, 467) == 2;
        o = r->bank(12, 513.0f);
        const bool tr2 = n_fx(o, 453) == 1 && n_se(o, 342) == 1;
        auto h = rig(1), n = rig(2), a = rig(2, 0);
        check(tr && hit && tr2 && n_bul(h->bank(12, 517.0f), 0x25, 467) == 4 && n_bul(n->bank(11, 480.0f), 0x25, 467) == 6 &&
              n_bul(a->bank(11, 480.0f), 0x25, 467) == 4,
              "B23 §3.1 / P1: clips 11 / 12: FX 452 / 453 on B + SE 342; hit: Shake(36), ZoomBlur, Push30 (0, 0, pv), Bits(2 / 4 / 6)");
    }
    {
        auto r = rig();
        Boss08Output o = r->bank(13, 554.0f);
        const bool a = n_fx(o, 458) == 1 && n_se(o, 341) == 1;
        o = r->bank(13, 557.0f);
        check(a && shake_is(o, 130, 0.25f, 0.25f) && !o.player.ext && o.bullets.empty(), "B23 §3.2 clip 13: f554 FX 458 + SE 341; f557 Shake(130, 0.25)");
    }
    {
        auto r = rig();
        Boss08Output o = r->bank(14, 606.0f);
        const bool se = n_se(o, 344) == 1;
        o = r->bank(14, 616.0f);
        const Boss08Effect* e = fx(o, 486);
        const int h = r->b.statics().h_darkwave;
        const bool wave = e && e->parent == Boss08Parent::kBody && veq(e->at_off, {0, 0.25f, -11}) && e->handle == h && h != 0 &&
                          shake_is(o, 480, 0.125f, 0.125f) && has(o.rumble, 4);
        o = r->bank(14, 616.0f);                                   // a re-spawn kills the old one first
        const int h2 = r->b.statics().h_darkwave;
        const bool re = has(o.kill_handles, h) && h2 != h;
        o = r->bank(15, 664.0f); const bool keep = o.kill_handles.empty();
        o = r->bank(16, 683.0f);
        check(se && wave && re && keep && has(o.kill_handles, h2) && r->b.statics().h_darkwave == 0 && n_se(o, 364) == 1 && has(o.rumble, 0),
              "B23 §3.3 / P6: f606 SE 344; f616 DarkWave 486 at (0, 0.25, -11) on B (static handle), Shake(480), Rumble 4; killed outside clips 14 / 15; f683 SE 364 + Rumble 0");
    }
    {
        auto r = rig(); r->in.player_pos = {3, 0, 4.5f};           // B at (0, 0, 8.5): d = (0.6, 0, -0.8)
        Boss08Output o = r->bank(10, 443.0f);
        const Boss08Effect* e = fx(o, 516);
        auto a = rig(2, 0);
        check(n_se(o, 343) == 1 && shake_is(o, 36, 0.3f, 0.3f) && e && veq(e->pos, r->b.mesh(kB).pos) && e->power == -8.0f && !e->flag201 &&
              e->parent == Boss08Parent::kNone && vnear(o.player.ext->v, {0.06f, 0, -0.08f}, 1e-6f) && o.player.ext->ticks == 30 &&
              has(o.rumble, 3) && n_bul(o, 0x25, 467) == 6 && n_bul(a->bank(10, 443.0f), 0x25, 467) == 4,
              "B23 §3.4 / P7 the landing: SE 343, Shake(36, 0.3), FX 516 at B.pos power -8, a RADIAL push away from B, Rumble 3, Bits(6)");
    }
    {
        auto r = rig(0, 3, kHigh);
        Boss08Output o = r->bank(20, 826.0f);
        const int h = r->b.statics().h_rolling;
        const bool spin = fx(o, 478) && fx(o, 478)->handle == h && h != 0 && fx(o, 478)->parent == Boss08Parent::kBody &&
                          n_bul(o, 0x25, 467) == 6 && n_se(o, 347) == 1;
        r->k().fc = 4; r->draws = 0; r->fixed.reset(); r->forced = {kU50, kU50, kU50};
        o = r->bank(21, 830.0f);
        const Boss08Bullet* b = bul(o, 2, 479);
        // a = 0, dir = norm(1, -1.75, 0); pos = B + (9 dir.x, 18, 0); gravity = 0.001875
        const float L = std::sqrt(1.0f + 1.75f * 1.75f);
        const bool rain = n_bul(o, 2, 479) == 1 && r->draws == 3 && !b->raw && vnear(b->dir, {1 / L, -1.75f / L, 0}, 1e-5f) &&
                          vnear(b->pos, {9 / L, 18, 8.5f}, 1e-4f) && bits(b->f[0]) == 0x3d48b439u && b->f[1] == 0.01f &&
                          nearf(b->f[2], 0.001875f, 1e-7f) && b->f[3] == 0 && b->f[9] == 0 && o.kill_handles.empty();
        const bool pull = o.player.ext && o.player.ext->ticks == 1 && vnear(o.player.ext->v, {-0.6f * 0.125f, 0, 0.8f * 0.125f}, 1e-6f);
        r->k().fc = 5;
        o = r->bank(21, 830.0f);
        const bool odd = o.bullets.empty() && o.player.ext;
        o = r->bank(22, 853.0f);
        auto n = rig(2, 0, kHigh); n->k().fc = 2; n->draws = 0; n->fixed = kU50;
        const Boss08Output on = n->bank(21, 830.0f);
        check(spin && rain && pull && odd && has(o.kill_handles, h) && n_bul(on, 2, 479) == 3 && n->draws == 9 &&
              nearf(on.player.ext->v.x, -0.6f * 0.125f * fbits(0x3eaaa64cu), 1e-7f),
              "B23 §4.1 / §4.2 / P4: f826 FX 478 (static handle) + Bits(6) + SE 347; the loop: every even frame 1 (NM 3) kind-2 479, 3 Rf each; a one-tick suction every tick (x0.3333 unless Toal); killed in clip 22");
    }
    {
        auto r = rig(0, 3, kHigh); r->k().fc = 7;
        Boss08Output o = r->bank(23, 877.0f);
        const Boss08Bullet* b = bul(o, 2, 457);
        const Vec3 tip = r->in.b_node[kSpearMark], sp = r->in.b_node[kSpear];
        const float dx = tip.x - sp.x, dz = tip.z - sp.z, L = std::sqrt(dx * dx + dz * dz);
        const bool first = shake_is(o, 100, 0.125f, 0.125f) && n_fx(o, 456) == 1 && fx(o, 456)->parent == Boss08Parent::kBodyNode &&
                           fx(o, 456)->parent_index == kSpearMark && n_se(o, 348) == 1 && b && veq(b->pos, tip) &&
                           vnear(b->dir, {dx / L, 0, dz / L}, 1e-6f) && bits(b->f[0]) == 0x3dc8b439u && bits(b->f[1]) == 0x3ca3d70au &&
                           b->f[2] == 0.01f && b->f[3] == 20.0f && b->f[9] == 1.0f;
        int shots = 0;
        for (float f = 877.0f; f <= 893.0f; f += 0.25f) shots += n_bul(r->bank(23, f), 2, 457);
        r->k().fc = 18;
        o = r->bank(23, 880.0f);
        const bool extra = n_fx(o, 456) == 1 && o.bullets.empty();
        r->k().fc = 18;
        check(first && shots == 9 && extra && r->bank(23, 895.0f).effects.empty() && r->bank(23, 895.0f).bullets.empty(),
              "B23 §4.4 / P3 MachineGunStab: f877 Shake(100) + FX 456 on SpearMark; nine bullet frames 877..893 (kind 2, 457, power 20, f9 1); 456 again when db+0x70 % 18 == 0");
    }
    {
        auto r = rig(0, 3, kHigh); r->k().quake_n = 5; r->k().mgcra = true;
        Boss08Output o = r->bank(24, 925.0f);
        const Boss08Effect* e = fx(o, 489);
        const bool start = e && veq(e->pos, {0, 0.1f, 0}) && e->parent == Boss08Parent::kNone && !e->flag201 && shake_is(o, 660, 0.1f, 0.125f) &&
                           has(o.rumble, 4) && n_se(o, 357) == 1 && o.mgcra_delete && r->k().quake_n == 0 && !r->k().mgcra;
        for (int i = 0; i < 30; ++i) o = r->bank(25, 932.0f, 1);
        const bool wait = r->k().quake_n == 0 && !r->k().mgcra;
        int at = -1;
        for (int i = 1; i <= 25 && at < 0; ++i) { o = r->bank(25, 932.0f, 2); if (o.mgcra_spawn) at = i; }
        o = r->bank(25, 932.0f, 2); const bool once = !o.mgcra_spawn && r->k().mgcra;
        o = r->bank(25, 932.0f, 5); const bool held = !o.mgcra_delete;
        o = r->bank(26, 940.0f);
        check(start && wait && at == 19 && once && held && o.mgcra_delete && !r->k().mgcra && r->k().quake_n == 0,
              "B23 §4.5 / §4.6 / P5: f925 FX 489 at (0, 0.1, 0), Shake(660, 0.1, 0.125), Rumble 4, SE 357; bs08_mgcra on the 19th tick with done == 2; deleted outside clips 24 / 25");
    }
}

static void test_bank_onback() {
    auto rig = [](int diff = 0) {
        auto r = std::make_unique<Rig>(setup(diff)); r->in.flag156 = false; r->form(kOnBack); r->fixed = r15(0);
        return r;
    };
    {
        auto r = rig();
        const Boss08Output a = r->bank(30, 1079.0f), b = r->bank(32, 1194.0f);
        check(fx(a, 461) && fx(a, 461)->parent == Boss08Parent::kBody && grey_flash(a) && n_se(a, 362) == 1 && fx(b, 462) && grey_flash(b) &&
              n_se(b, 362) == 1 && r->bank(30, 1194.0f).effects.empty(),
              "B23 §5.1 / P8: clip 30 f1079 FX 461, clip 32 f1194 FX 462, each on B with Flash() and SE 362");
    }
    {
        auto r = rig(); r->in.player_pos = {3.7f, 0.2f, -6.4f}; r->fixed.reset(); r->forced = {r15(8)};      // 8 % 5 = 3
        r->draws = 0;
        const Boss08Output o = r->bank(31, 1131.0f);
        const Vec3 pos = r->in.b_node[kLeftToe23Mark];
        auto nrm = [](Vec3 v) { const float l = std::sqrt(v.y * v.y + v.x * v.x + v.z * v.z); return Vec3{v.x / l, v.y / l, v.z / l}; };
        Vec3 u = nrm({3.7f - pos.x, 0.2f - pos.y, -6.4f - pos.z});
        Vec3 u1 = nrm({u.x - 5, u.y + 5, u.z}), u2 = nrm({u1.x - 5, u1.y - 5, u1.z}), u3 = nrm({u2.x, u2.y, u2.z - 1}),
             u4 = nrm({u3.x + 5, u3.y + 5, u3.z}), u5 = nrm({u4.x + 5, u4.y - 5, u4.z});
        const Boss08Bullet* b0 = bul(o, 0x25, 455);
        const bool five = n_bul(o, 0x25, 455) == 5 && r->draws == 1 && n_se(o, 361) == 1 && vnear(b0->dir, u1, 1e-5f) &&
                          vnear(bul(o, 0x25, 455, 1)->dir, u2, 1e-5f) && vnear(bul(o, 0x25, 455, 2)->dir, u3, 1e-5f) &&
                          vnear(bul(o, 0x25, 455, 3)->dir, u4, 1e-5f) && vnear(bul(o, 0x25, 455, 4)->dir, u5, 1e-5f);
        const bool blk = veq(b0->pos, pos) && bits(b0->f[0]) == 0x3f19999au && bits(b0->f[1]) == 0xb8d1b717u && b0->f[3] == -5.0f &&
                         bits(b0->f[5]) == 488 && bits(b0->f[6]) == 3 && bits(b0->f[7]) == 2 && bits(b0->f[8]) == 0x3e97e9d8u && b0->f[9] == 1.0f &&
                         b0->f[10] == 2.0f && b0->f[11] == 60.0f && bits(b0->f[12]) == ((131u << 16) | (131u << 8) | 121u) &&
                         bits(bul(o, 0x25, 455, 4)->f[12]) == bits(b0->f[12]);
        r->forced = {r15(0)};
        const Boss08Output o2 = r->bank(33, 1246.0f);
        check(five && blk && n_bul(o2, 0x25, 455) == 5 && veq(o2.bullets[0].pos, r->in.b_node[kRightToe23Mark]),
              "B23 §5.2 / P19 raikyu: five kind-0x25 455 from one Raw() % 5, the cumulative direction chain u1..u5, one shared packed target, SE 361 once");
    }
    {
        auto r = rig();
        r->draws = 0;
        const Boss08Output o = r->bank(34, 1316.0f);
        const bool th = fx(o, 463) && grey_flash(o) && n_bul(o, 0x25, 467) == 6 && n_se(o, 360) == 1 && n_bul(o, 6, 519) == 0;
        r->k().fc = 20; r->draws = 0;
        const Boss08Output v = r->bank(34, 1320.0f);
        bool fan = n_bul(v, 6, 519) == 6 && r->draws == 0;
        for (int i = 0; fan && i < 6; ++i) {
            const float a = -(40.0f + 20.0f * float(i)) * kDeg;
            const Vec3 d{float(std::cos(double(a))), 0, float(std::sin(double(a)))};
            const Boss08Bullet* b = bul(v, 6, 519, i);
            fan = vnear(b->dir, d, 1e-5f) && vnear(b->pos, {r->in.w_ora.x + 2 * d.x, 1.75f, r->in.w_ora.z - 5 + 2 * d.z}, 1e-4f) &&
                  b->f[0] == 0.1f && bits(b->f[1]) == 0x3a83126fu && b->f[3] == 0 && bits(b->f[7]) == 20 && bits(b->f[8]) == 0x3c8efa35u &&
                  b->f[9] == 6.0f && bits(b->f[10]) == 0x3f666666u && !b->raw;
        }
        r->k().fc = 21;
        const bool off = r->bank(34, 1320.0f).bullets.empty();
        r->k().fc = 20;
        check(th && fan && off && r->bank(34, 1318.75f).bullets.empty() && r->bank(34, 1331.25f).bullets.empty() &&
              r->bank(34, 1331.0f).bullets.size() == 6,
              "B23 §5.3 / P9: f1316 FX 463 + Flash + Bits(6) + SE 360; 1319 <= f <= 1331 with db+0x70 % 10 == 0: six kind-6 519 at -40..-140 deg from OraMarker (y 1.75, z - 5, + 2 dir), no draw");
    }
}

static void test_arms() {
    {
        Rig r; r.in.flag156 = false; r.in.player_pos = {3, 2, 4.5f}; r.k().beat_l = 1;
        std::vector<Boss08Output> v = r.run(241);
        const float yaw = -kHalfPi - float(std::atan2(double(4.5f - 8.5f), double(3.0f)));
        const Boss08Effect* e = fx(v[0], 466);
        const bool t1 = n_se(v[0], 333) == 1 && e && veq(e->pos, {3, 0.1f, 4.5f}) && nearf(e->rot.y, yaw, 1e-6f) && !e->flag201 &&
                        e->parent == Boss08Parent::kNone && sum_fx(v, 466) == 1;
        const Boss08Motion* m = motion(v[19], kA, "03_LeftBeat");
        int at20 = -1, at110 = -1;
        for (int i = 0; i < 241; ++i) { if (motion(v[std::size_t(i)], kA, "03_LeftBeat")) at20 = i; if (shake_is(v[std::size_t(i)], 30, 0.25f, 0.25f)) at110 = i; }
        check(t1 && m && m->flag && at20 == 19 && at110 == 109 && has(v[109].rumble, 1) && r.k().beat_l == 0 && r.k().hand_l_t == 1.0f &&
              r.k().hand_r_t == 1.0f,
              "B23 §6.1 LeftBeat: t1 SE 333 + FX 466 under the player (rot yawToP); t20 03_LeftBeat; t110 Shake(30, 0.25) + Rumble(1); 241 ticks, then the hand target 1.0");
    }
    {
        Rig r; r.in.flag156 = false; r.in.player_pos = {3, 2, 4.5f}; r.k().beat_r = 1;
        r.step();
        const bool fade = r.k().hand_r_t == 0 && r.k().hand_l_t == 1 && r.k().beat_r == 2;
        std::vector<Boss08Output> v = r.run(18);
        const Boss08Mesh& A = r.b.mesh(kA);
        const bool pre = A.pos.y == -999 && r.b.arms().alpha_t == 0;
        r.in.player_pos = {-2, 3, 1};
        Boss08Output o = r.step();                                 // t 20
        const float yaw = -kHalfPi - float(std::atan2(double(1.0f - 8.5f), double(-2.0f)));
        const bool placed = motion(o, kA, "02_RightBeat") && veq(A.pos, {-2, 0, 1}) && nearf(A.yaw, yaw, 1e-6f) && r.b.arms().alpha_t == 1.0f &&
                            A.no_hit && nearf(A.alpha, 0.025f, 1e-7f);
        r.run(19); const bool still = A.no_hit;
        r.step(); const bool t40 = !A.no_hit;                      // t 40
        r.run(159); const bool up = r.b.arms().alpha == 1.0f && r.b.arms().alpha_t == 1.0f;
        r.step();                                                  // t 200
        const bool t200 = r.b.arms().alpha_t == 0 && A.no_hit;
        r.run(41);
        check(fade && pre && placed && still && t40 && up && t200 && r.k().beat_r == 0 && r.k().hand_r_t == 1.0f && r.b.arms().alpha == 0 &&
              A.pos.y == -999,
              "B23 §6.1 / P10 RightBeat: hand target 0 at t1; t20 Place under the player (y 0, yawToP), alpha target 1, 02_RightBeat; t40 +0x298 &= ~0x40; t200 target 0, |= 0x40; arms ease 0.025, parked at y -999");
    }
    {
        Rig r(setup(2)); r.in.flag156 = false; r.in.player_pos = {3, 2, 4.5f}; r.k().clap = 1; r.fixed = r15(7);
        Boss08Output o = r.step();
        const Boss08Effect* e = fx(o, 468);
        const Boss08Mesh& A = r.b.mesh(kA);
        const bool t1 = n_se(o, 334) == 1 && r.k().hand_l_t == 0 && r.k().hand_r_t == 0 && e && veq(e->pos, {3, 0, 4.5f}) && e->flag201 &&
                        motion(o, kA, "01_Clap") && r.b.arms().alpha_t == 1.0f && !A.no_hit && veq(A.pos, {3, 0, 4.5f});
        std::vector<Boss08Output> v = r.run(58);
        const bool none = sum_bul(v, 0x25, 484) == 0;
        r.in.player_pos = {3.7f, 0.2f, -6.4f};
        o = r.step();                                              // t 60
        const Boss08Bullet* b = bul(o, 0x25, 484);
        const bool shot = b && veq(b->pos, A.pos) && veq(b->dir, {0, 1, 0}) && b->f[0] == 1.0f && bits(b->f[1]) == 0xba449ba6u && b->f[3] == -6.0f &&
                          bits(b->f[5]) == 483 && bits(b->f[6]) == 2 && bits(b->f[7]) == 2 && bits(b->f[8]) == 0x3ed67750u &&
                          bits(b->f[12]) == ((131u << 16) | (130u << 8) | 121u) && r.draws == 1;
        v = r.run(19);
        o = r.step();                                              // t 80
        const bool t80 = shake_is(o, 30, 0.3f, 0.3f) && has(o.rumble, 3);
        r.run(59); const bool b140 = r.b.arms().alpha_t == 1.0f;
        r.step(); const bool t140 = r.b.arms().alpha_t == 0 && A.no_hit;
        r.run(61);
        Rig n; n.in.flag156 = false; n.k().clap = 60;
        check(t1 && none && shot && t80 && b140 && t140 && r.k().clap == 0 && r.k().hand_l_t == 1 && r.k().hand_r_t == 1 && n.step().bullets.empty(),
              "B23 §6.2 / P11 / P12 Clap: t1 SE 334, both hands, Place, FX 468, 01_Clap; NM t60 one kind-0x25 484 straight up (24 deg); t80 Shake(30, 0.3) + Rumble(3); t140; 201 ticks");
    }
}

static void test_smoke() {
    Rig r; r.in.flag156 = false; r.k().smoke[0] = 1; r.k().smoke[2] = 1; r.fixed = kU50;      // every Rf = its midpoint
    const Boss08Output o = r.step();
    const Vec3 s1 = r.in.b_node[kSkirt1], rf = r.in.b_node[kRightFoot], rl = r.in.b_node[kRightLeg];
    const Boss08Bullet *a = bul(o, 2, 487, 0), *b = bul(o, 2, 487, 1), *c = bul(o, 2, 487, 2), *d = bul(o, 2, 487, 3);
    check(n_bul(o, 2, 487) == 4 && r.draws == 44 && vnear(a->pos, {s1.x, s1.y - 1.5f, s1.z}) && vnear(b->pos, {s1.x, s1.y - 3.0f, s1.z}) &&
          vnear(c->pos, rf) && vnear(d->pos, {(rf.x + rl.x) / 2, (rf.y + rl.y) / 2, (rf.z + rl.z) / 2}) && !a->flag201 && !a->raw &&
          nearf(a->f[0], 0.0225f, 1e-7f) && nearf(a->f[1], 0.0025f, 1e-7f) && a->f[3] == 0 && veq(a->dir, {0, 0, 0}),
          "B23 §7 limb smoke: 2 puffs a tick per smoking group (kind 2, 487, no +0x201), 11 draws a puff; g0 1.5 / 3.0 under Skirt1, legs: foot and foot-leg midpoint");
    r.run(89);
    const bool c91 = r.k().smoke[0] == 91;
    const Boss08Output o91 = r.step();
    const Boss08Output o92 = r.step();
    check(c91 && n_bul(o91, 2, 487) == 4 && r.k().smoke[0] == 0 && o92.bullets.empty(), "B23 §7 / P15: the counter runs 1..91 (91 ticks) and resets itself");
}

static void test_tail() {
    // ---- chaining
    auto chain = [](int form, int clip_, std::uint32_t c, int body_clip = -1) {
        Rig r; r.in.flag156 = false; r.form(form);
        r.at(kT, clip_, float(kTailClipTable[clip_].first) + 1.0f, c);
        if (body_clip >= 0) r.at(kB, body_clip, float(kBodyClipTable[body_clip].first) + 10.0f);
        const Boss08Output o = r.step();
        for (const auto& x : o.clips) if (x.mesh == kT) return x.section;
        return -1;
    };
    check(chain(0, 26, 0, 35) == -1 && chain(0, 26, 0, 0) == 0 && chain(0, 3, 1) == 0 && chain(0, 3, 0) == -1 && chain(0, 0, 1) == -1,
          "CE §3.1 form 0: the tail's lariat is cut the tick the BODY leaves clip 35; any other finished clip -> 01_NormalWait");
    check(chain(1, 16, 1) == 1 && chain(1, 16, 0) == -1 && chain(1, 1, 1) == 2 && chain(1, 3, 1) == 2 && chain(1, 2, 1) == -1,
          "CE §3.1 form 1: 17_High>>Normal -> 02_Normal>>Low; else -> 03_LowWait (04_Jump included)");
    check(chain(2, 4, 1) == 5 && chain(2, 7, 1) == 8 && chain(2, 8, 1) == 9 && chain(2, 10, 1) == 11 && chain(2, 11, 1) == 12 &&
          chain(2, 13, 1) == 14 && chain(2, 14, 1) == 15 && chain(2, 8, 0) == -1 && chain(2, 9, 1) == 6 && chain(2, 6, 1) == -1,
          "CE §3.1 / C15 form 2: Low>>Normal -> Normal>>High; rotate Start -> Loop -> End after ONE loop; else -> 07_HighWait");
    check(chain(3, 16, 1) == 17 && chain(3, 4, 1) == 17 && chain(3, 20, 1) == 21 && chain(3, 21, 1) == -1 && chain(3, 22, 1) == 18 &&
          chain(3, 25, 1) == 18 && chain(3, 17, 1) == 18 && chain(3, 18, 1) == -1 && chain(3, 23, 1) == -1,
          "CE §3.1 form 3: -> 18_Normal>>OnBack; Guard_Start -> Loop (held); Guard_End / SwingTail / the rest -> 19_OnBackWait");
    // ---- decisions
    auto ob = [](int diff = 0, bool easy = false) {
        auto r = std::make_unique<Rig>(setup(diff, 3, easy));
        r->form(kOnBack); r->k().cooldown = 99999;
        return r;
    };
    {
        auto r = ob();
        Boss08Output o = r->step();
        const Boss08Effect* e = fx(o, 144);
        const bool bar = n_clip(o, kT, 20) == 1 && e && e->call == Boss08Call::kFx2 && e->src == 1 && e->stat == Boss08Stat::kBody &&
                         e->parent == Boss08Parent::kCoreOra && veq(e->at_off, {0, -0.5f, 0}) && veq(e->at_scale, {2.5f, 2.5f, 2.5f}) &&
                         e->at_mode == 1 && e->handle == r->b.core().h_barrier && e->handle != 0;
        o = r->step();
        Rig n; n.k().cooldown = 99999; n.b.tail().form = 0; n.k().form = 0;
        check(bar && n_fx(o, 144) == 0 && n_fx(n.step(), 144) == 0,
              "CE §3.3 / C1 barrier up: 19_OnBackWait -> 21_Guard_Start; FX2 144 (src 1, stat B) on OraMarker at (0, -0.5, 0) scale 2.5 while no handle; OnBack only");
    }
    {
        auto r = ob(); r->clip(kT, 21); r->b.tail().tentacle_cd = 2; r->b.core().h_barrier = 9;
        r->step(); r->step();
        const bool cd = r->b.tail().tentacle_cd == 0 && r->draws == 0;
        r->forced = {0x20000u, r15(5), r15(11)};                   // Bit17 set -> Beat; k = 5; R(60) = 11
        Boss08Output o = r->step();
        const Track& t = r->b.track(tentacle_track(5));
        const bool beat = r->b.tail().beat[5] == 1 && r->b.tail().stab[5] == 0 && r->b.tail().tentacle_cd == 191 && r->draws == 3 &&
                          o.tracks.size() == 1 && o.tracks[0].bind && o.tracks[0].track == 1 && o.tracks[0].section == 19 &&
                          std::strcmp(o.tracks[0].node, "Bone_RightTentacle2-1") == 0 && t.owns && t.section == 19 && t.root == 5;
        r->b.tail().tentacle_cd = 0; r->draws = 0; r->forced = {0u, r15(5)};          // the same tentacle: busy
        o = r->step();
        const bool busy = r->draws == 2 && r->b.tail().tentacle_cd == 0 && o.tracks.empty();
        r->forced = {0u, r15(8 + 2), r15(0)};                      // Bit17 clear -> Stab; k = 10 & 7 = 2
        o = r->step();
        const bool stab = r->b.tail().stab[2] == 1 && o.tracks[0].track == 6 && o.tracks[0].section == 23 && r->b.tail().tentacle_cd == 180;
        auto g = ob(); g->clip(kT, 20); g->b.core().h_barrier = 9; g->step();
        auto u = ob(); u->clip(kT, 21); u->clip(kY, 4); u->b.core().h_barrier = 9; u->step();
        auto h = ob(1, true); h->clip(kT, 21); h->b.core().h_barrier = 9; h->forced = {0u, 0u, r15(0)}; h->step();
        check(cd && beat && busy && stab && g->draws == 0 && u->draws == 0 && h->b.tail().tentacle_cd == 200,
              "CE §3.3 / C14 tentacle pick (Guard_Loop, cooldown 0, settled): 2 draws; Bit17(w1) Beat / Stab; k = (w2 >> 17) & 7; busy = 2 draws again; cooldown R(60) + 180 / 140 / 100 (+60)");
    }
    {
        auto r = ob(); r->k().window = 3; r->b.tail().swing_cd = 99999; r->b.mesh(kB).touch = false; r->b.mesh(kB).collide = false;
        r->b.core().h_barrier = 0;
        Boss08Output o = r->step();
        const bool open = r->k().window == 2 && n_fx(o, 144) == 0 && r->b.tail().swing_cd == 99998 && n_clip(o, kT, 20) + n_clip(o, kT, 25) == 0;
        r->step();
        o = r->step();
        const bool closed = r->k().window == 0 && r->b.mesh(kB).touch && r->b.mesh(kB).collide && n_clip(o, kT, 20) == 1 && n_fx(o, 144) == 0;
        o = r->step();
        check(open && closed && n_fx(o, 144) == 1,
              "CE §3.3: the open window counts down here; at 0: body Touch on, collision on, SetClipName(21_Guard_Start); the barrier effect the tick after");
        auto s = ob(); s->k().window = 500; s->b.tail().swing_cd = 0; s->forced = {r15(123)};
        o = s->step();
        auto h = ob(2, true); h->k().window = 500; h->b.tail().swing_cd = 0; h->forced = {r15(360 + 5)}; h->step();
        auto g = ob(); g->k().window = 500; g->b.tail().swing_cd = 0; g->clip(kT, 22); g->step();
        check(n_clip(o, kT, 25) == 1 && s->b.tail().swing_cd == 123 + 440 && s->draws == 1 && h->b.tail().swing_cd == 5 + 340 + 60 && g->draws == 0,
              "CE §3.3 / C3 barrier down: SwingTail when its cooldown is 0 (not in a Guard clip); next = R(360) + 440 / 390 / 340 (+60)");
    }
    // ---- timelines
    {
        Rig r; r.in.flag156 = false; r.form(kOnBack); r.b.tail().beat[6] = 1;
        track_bind(r.b.track(2), 6, 19);
        std::vector<Boss08Output> v = r.run(291);
        const Boss08Effect* e = fx(v[79], 476);
        int rel = -1;
        for (int i = 0; i < 291; ++i) if (!v[std::size_t(i)].tracks.empty()) rel = i;
        check(first_se(v, 358, true) == 67 && sum_se(v, 358, true) == 1 && e && e->parent == Boss08Parent::kBody && veq(e->at_off, {0, 0.1f, 0}) &&
              e->stat == Boss08Stat::kTail && e->flag201 && e->call == Boss08Call::kFx && rel == 289 && !v[289].tracks[0].bind &&
              v[289].tracks[0].track == 2 && r.b.tail().beat[6] == 0 && r.b.track(2).releasing,
              "CE §3.2 (2) Beat: counter 68 SE2 358; 80 FX 470 + k on B at (0, 0.1, 0) (stat T); 290 release the track, counter 0");
        r.run(25);
        check(!r.b.track(2).owns && !r.b.track(2).releasing, "CE §3.4: 20 ticks after the release the sub-tree is back on the main clip");
    }
    {
        Rig r; r.in.flag156 = false; r.form(kOnBack); r.b.tail().stab[1] = 1; r.in.player_pos = {2, 3, -4};
        std::vector<Boss08Output> v = r.run(361);
        const Boss08Effect *vx = fx(v[39], 481), *sp = fx(v[79], 482);
        const int h = vx ? vx->handle : -1;
        const bool tel = vx && vx->call == Boss08Call::kInline && vx->parent == Boss08Parent::kBody && veq(vx->at_off, kVortex[1]) &&
                         vx->stat == Boss08Stat::kTail && h > 0 && n_se(v[39], 359, true) == 1;
        const bool spear = sp && veq(sp->pos, {2, 0, -4}) && sp->parent == Boss08Parent::kNone && sp->handle == 0;
        const bool pull = v[289].tracks.size() == 1 && v[289].tracks[0].bind && v[289].tracks[0].section == 24 && v[289].tracks[0].track == 5 &&
                          has(v[289].kill_handles, h);
        check(tel && spear && pull && v[359].tracks.size() == 1 && !v[359].tracks[0].bind && r.b.tail().stab[1] == 0 && r.b.tail().vortex[1] == 0,
              "CE §3.2 (3) Stab: 40 DarkVortex 481 at V[k] on B + SE2 359; 80 TentacleSpear 482 at the player's (x, 0, z); 290 bind 25_Pull + kill the vortex; 360 release");
        Rig f; f.in.flag156 = false; f.form(kOnBack); f.b.tail().stab[0] = 40; f.in.fx_pool_full = true;
        const Boss08Output o = f.step();
        f.b.tail().stab[0] = 290;
        const Boss08Output o2 = f.step();
        check(n_fx(o, 481) == 0 && n_se(o, 359, true) == 1 && f.b.tail().vortex[0] == 0 && o2.kill_handles.empty() && o2.tracks.size() == 1,
              "CE §3.2 (3) / K8: a full pool refuses the vortex (SE2 359 still plays); the 290 kill is guarded against the null handle");
    }
    {
        Rig r; r.in.flag156 = false; r.form(kOnBack); r.at(kT, 25, 1729.75f);
        Boss08Output o = r.step();
        const bool on = r.b.mesh(kT).touch && n_fx(o, 464) == 21 && n_se(o, 346, true) == 1 && n_fx(o, 469) == 0;
        const Boss08Effect *e0 = fx(o, 464, 0), *e2 = fx(o, 464, 2), *e3 = fx(o, 464, 3);
        const bool sparks = e0->parent == Boss08Parent::kTailNode && e0->parent_index == 2 && veq(e0->at_off, {0, 0, 0}) &&
                            nearf(e2->at_off.z, 2.0f, 1e-5f) && e3->parent_index == 3 && e0->stat == Boss08Stat::kTail && e0->flag201;
        o = r.step();                                              // f 1730
        const Boss08Effect* tr = fx(o, 469);
        const bool trail = tr && tr->call == Boss08Call::kInline && tr->parent == Boss08Parent::kBody && tr->stat == Boss08Stat::kTail &&
                           n_fx(o, 464) == 0;
        std::vector<Boss08Output> v = r.run(118);
        const bool cad = sum_fx(v, 464) == 4 * 21 && sum_se(v, 346, true) == 0 && n_fx(v[26], 464) == 21;
        o = r.step();                                              // t 120
        const bool s120 = n_se(o, 346, true) == 1;
        r.at(kT, 18, 1400.0f);
        r.step();
        Rig l; l.in.flag156 = false; l.form(kHigh); l.at(kT, 11, 1100.0f);
        const Boss08Output ol = l.step();
        check(on && sparks && trail && cad && s120 && !r.b.mesh(kT).touch && r.b.tail().spark_t == 0 && n_fx(ol, 464) == 117 &&
              fx(ol, 464)->parent_index == 10 && l.b.mesh(kT).touch,
              "CE §3.2 (1)(4): SwingTail: Touch on, 7 x 3 sparks 464 every 28 ticks (offsets 0, 1/3, 2/3 of node+0x130), SE2 346 every 120, the trail 469 at f 1730; a rotate loop: 39 x 3");
    }
    {
        Rig r; r.in.flag156 = false; r.form(kOnBack); r.b.tail().laser = 1;
        Boss08Output o = r.step();
        const bool pre = o.effects.empty() && r.b.tail().laser == 2;
        o = r.step();
        const Boss08Effect* e = fx(o, 465);
        const bool beam = e && e->call == Boss08Call::kInline && e->parent == Boss08Parent::kTailNode && e->parent_index == kTNodeEye &&
                          veq(e->at_rot, {0, kPi, 0}) && !e->flag201 && n_se(o, 363, true) == 1;
        r.run(177);
        const bool l180 = r.b.tail().laser == 180;
        r.step();
        check(pre && beam && l180 && r.b.tail().laser == 0, "CE §3.2 (5) eye laser: at counter 2 inline 465 on Bone_eye (rot (0, pi, 0), no +0x201) + SE2 363; a 180-tick lock");
    }
}

static void test_camera() {
    Boss08Setup s;
    Rig r(s);
    const Vec3 P{0, 0, -10};                                       // d = 18.5 from (0, 0, 8.5)
    Boss08CamRecord c1; c1.fov = 0.418879f; c1.eye_pitch = 0.418879f; c1.offset = {0, 0.9f, 0};
    const bool ret = r.b.camera_hook(1, c1, P);
    const bool k1 = nearf(c1.fov, (0.418879f - 0.0523599f) + 18.5f * kDeg * 0.25f, 1e-6f) &&
                    nearf(c1.eye_pitch, (0.418879f - 0.0349066f) + 18.5f * 0.015f, 1e-6f) && nearf(c1.offset.y, 1.65f, 1e-6f);
    r.b.camera_hook(1, c1, P);                                     // the statics are captured once
    check(ret && k1 && nearf(c1.offset.y, 1.65f, 1e-6f) && nearf(*r.b.statics().fov1, 21.0f * kDeg, 1e-6f),
          "CE §4.7 k 1: fov = 21 deg + d * 0.25 deg, pitch = 22 deg + d * 0.015, up offset 0.9 + 0.75 (bases captured once)");
    Boss08CamRecord c2; c2.fov = 0.558505f; c2.eye_pitch = 0.418879f; c2.offset = {0, 1.7f, 0};
    r.b.camera_hook(2, c2, P);
    Boss08CamRecord c4; c4.fov = 0.436332f; c4.eye_pitch = 0.436332f; c4.offset = {0, 3.2f, 0};
    r.b.camera_hook(4, c4, P);
    check(nearf(c2.fov, 25.0f * kDeg + 18.5f * kDeg * 0.35f, 1e-5f) && nearf(c2.eye_pitch, 21.0f * kDeg + 18.5f * 0.01f, 1e-5f) && c2.offset.y == 1.7f &&
          nearf(c4.fov, 15.0f * kDeg + 18.5f * kDeg * 0.5f, 1e-5f) && nearf(c4.eye_pitch, 13.0f * kDeg + 18.5f * 0.01f, 1e-5f) &&
          nearf(c4.offset.y, 1.7f, 1e-6f),
          "CE §4.7 k 2 / 4: 25 deg + d * 0.35 deg, 21 deg + d * 0.01; 15 deg + d * 0.5 deg, 13 deg + d * 0.01, up offset 3.2 - 1.5");
    r.k().cam_eye = {1, 2, 3}; r.k().cam_target = {4, 5, 6}; r.k().cam_fov = 0.523599f; r.k().cam_off = {0, 5.55f, 0};
    Boss08CamRecord c3; c3.eye_mode = 2; c3.target_mode = 1; c3.eye_dist = 30;
    r.b.camera_hook(3, c3, P);
    Boss08CamRecord c5, c6; c6.fov = 0.5f;
    const bool r6 = r.b.camera_hook(6, c6, P);
    r.b.camera_hook(5, c5, P);
    check(veq(c3.eye_fixed, {1, 2, 3}) && veq(c3.target_fixed, {4, 5, 6}) && c3.eye_dist == 0 && c3.fov == 0.523599f && veq(c3.offset, {0, 5.55f, 0}) &&
          c3.eye_mode == 0 && c3.target_mode == 0 && veq(c5.target_fixed, {0, 490, 0}) && r6 && c6.fov == 0.5f,
          "CE §4.7 k 3: the High block (both points fixed); k 5: target (0, 490, 0); k 0 / 6: nothing; every k returns 1");
}

// One limb-break cycle driven the way it is played [A §A.10].
static void test_cycle() {
    Rig r; r.k().cooldown = 99999; r.b.core().weak = 0; r.fixed = r15(1);      // every R(3) = 1
    r.in.b_node[kBerryMouthMark] = {0, 4, 7};
    int hits = 0;
    while (r.k().chest_t > 0) { r.b.hit_part(kB, 0, 1); r.step(); ++hits; }
    int n = 0;
    while (!r.k().down && n < 400) { r.step(); ++n; }
    const bool down = hits == 34 && r.k().down && r.sec[kB] == 36 && r.b.core().exposure != 0;
    // the alpha followed the hit-driven target at -0.005 a frame: 200 frames from 1.0
    const int fall_frames = hits + n;
    std::vector<Boss08Output> v = r.run(50);
    const bool vis = r.b.core().alpha == 1.0f && v.size() == 50;
    r.b.hit_part(kW, 0, 3000);
    r.step();
    const bool hurt = r.b.mesh(kW).hp == 6999 && r.sec[kW] == 4 && r.b.part(8).def == 275;
    n = 0;
    while (r.b.core().exposure != 0 && n < 700) { r.step(); ++n; }
    const bool closed = n == 600 - 53 && r.b.core().seq != 0 && r.k().chest_t == 1.0f;        // 599 W frames after Down
    n = 0;
    while (r.k().form == 0 && n < 400) { r.step(); ++n; }
    // band 1 crossed (6999 / 9999 < 0.8): refill on, the next form = R(3) != 0 -> 1 (Low)
    const bool changed = r.k().form == 1 && r.b.core().band == 1 && r.sec[kB] == 8 && r.b.tail().form == 1 && r.k().cooldown == 180 &&
                         nearf(float(n), 183.0f, 3.0f);
    n = 0;
    while (!(r.k().settled && !r.k().down) && n < 900) { r.step(); ++n; }
    const Boss08Mesh& B = r.b.mesh(kB);
    check(down && fall_frames >= 200 && fall_frames <= 202, "A §A.10: 34 hits take the chest target to 0; Down on the first frame its alpha is 0 (200 frames from 1.0)");
    check(vis && hurt, "A §A.10: the core is visible after 50 frames (+0.02) and takes real damage with the body's DEF while exposed");
    check(closed && changed, "A §A.10 / D §3: the exposure closes when db+0x1b4 reaches 600; ~182 frames later the band advances and the next form is drawn");
    check(r.k().settled && !r.k().down && B.touch && r.sec[kY] == 2 && nearf(float(n), 642.0f, 3.0f) && nearf(B.pos.z, -1.0f, 0.21f) &&
          r.k().cam_idx == 2 && r.b.core().refill == false && r.b.mesh(kW).hp < 0.8f * 9999 && r.b.mesh(kW).hp > 0.8f * 9999 - 50.0f,
          "A §A.5 / §A.10: settled after the floor one-shot f (640 ticks) + 2: camera 2, body re-armed at z -1; the HP bar back to just under the 80 % line");
}

int main() {
    test_tables_ctor();
    test_tracks();
    test_mover();
    test_motion();
    test_pushout();
    test_chain();
    test_form_change();
    test_shell();
    test_fall();
    test_core();
    test_damage_body();
    test_damage_core();
    test_death();
    test_pickers();
    test_bank_hands();
    test_bits_homing();
    test_bank_spear();
    test_bank_onback();
    test_arms();
    test_smoke();
    test_tail();
    test_camera();
    test_cycle();
    std::printf("[boss08] %d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
