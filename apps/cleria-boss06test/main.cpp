// Headless tests for src/cleria/boss06 (Zava's two heads) against build/boss_re/ZAVA.md (P1), ZAVA_D.md (D),
// ZAVA_EF.md (EF) and ZAVA_E2.md (E2). Every expectation is a number of those specs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>
#include <vector>

#include <functional>
#include <map>

#include "cleria/actor.hpp"
#include "cleria/boss06.hpp"
#include "cleria/boss06_zava.hpp"
#include "cleria/brains/brains.hpp"

using namespace cleria;
using namespace cleria::boss06;

static int fails = 0, total = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss06] %-96s %s\n", what, ok ? "OK" : "FAIL");
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

static bool nearf(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
static bool vnear(const Vec3& a, const Vec3& b, float tol = 1e-4f) {
    return nearf(a.x, b.x, tol) && nearf(a.y, b.y, tol) && nearf(a.z, b.z, tol);
}
static bool veq(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static std::uint32_t bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
static float wrap(float a) { while (a > 3.1415927f) a -= 6.2831855f; while (a < -3.1415927f) a += 6.2831855f; return a; }
static bool has(const std::vector<int>& v, int x) { for (int e : v) if (e == x) return true; return false; }
static int n_clip(const Boss06Output& o, int head, int slot) {
    int n = 0;
    for (const Boss06Clip& c : o.clips) n += c.head == head && c.slot == slot;
    return n;
}
static int n_fx(const Boss06Output& o, int id, int head = -1) {
    int n = 0;
    for (const Boss06Effect& e : o.effects) n += e.id == id && (head < 0 || e.head == head);
    return n;
}
static const Boss06Effect* fx(const Boss06Output& o, int id, int head = -1) {
    for (const Boss06Effect& e : o.effects) if (e.id == id && (head < 0 || e.head == head)) return &e;
    return nullptr;
}
static const Boss06Sound* snd(const Boss06Output& o, int id) {
    for (const Boss06Sound& s : o.sounds) if (s.id == id) return &s;
    return nullptr;
}
static bool flag(const Boss06Output& o, int f, int v) {
    for (const auto& p : o.flags) if (p.first == f && p.second == v) return true;
    return false;
}

// The host stand-in: the clip model (one tick a frame; a stop clip completes after its length; a SetClip restarts
// it), the node matrices (every node at the head's origin), the RNG (a queue of forced R() values, else xorshift).
struct Rig {
    Boss06 b;
    Boss06Input in;
    Boss06Zava z;
    XorShift rng;
    std::deque<std::uint32_t> forced;
    std::optional<std::uint32_t> fixed;
    int draws = 0;
    int sec[2] = {0, 0}, t[2] = {0, 0};
    bool blocked = false; int probes = 0; Vec3 pfrom{}, pdelta{};
    explicit Rig(const Boss06Setup& s = Boss06Setup{}) : b(s) {
        in.rng = [this]() -> std::uint32_t {
            ++draws;
            if (fixed) return *fixed << 17;
            if (!forced.empty()) { const std::uint32_t v = forced.front(); forced.pop_front(); return v << 17; }
            return rng();
        };
        in.probe = [this](int, const Vec3& f, const Vec3& d) { ++probes; pfrom = f; pdelta = d; return blocked; };
        in.player_pos = {-34.0f, 6.1f, -40.0f};                    // far away
        in.cam_eye = {0, 0, -10}; in.cam_target = {0, 0, 0};       // eye - target = (0, 0, -1) * 10
    }
    Rig(const Rig&) = delete;
    void feed() {
        for (int h = 0; h < 2; ++h) {
            in.head[std::size_t(h)].section = sec[h];
            in.head[std::size_t(h)].count = (!kSectionTable[sec[h]].loop && t[h] >= section_ticks(h, sec[h])) ? 1u : 0u;
            const Vec3 p = b.head(h).pos;
            for (auto& n : in.head[std::size_t(h)].nodes) n = ef2sim::translation(p.x, p.y, p.z);
        }
    }
    void clips(const Boss06Output& o) { for (const Boss06Clip& c : o.clips) { sec[c.head] = c.section; t[c.head] = 0; } }
    Boss06Output step() {
        feed();
        Boss06Output o = b.tick(in, z);
        clips(o); ++t[0]; ++t[1];
        if (o.hold) in.player_pos = o.hold->pos;
        if (o.release) in.player_pos = o.release->pos;
        return o;
    }
    Boss06Output start(int h, int m) { Boss06Output o; b.start_move(h, m, o); clips(o); return o; }
    Boss06Output abort(int h) { feed(); Boss06Output o; b.abort_move(h, in, z, o); clips(o); return o; }
    Boss06Head& a() { return b.head(0); }
    Boss06Head& hb(int i) { return b.head(i); }
};

static Boss06Setup setup(int diff, bool easy = false, bool ve = false) {
    Boss06Setup s; s.diff_index = diff; s.diff_easy = easy; s.diff_very_easy = ve;
    return s;
}
static const Vec3 kFarB{-20.0f, 6.1f, 21.0f};                      // head b parked away from head a's lap

static void test_ctor() {
    Rig n, h(setup(1)), nm(setup(2)), e(setup(0, true)), ve(setup(0, true, true));
    const Boss06Head& a = n.b.head(0); const Boss06Head& b = n.b.head(1);
    check(a.id == 0 && b.id == 1 && veq(a.pos, {-39.46f, 6.1f, 30.0f}) && veq(b.pos, {-28.82f, 6.1f, 30.0f}),
          "P1 §1.1/§3.2: a = boss06a at (-39.46, 6.1, 30), b = boss06b at (-28.82, 6.1, 30)");
    check(a.max_hp == 5000 && a.hp == 5000 && a.str == 232 && a.def == 320 && nm.a().def == 325,
          "P1 §3.2.7/12 [0x53beb8]: HP 5000, STR 232, DEF 320 (Nightmare 325)");
    check(a.move == 0 && a.step == 0 && a.timer == 0 && a.speed_target == 0.08f && a.accel == 0.001f && a.speed == 0 &&
          a.radius == 3.0f && a.wp_stop == 0xff && a.turn_on == 1 && a.pattern == 0 && a.queued == 0 && a.free == 0,
          "P1 §3.2.2/3/11: move 0, f8 0.08, fc 0.001, radius 3.0, hb+0x84 = 0xff, hb+0x7e = 1, pattern 0");
    check(a.k_speed == 1.0f && a.k_time == 1.0f && h.a().k_speed == 1.1f && h.a().k_time == 1.0f &&
          nm.a().k_speed == 1.2f && nm.a().k_time == 0.8f && e.a().k_speed == 0.8f && e.a().k_time == 1.3f &&
          ve.a().k_speed == 0.7f && ve.a().k_time == 1.3f,
          "P1 §3.2.12 [0x53c0aa]: hb+0x100 / 0x104 = VE .7/1.3, Easy .8/1.3, N 1/1, H 1.1/1, NM 1.2/.8");
    check(n.b.diff_level() == 2 && h.b.diff_level() == 3 && nm.b.diff_level() == 4 && e.b.diff_level() == 1 &&
          ve.b.diff_level() == 0, "D §6.6 FUN_00535280: difficulty level VE 0, Easy 1, Normal 2, Hard 3, NM 4");
    check(section_ticks(0, 1) == 92 && section_ticks(1, 1) == 92 && section_ticks(0, 12) == 80 &&
          section_ticks(1, 12) == 100 && section_ticks(0, 14) == 88 && section_ticks(1, 14) == 128 &&
          section_ticks(0, 8) == 28 && section_ticks(1, 8) == 44 && kClipSection[kShake2] == 4 &&
          kClipSection[kBite] == 7 && kClipSection[kPiyori1] == 12,
          "P1 §3.1/§3.2.5: clip ticks (ATTACK1 92, PIYORI_1 80/100, PIYORI_3 88/128, LASER_0 28/44), slot 12 = SHAKE");
    check(vnear(waypoint_world(2, 7), {-31.44f, 6.0f, 34.6f}) && vnear(waypoint_world(1, 14), {-36.56f, 6.0f, 34.6f}) &&
          vnear(waypoint_world(3, 11), {-28.88f, 6.0f, 21.0f}) && vnear(waypoint_world(3, 12), {-34.0f, 6.0f, 29.0f}) &&
          kWaypoints[3].count == 12 && kWaypoints[1].count == 15,
          "D §6.3: table*0.8 + (-34, 6, 29): T2[7] (-31.44, 34.6), T1[14] (-36.56, 34.6), T3[11] (-28.88, 21)");
}

static void test_pattern0_and_queue() {
    Rig r;
    const Boss06Output o1 = r.step();
    const Boss06Output o2 = r.step();
    check(n_clip(o1, 0, kWait) == 3 && n_clip(o1, 1, kWait) == 3 && o2.clips.empty() && r.a().pstep == 1 && r.a().fc == 2,
          "D §2 pattern 0 / §5.1: SetClip(WAIT) x2 + move 0's once on the first frame, then nothing");
    r.start(0, 6);
    r.a().queued = 1;
    for (int f = 0; f <= 120; ++f) r.step();
    const bool waited = r.a().pattern == 0 && r.a().move == 0;
    r.step();
    check(waited && r.a().pattern == 1 && r.a().pstep == 1,
          "D §2 [0x5449bd]: a queued pattern is applied only once the move is idle (move 6 lasts 120 frames)");
}

static void test_pattern1_and_mover() {
    Rig r;
    XorShift ref;
    r.a().queued = 1; r.hb(1).queued = 1;
    r.step();
    const std::uint32_t wa = ref(), wb = ref();
    const Boss06Head& a = r.a(); const Boss06Head& b = r.hb(1);
    check(a.wp == 7 && vnear(a.target, {-31.44f, 6.0f, 34.6f}) && b.wp == 14 && vnear(b.target, {-36.56f, 6.0f, 34.6f}),
          "D §3.1 step 0 [0x54364c]: a -> table 2 point 7 (-31.44, 6, 34.6); b -> table 1 point 14 (-36.56, 6, 34.6)");
    check(a.hold == (wa >> 17) % 121 + 60 - 1 && b.hold == (wb >> 17) % 121 + 60 - 1 && r.draws == 2,
          "D §3.1 / §11 [0x5436c2]: hb+0x78 = R(121) + 60, one draw per head, a before b (then -1 at the frame's end)");
    check(a.free == 1 && b.free == 1 && a.accel == 0.001f && b.accel == 0.0001f && a.speed == 0.001f && b.speed == 0.0001f &&
          veq(a.pos, {-39.46f, 6.1f, 30.0f}),
          "D §3.1 / §2 SPEED: hb+0x7f = 1; accel a 0.001, b 0.0001; no step on frame 0 (speed 0), then one accel step");

    // The lap of head a alone (b parked): displacement = sum of the speeds, straight at the target.
    Rig m;
    m.hb(1).pos = kFarB;
    m.a().queued = 1;
    const Vec3 p0 = m.a().pos;
    const Vec3 tgt = waypoint_world(2, 7);
    const float L = std::sqrt((tgt.x - p0.x) * (tgt.x - p0.x) + (tgt.z - p0.z) * (tgt.z - p0.z));
    const Vec3 d0{(tgt.x - p0.x) / L, 0, (tgt.z - p0.z) / L};
    for (int f = 0; f < 41; ++f) m.step();
    check(vnear(m.a().pos, {p0.x + d0.x * 0.82f, 6.1f, p0.z + d0.z * 0.82f}, 1e-3f) && nearf(m.a().speed, 0.041f, 1e-6f),
          "D §6.2: after 41 frames a moved 0.001*(1+..+40) = 0.82 toward its waypoint, y stays 6.1");
    for (int f = 41; f < 131; ++f) m.step();
    auto d2 = [&] { return (m.a().pos.x - tgt.x) * (m.a().pos.x - tgt.x) + (m.a().pos.z - tgt.z) * (m.a().pos.z - tgt.z); };
    const bool before = d2() >= 4.0f && m.a().speed == 0.08f;
    m.step();
    const bool within = d2() < 4.0f && m.a().wp == 7;
    const Vec3 pa = m.a().pos;
    m.step();
    check(before && within && m.a().wp == 8 && veq(m.a().pos, pa) && vnear(m.a().target, {-38.48f, 6.0f, 25.8f}) && m.draws == 1,
          "D §6.2: 7.24 u after 131 frames (> 2 away), 7.32 after 132 (within 2.0); arrival frame: index 8, no step");
    m.a().free = 0;
    m.step(); m.step();
    check(veq(m.a().pos, pa), "D §6.2 [0x544030]: the mover does nothing while hb+0x7f == 0");

    // Blocked steps: the 1 % roar or the waypoint skip.
    Rig k;
    k.hb(1).pos = kFarB;
    k.a().queued = 1; k.blocked = true;
    k.forced = {0, 500};
    k.step();
    check(k.a().wp == 8 && k.a().blocked == 1 && k.a().move == 0 && k.draws == 2 && veq(k.pfrom, k.a().pos) &&
          nearf(std::sqrt(k.pdelta.x * k.pdelta.x + k.pdelta.z * k.pdelta.z), 3.0f, 1e-3f),
          "D §6.2 / §6.4: probe(from = H.pos, delta = dir*(speed + 3.0)); blocked, R(1000) >= 10: skip a waypoint, 0x88++");
    k.forced = {5, 10};
    const Boss06Output o = k.step();
    check(k.a().move == 7 && k.a().blocked == 2 && k.a().free == 0 && k.a().timer == 69 && n_clip(o, 0, kShake) == 1 &&
          snd(o, 366) && veq(snd(o, 366)->pos, k.a().pos) && k.draws == 4,
          "D §6.2 / §5.3: R(1000) < 10 -> move 7 the same frame: SHAKE, SE 366 at the head, 0x7f = 0, timer R(61) + 60");
    Boss06Output oe;
    for (int f = 0; f < 70; ++f) oe = k.step();
    check(k.a().move == 0 && k.a().free == 1 && n_clip(oe, 0, kWait) == 1 && k.draws == 4,
          "D §5.3 FINISH7 [0x542e36]: the roar ends after its timer, hb+0x7f = 1 again, WAIT");
}

static void test_breath() {
    Rig r;
    r.hb(1).pos = kFarB;
    r.a().queued = 1; r.forced = {0};
    r.step();
    auto at = [&](float dz) { r.a().hold = 0; r.in.player_pos = {r.a().pos.x, 6.1f, r.a().pos.z - dz}; r.step(); return r.a().move; };
    const bool near_no = at(2.9f) == 0, far_no = at(12.1f) == 0;
    r.a().phase2 = 1; const bool p2_no = at(5.0f) == 0; r.a().phase2 = 0;
    r.hb(1).move = 2; r.hb(1).step = 1; const bool other_no = at(5.0f) == 0; r.hb(1).move = 0;
    r.a().hold = 5; r.in.player_pos = {r.a().pos.x, 6.1f, r.a().pos.z - 5.0f}; r.step();
    const bool hold_no = r.a().move == 0;
    check(near_no && far_no && p2_no && other_no && hold_no,
          "D §3.1 breath rule: not inside 3 / beyond 12, not with hb+0x81, not while the other head breathes, not on hold");
    r.a().hold = 0; r.in.player_pos = {r.a().pos.x, 6.1f, r.a().pos.z - 5.0f};
    r.forced = {60};
    Boss06Output o = r.step();                                     // frame f
    check(r.a().move == 2 && n_clip(o, 0, kAttack1) == 1 && r.a().slots[0].id == 385 && r.a().speed_target == 0.01f &&
          r.a().accel == 0.001f && r.a().slots[0].timer == 1,
          "D §5.2 step 0 [0x542eab]: ring 3 < d < 12 -> move 2: ATTACK1, f8 0.01 / fc 0.001, FX slot 0 <- 385");
    int shots = 0, first = -1, last = -1, se292 = 0, puffs = 0, end = -1, gap_ok = 1;
    Boss06Bullet bl{}; Boss06Effect pf{};
    for (int f = 1; f <= 92; ++f) {
        o = r.step();
        if (!o.bullets.empty()) {
            if (first < 0) { first = f; bl = o.bullets[0]; if (fx(o, 390)) pf = *fx(o, 390); }
            else if ((f - last) != 3) gap_ok = 0;
            last = f; ++shots;
        }
        se292 += snd(o, 292) != nullptr; puffs += n_fx(o, 390);
        if (end < 0 && r.a().move == 0) end = f;
    }
    check(shots == 10 && first >= 39 && first <= 41 && gap_ok && last == first + 27 && se292 == 1 && puffs == 10 &&
          r.a().slots[0].id == 0,
          "EF §F.2 case 385: from slot frame 40, every 3rd frame, 10 shots, SE 292 (a) once, then the slot clears");
    check(bl.kind == 0x21 && bl.effect == 386 && bl.f[0] == 0.18f && bl.f[1] == -0.001f && bl.f[2] == 0.0005f &&
          bl.f[3] == 0.0f && bl.f[4] == 3.0f && bits(bl.f[5]) == 0 && bits(bl.f[7]) == 0 && bl.f[9] == 1.0f &&
          bl.f[10] == 15.0f && bl.f[11] == 0.0f && bl.flag201 && bl.dir.y == 0.0f,
          "EF §F.2 [0x53d888]: bullet kind 0x21 eff 386 (ice): 0.18, -0.001, 0.0005, power 0, knock 3, f9 1, f10 15");
    check(pf.id == 390 && pf.call == Boss06Call::kFx2 && pf.stat && pf.rot.y == 1.5707964f && pf.power == 0 && pf.knock == 1.0f &&
          pf.parent == Boss06Parent::kNode && pf.parent_index == kFireEfc && pf.at_mode == 1 && veq(pf.at_off, {0, 0, -0.5f}),
          "EF §F.2: muzzle puff FX2 390 rot (0, pi/2, 0), attached to fire_efc mode 1 at (0, 0, -0.5)");
    check(end == 92 && r.a().hold == 179 && r.a().speed_target == 0.08f && r.a().accel == 0.001f && r.a().clip == kWait,
          "D §5.2 step 1: at ATTACK1's end (92 f) hold = R(61) + 120 on Normal (R = 60 -> 180), FINISH");

    struct Case { Boss06Setup s; std::uint32_t v; int hold; const char* what; };
    const Case cases[] = {
        {setup(0), 61, 120, "D §5.2: Normal hold R(61) + 120 (R(61) of 61 = 0 -> 120)"},
        {setup(0, true, true), 77, 233, "D §5.2 [0x542fa7]: k = 1.3 -> R(78) + 156 in f32 (77 -> 233)"},
        {setup(0, true), 78, 156, "D §5.2: k = 1.3 -> n = 78, not 79 (78 -> 156)"},
        {setup(2), 48, 144, "D §5.2: Nightmare k = 0.8 -> R(49) + 96 (48 -> 144)"},
        {setup(2), 49, 96, "D §5.2: Nightmare n = 49 (49 -> 96)"},
    };
    for (const Case& c : cases) {
        Rig q(c.s);
        q.step();
        q.start(1, 2);
        q.fixed = c.v;
        Boss06Output ob; int se291 = 0, b385 = 0; float pw = -1;
        for (int f = 0; f <= 92; ++f) {
            ob = q.step(); se291 += snd(ob, 291) != nullptr;
            for (const Boss06Bullet& x : ob.bullets) { b385 += x.effect == 385; pw = x.f[3]; }
        }
        check(q.hb(1).move == 0 && q.hb(1).hold == c.hold - 1 && se291 == 1 && b385 == 10 && pw == 3.0f && n_fx(ob, 390) == 0,
              c.what);
    }
}

static void test_ride_and_shake() {
    Rig r;
    r.step();
    r.in.player_grounded = true; r.in.player_floor = kFloorA;
    for (int f = 0; f < 5; ++f) r.step();
    const bool cnt = r.a().ride == 5 && r.hb(1).ride == 0;
    r.in.player_grounded = false; r.step();
    const bool air = r.a().ride == 5;
    r.in.player_grounded = true; r.in.player_floor = kFloorNone; r.step();
    const bool none = r.a().ride == 5;
    r.in.player_floor = kFloorA;
    for (int f = 0; f < 300; ++f) r.step();
    const bool sat = r.a().ride == 255;
    r.in.player_floor = kFloorB; r.step();
    check(cnt && air && none && sat && r.a().ride == 0 && r.hb(1).ride == 1,
          "D §2 [0x544dc4]: hb+0x86 ++ on its own head (sat. 255), unchanged airborne / no ground object, else 0");

    const int thr[3] = {180, 30, 60};
    bool ok = true;
    for (int p = 1; p <= 3; ++p) {
        Rig q;
        q.hb(1).pos = kFarB;
        q.b.set_pattern(0, p);
        q.step();
        q.a().hold = 0; q.a().laser_cd = 9999;
        if (p != 1) q.a().pstep = 2;
        q.in.player_grounded = true; q.in.player_floor = kFloorA;
        q.a().ride = std::uint8_t(thr[p - 1]);
        q.step();
        ok = ok && q.a().move == 0 && q.a().ride == thr[p - 1] + 1;
        const Boss06Output o = q.step();
        ok = ok && q.a().move == 6 && n_clip(o, 0, kShake) == 1 && q.a().timer == 119;
    }
    check(ok, "D §4.4: shake-off (move 6) when hb+0x86 > 180 (pattern 1) / > 30 (pattern 2) / > 60 (pattern 3)");

    Rig m;
    m.step();
    m.in.player_grounded = true; m.in.player_floor = kFloorA;
    m.in.player_pos = {m.a().pos.x + 3.0f, 9.0f, m.a().pos.z + 4.0f};
    m.step();
    m.start(0, 6);
    int first = -1, n = 0, end = -1; Vec3 v{};
    for (int f = 0; f <= 120; ++f) {
        const Boss06Output o = m.step();
        if (o.player_vel) { if (first < 0) { first = f; v = *o.player_vel; } ++n; }
        if (end < 0 && m.a().move == 0) end = f;
    }
    const Vec3 e{3.0f, 2.9f, 4.0f};
    const float l = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z);
    check(first == 31 && n == 90 && end == 120 && vnear(v, {e.x / l * 0.5f, 0.5f, e.z / l * 0.5f}, 1e-5f),
          "D §5.4: timer 120; from frame 31 (timer < 90) to 120 the player's velocity is SET to (0.5 d.x, 0.5, 0.5 d.z)");
}

static void test_moves_9_10() {
    Rig r;
    r.step();
    r.z.present = true; r.z.pos = {r.a().pos.x - 5.0f, 6.1f, r.a().pos.z};
    r.a().free = 1; r.a().speed = 0.05f;
    r.start(0, 9);
    Boss06Output o = r.step();
    const bool s0 = n_clip(o, 0, kWait) == 1 && r.a().busy == 1 && r.a().free == 0 && r.a().speed_target == 0.08f &&
                    nearf(r.a().yaw, 0.005f, 1e-6f);
    for (int f = 1; f < 10; ++f) r.step();
    const bool turned = nearf(r.a().yaw, 0.05f, 1e-5f);
    for (int f = 0; f < 500; ++f) r.step();
    check(s0 && turned && r.a().move == 9 && r.a().step == 1 && nearf(r.a().yaw, 1.5707964f, 1e-5f) && r.a().pitch == 0,
          "D §5.5: move 9 = WAIT, 0x114 = 1, 0x7f = 0, speed 0, turns to Zava at 0.005 rad/f, never ends by itself");

    r.start(0, 10);
    o = r.step();
    const Boss06Effect* e = fx(o, 426);
    check(o.shake && o.shake->first == 30 && veq(o.shake->second, {0.2f, 0.2f, 0.2f}) && n_clip(o, 0, kShake) == 1 &&
          r.a().slots[0].id == 426 && r.a().timer == 59,
          "D §5.6 [0x543262]: move 10 = Shake(30, 0.2), SHAKE, FX slot 0 <- 426, timer 60");
    check(e && e->call == Boss06Call::kFx2 && veq(e->scale, {2, 2, 2}) && e->stat && e->power == 0 && e->knock == 3.0f &&
          vnear(e->pos, {r.a().pos.x, r.a().pos.y, r.a().pos.z - 4.0f}) && e->parent == Boss06Parent::kSlot &&
          e->parent_index == 0 && e->at_mode == 2 && e->handle != 0 && snd(o, 395) && has(o.rumble, 3) &&
          nearf(r.a().slots[0].m.m[3][2], r.a().pos.z - 4.0f),
          "EF §F.2 case 426: FX2 426 scale 2 knock 3 at H.pos + 4*camdir, attached to the slot matrix mode 2, SE 395, Rumble 3");
    const int h = e ? e->handle : 0;
    for (int f = 1; f < 60; ++f) o = r.step();
    const bool alive = r.a().move == 10 && r.a().slots[0].id == 426 && o.kill_handles.empty();
    o = r.step();
    check(alive && r.a().move == 0 && n_clip(o, 0, kWait) == 1 && has(o.kill_handles, h) && r.a().slots[0].id == 0 &&
          r.a().slots[0].handle == 0,
          "D §5.6 / EF case 426: at frame 60 the move finishes and the barrier (slot timer 61) is soft-killed");
}

static void test_life_tick() {
    const int th[5][4] = {{1, 1, 1, 1}, {1, 2, 3, 4}, {2, 4, 6, 9}, {2, 4, 8, 12}, {4, 6, 10, 14}};
    const Boss06Setup su[5] = {setup(0, false, true), setup(0, true), setup(0), setup(1), setup(2)};
    const char* names[5] = {
        "D §4.1 [0x544750]: Very Easy thresholds 1/1/1/1; break = ZoomBlur(.5, -.05), +0x7a8 = 0, 0x78 = 0x7a = 60",
        "D §4.1: Easy thresholds 1/2/3/4; life lost kills orbiter #[lives left]; move 12 on both / move 8 at the last",
        "D §4.1: Normal thresholds 2/4/6/9; only the head that ticks first (a) loses the life (b's swap re-writes 0x78 = 60)",
        "D §4.1: Hard thresholds 2/4/8/12",
        "D §4.1: Nightmare thresholds 4/6/10/14",
    };
    for (int d = 0; d < 5; ++d) {
        bool ok = true;
        for (int i = 0; i < 4; ++i) {
            Rig r(su[d]);
            r.step();
            r.z.present = true;
            r.a().shield = 1; r.a().lives = std::uint8_t(4 - i); r.a().orb = {11, 12, 13, 14};
            r.hb(1).shield = 1; r.hb(1).lives = std::uint8_t(4 - i);
            r.z.hits = std::uint32_t(th[d][i] - 1);
            Boss06Output o = r.step();
            ok = ok && r.a().lives == 4 - i && !o.zoom && r.z.hits == std::uint32_t(th[d][i] - 1);
            r.z.hits = std::uint32_t(th[d][i]);
            o = r.step();
            const int left = 3 - i;
            ok = ok && r.a().lives == left && r.hb(1).lives == 4 - i && r.z.hits == 0 && o.zoom && o.zoom->first == 0.5f &&
                 o.zoom->second == -0.05f && r.a().hold == (left == 0 ? 59 : 60) && r.a().rearm == 59 && has(o.kill_handles, 11 + left) &&
                 r.a().orb[std::size_t(left)] == 0;
            if (left == 0) ok = ok && r.a().move == 8 && r.a().queued == 1 && r.hb(1).move == 0;
            else ok = ok && r.a().move == 12 && r.hb(1).move == 12;
        }
        check(ok, names[d]);
    }
    Rig r;
    r.step();
    r.z.present = true; r.z.hits = 50;
    r.a().lives = 4; r.a().shield = 1; r.a().stunned = 1; r.step();
    const bool stun = r.a().lives == 4;
    r.a().stunned = 0; r.a().rearm = 3; r.step();
    const bool rearm = r.a().lives == 4 && r.a().rearm == 2;
    r.a().rearm = 0; r.a().shield = 0; r.step();
    const bool shield = r.a().lives == 4;
    r.a().shield = 1; r.z.present = false; r.step();
    const bool noz = r.a().lives == 4;
    r.a().lives = 0; r.z.present = true; r.step();
    check(stun && rearm && shield && noz && r.a().lives == 0 && r.z.hits == 50,
          "D §4.1: no break while stunned (0x116), re-arming (0x7a), unshielded (0x80), without Zava, or with 0x83 = 0");
    r.a().hp = 10; r.step();
    const bool refill = r.a().hp == 5000 && !r.a().hits_off;
    r.a().hp = 10; r.in.player_hp_mirror = 0; r.step();
    check(refill && r.a().hp == 10 && r.a().hits_off,
          "D §4.1: HP = max HP every frame; player dead ([0x76a730] < 1): H+0x298 |= 0x40 and nothing else");
}

static void test_fusion_break() {
    for (int nm = 0; nm < 2; ++nm) {
        Rig r(setup(nm ? 2 : 0));
        r.step();
        r.z.present = true;
        r.a().shield = 1; r.a().phase2 = 1; r.a().lives = 1; r.a().busy = 1; r.a().orb = {21, 0, 0, 0};
        r.z.hits = 20;
        r.step();                                                  // the last life: Abort, queued 1, Start(8)
        const bool started = r.a().move == 8 && r.a().lives == 0;
        r.forced = {25, 7, 9};
        r.draws = 0;
        const Vec3 body = r.a().pos;
        const Boss06Output o = r.step();
        const Vec3 vh{0.4472136f, 0.0f, -0.8944272f};              // (1 - 2*25/100, 0, -1) normalised
        const bool thrown = o.zava_throw && vnear(o.zava_throw->pos, {body.x + 2 * vh.x, body.y, body.z + 2 * vh.z}, 1e-5f) &&
                            vnear(o.zava_throw->vel, {0.5f * vh.x, 0, 0.5f * vh.z}, 1e-6f) && veq(r.z.pos, o.zava_throw->pos);
        const Boss06Effect* e = fx(o, 440);
        const bool fxok = e && veq(e->pos, body) && !e->stat && e->knock == 1.0f && snd(o, 307) && veq(snd(o, 307)->pos, r.a().pos);
        const bool state = r.a().shield == 0 && r.a().phase2 == 0 && r.a().busy == 0 && r.a().free == 1 && r.a().move == 0 &&
                           r.a().step == 0 && n_clip(o, 0, kWait) == 1 && r.draws == 3;
        if (!nm) {
            check(started && thrown && fxok && state && r.a().stun_req == 1,
                  "E2 §2 move 8: un-fuse, Zava thrown 2.0 from body_efc at 0.5 (3 draws), FX2 440, SE 307, stun request, 1 frame");
            const Boss06Output o2 = r.step();
            check(r.a().pattern == 1 && r.a().move == 5 && r.a().stunned == 1 && r.a().stun_req == 0 && snd(o2, 367),
                  "D §4.3: after the break the queued pattern 1 applies and its first frame turns the request into move 5");
        } else {
            check(started && thrown && state && r.a().stun_req == 0, "E2 §2 [0x540274]: no stun request on Nightmare");
        }
    }
}

static void test_stun_and_hit() {
    const int p1[2] = {80, 100}, total_f[2] = {468, 528};
    for (int h = 0; h < 2; ++h) {
        Rig r;
        r.hb(1 - h).pos = h == 0 ? kFarB : Vec3{-48.0f, 6.1f, 21.0f};
        r.hb(h).queued = 1; r.forced = {0};
        r.step();
        r.in.player_section = 0x10f;
        r.feed();
        Boss06Output ho;
        const bool refused = !r.b.accept_hit(h, true, {1, 2, 3}, 1000, r.in, ho);
        r.clips(ho);
        if (h == 0)
            check(refused && r.hb(h).stun_req == 1 && n_clip(ho, h, kDamage) == 1 && ho.effects.empty(),
                  "D §7.1 [0x53b930]: the hit is always refused; section 0x10f raises hb+0x115; WAIT -> DAMAGE flinch");
        const Vec3 p = r.hb(h).pos;
        int f1 = -1, f2 = -1, f3 = -1, fend = -1, kills60 = 0;
        Boss06Effect star{}; bool s0 = false, slot385 = false;
        for (int f = 0; f <= total_f[h]; ++f) {
            const Boss06Output o = r.step();
            if (f == 0) s0 = snd(o, 367) && n_clip(o, h, kPiyori1) == 1 && r.hb(h).stunned == 1 && r.hb(h).busy == 1;
            if (n_clip(o, h, kPiyori2)) { f1 = f; if (fx(o, 60)) star = *fx(o, 60); }
            if (n_clip(o, h, kPiyori3)) { f2 = f; slot385 = r.hb(h).slots[0].id == 385 && has(o.kill_ids, 60); }
            if (f1 >= 0 && f2 >= 0 && f3 < 0 && r.hb(h).move == 0) { f3 = f; fend = r.hb(h).hold; kills60 += has(o.kill_ids, 60); }
        }
        check(s0 && f1 == p1[h] && f2 == p1[h] + 300 && f3 == total_f[h] && slot385 && fend == 59 && kills60 == 1 &&
              r.hb(h).stunned == 0 && r.hb(h).busy == 0 && r.hb(h).stars == 0 && veq(r.hb(h).pos, p),
              h == 0 ? "EF §E1.2 move 5 (a): SE 367, PIYORI_1 80 f, PIYORI_2 300 f, kill 60, PIYORI_3 + slot 385, end at 468 f"
                     : "EF §E1.2 move 5 (b): PIYORI_1 100 f + 300 + PIYORI_3 128 f = 528 f, hold 60, flags cleared");
        if (h == 0)
            check(star.id == 60 && star.call == Boss06Call::kInline && veq(star.scale, {1.25f, 1.25f, 1.25f}) && star.stat &&
                  star.parent == Boss06Parent::kNode && star.parent_index == kPiyoEfc && star.at_mode == 2 && star.handle != 0,
                  "EF §E1.2 step 1: InlineFX 60 (piyopiyo) scale 1.25, stat H+0x94, attached to piyo_efc mode 2");
    }
    {
        Rig r(setup(2));
        r.step(); r.in.player_section = 0x10f; r.feed();
        Boss06Output o;
        r.b.accept_hit(0, true, {}, 1000, r.in, o);
        const bool nm = r.a().stun_req == 0;
        Rig q;
        q.step(); q.in.player_section = 0x10f; q.feed();
        q.a().busy = 1; q.b.accept_hit(0, true, {}, 1000, q.in, o);
        const bool busy = q.a().stun_req == 0;
        q.a().busy = 0; q.a().stunned = 1; q.b.accept_hit(0, true, {}, 1000, q.in, o);
        const bool stunned = q.a().stun_req == 0;
        q.a().stunned = 0; q.in.player_section = 0x110; q.b.accept_hit(0, true, {}, 1000, q.in, o);
        check(nm && busy && stunned && q.a().stun_req == 0,
              "D §7.1 / §4.3: no stun request on Nightmare, while busy (0x114), while stunned (0x116), or off section 0x10f");
    }
    {
        Rig r;
        r.step(); r.feed();
        Boss06Output o;
        r.b.accept_hit(0, true, {}, 1000, r.in, o);
        r.clips(o);
        int back = -1;
        for (int f = 0; f <= 48 && back < 0; ++f) if (n_clip(r.step(), 0, kWait)) back = f;
        check(n_clip(o, 0, kDamage) == 1 && back == 48, "D §2 [0x544d44]: the DAMAGE flinch (48 ticks) returns to WAIT when it completes");
        r.a().shield = 1;
        Boss06Output s1, s2, s3, s4, s5;
        r.feed();
        r.b.accept_hit(0, true, {4, 5, 6}, 5000, r.in, s1);
        r.b.accept_hit(0, true, {4, 5, 6}, 5200, r.in, s2);
        r.b.accept_hit(0, true, {4, 5, 6}, 5201, r.in, s3);
        r.b.accept_hit(0, false, {4, 5, 6}, 6000, r.in, s4);
        r.b.accept_hit(0, true, {4, 5, 6}, 6100, r.in, s5);
        const Boss06Effect* e = fx(s1, 344);
        check(e && e->call == Boss06Call::kInline && veq(e->pos, {4, 5, 6}) && veq(e->scale, {2, 2, 2}) && e->or_1dc == 0x106 &&
              !fx(s2, 344) && fx(s3, 344) && !fx(s4, 344) && !fx(s5, 344),
              "D §7.1: shielded (0x80): splash 344 scale 2 at the hit point, > 200 ms apart; the stamp moves even with no spawn");
    }
    {
        Rig r;
        r.hb(1).pos = kFarB;
        r.b.set_pattern(0, 2);
        r.a().laser_cd = 9999; r.a().stun_req = 1;
        for (int f = 0; f < 200; ++f) r.step();
        check(r.a().stun_req == 1 && r.a().move == 0 && r.a().stunned == 0, "D §3.2 / D7: pattern 2 never reads the stun request");
    }
    {
        Rig r;
        r.a().speed = 0.08f;
        const std::optional<Vec3> v = r.b.touch(0, 0, {r.a().pos.x + 3, r.a().pos.y, r.a().pos.z + 4});
        check(v && vnear(*v, {0.048f, 0, 0.064f}, 1e-6f) && !r.b.touch(0, 1, {0, 0, 0}),
              "D §7.2 [0x53b780]: touch sets the actor's velocity to normalize(actor - head) * hb+0xf4, except entry 1");
    }
}

static void test_pattern2() {
    {
        Rig r;
        r.hb(1).pos = kFarB;
        r.a().queued = 2; r.a().laser_cd = 9999;
        r.in.player_pos = {r.a().pos.x, 6.1f, 22.0f};              // 8 in front (-z), inside the arena
        Boss06Output o;
        for (int f = 0; f < 120; ++f) o = r.step();
        const bool wait = r.a().pstep == 1 && r.a().hold == 0 && r.a().speed_target == 0.08f;
        o = r.step();                                              // frame 120: step 2, the first Turn
        const bool hunt = r.a().pstep == 2 && r.a().speed_target == 1.0f * 0.1f && r.a().accel == 0.1f && r.a().move == 0 &&
                          nearf(r.a().fwd.z, -1.0f, 1e-5f);
        o = r.step();                                              // frame 121: facing the player -> bite
        check(wait && hunt && r.a().move == 1 && n_clip(o, 0, kCharge0) == 1,
              "D §3.2: pattern 2 waits 120 f, then f8 = 0.1*hb+0x100, fc = 0.1; facing (dot > 0.7), d < 10, in the arena -> move 1");
    }
    {
        Rig e(setup(0, true));
        e.hb(1).pos = kFarB;
        e.a().queued = 2; e.a().laser_cd = 9999;
        e.in.player_pos = {e.a().pos.x, 6.1f, 22.0f};
        for (int f = 0; f < 125; ++f) e.step();
        const bool no_bite = e.a().move == 0 && e.a().speed_target == 0.8f * 0.1f;
        e.in.player_pos = {e.a().pos.x, 6.1f, 24.5f};              // 5.5 < 6
        e.step();
        Rig n;
        n.hb(1).pos = kFarB;
        n.a().pos = {-39.46f, 6.1f, 24.0f};
        n.a().queued = 2; n.a().laser_cd = 9999;
        n.in.player_pos = {-39.46f, 6.1f, 18.0f};                  // 6 in front, but outside the arena box (z < 19)
        for (int f = 0; f < 125; ++f) n.step();
        check(no_bite && e.a().move == 1 && n.a().move == 0,
              "D §3.2: bite range 6 on Very Easy / Easy (thr 36), and never when the player is outside the arena box");
        n.a().laser_cd = 0;
        n.step();
        check(n.a().move == 3 && n.a().slots[0].id == 389 && n.a().timer == 119,
              "D §3.2: idle, hold 0 and laser cool-down 0 -> move 3 (charge 120 outside pattern 3)");
    }
    {
        Rig r;
        r.hb(1).pos = kFarB;
        r.a().queued = 2; r.a().laser_cd = 9999; r.a().free = 1;
        r.a().target = waypoint_world(3, 0);                       // as a real fight leaves it: a valid waypoint
        r.in.player_pos = {r.a().pos.x, 6.1f, 0.0f};               // straight ahead, far
        for (int f = 0; f <= 125; ++f) r.step();
        // Frame 120 steers with the 0.08 the wait accelerated to (the first Turn gives the forward), then 0.1 a frame.
        const bool steer = nearf(r.a().pos.z, 30.0f - 0.08f - 5 * 0.1f, 1e-4f) && r.a().speed == 0.1f && r.a().pos.x == -39.46f;
        bool inside = true, unstick = false; int stuck = 0;
        for (int f = 0; f < 300 && !unstick; ++f) {
            r.step();
            inside = inside && r.a().pos.z >= 19.0f;
            stuck = std::max(stuck, int(r.a().blocked));
            unstick = r.a().unstick == 60;
        }
        check(steer && inside && unstick && stuck == 61,
              "D §3.2 free steering: 0.1 u/f along the flat forward, stopped by the box z >= 19; 0x88 > 60 -> 0x89 = 60");
        Rig q;
        q.b.set_pattern(0, 2);
        q.hb(1).blocked = 61;
        q.step();
        Rig ve(setup(0, false, true)), nm(setup(2));
        ve.b.set_pattern(0, 2); ve.a().pstep = 2; ve.a().laser_cd = 9; ve.step();
        nm.b.set_pattern(0, 2); nm.a().pstep = 2; nm.a().laser_cd = 9; nm.step();
        check(q.a().unstick == 120 && ve.a().speed_target == 0.7f * 0.1f && nm.a().speed_target == 1.2f * 0.1f,
              "D §3.2 / D8: the other head's 0x88 > 60 -> 0x89 = 120; hunt speed 0.07 (VE) .. 0.12 (NM)");
    }
}

struct BiteRun { int caught = -1, release = -1, exit = -1, dmg = 0, fx279 = 0, se304 = 0, rumble2 = 0, sec4 = 0, holds = 0, bursts = 0;
                 Boss06Release rel{}; Boss06Hold h20{}; float pitch20 = 0, speed_end = -1; bool free_out = false, flag_on = true; };

template <class F> static BiteRun run_bite(const Boss06Setup& s, F per_frame) {
    Rig r(s);
    r.step();
    r.in.player_pos = {r.a().pos.x, 6.1f, 22.0f};
    r.a().shield = 1; r.a().speed = 0.03f; r.a().speed_target = 0.03f;
    r.start(0, 1);
    BiteRun br;
    for (int f = 0; f < 700 && br.exit < 0; ++f) {
        if (br.caught >= 0) per_frame(r, f - br.caught - 1);
        const Boss06Output o = r.step();
        if (br.caught < 0 && r.a().step == 4) br.caught = f;
        if (o.hold) {
            ++br.holds; br.sec4 += o.hold->section4; br.flag_on = br.flag_on && flag(o, 134, 1);
            if (f - br.caught - 1 == 20) { br.h20 = *o.hold; br.pitch20 = r.a().pitch; }
        }
        br.dmg += int(o.damage.size()); br.fx279 += n_fx(o, 279); br.se304 += snd(o, 304) != nullptr;
        for (int x : o.rumble) br.rumble2 += x == 2;
        br.bursts += int(o.bursts.size());
        if (o.release) { br.release = f - br.caught - 1; br.rel = *o.release; }
        if (br.release >= 0 && o.player_free) { br.exit = f; br.speed_end = r.a().speed; br.free_out = r.a().free == 1 && r.a().shield == 1; }
    }
    return br;
}

static void test_bite() {
    {
        Rig r;
        r.step();
        r.in.player_pos = {r.a().pos.x, 6.1f, 22.0f};
        r.a().shield = 1;
        r.start(0, 1);
        int fb = -1, fs = -1, fc = -1, fh = -1; float z60 = 0, z61 = 0;
        bool free_cleared = false;
        for (int f = 0; f <= 74; ++f) {
            const Boss06Output o = r.step();
            if (f == 0 && n_clip(o, 0, kCharge0) != 1) fb = -99;
            if (n_clip(o, 0, kBite)) { fb = f; free_cleared = r.a().free == 0; }
            if (snd(o, 366)) { fs = f; z60 = r.a().pos.z; }
            if (f == 61) z61 = r.a().pos.z;
            if (fc < 0 && r.a().step == 4) fc = f;
            if (n_clip(o, 0, kShake2)) fh = f;
        }
        check(fb == 30 && free_cleared && fs == 60 && nearf(z60, 29.6f, 1e-4f) && nearf(z61, 29.1f, 1e-4f),
              "EF §E1.1: CHARGE_0 30 f, BITE 30 f (0x7f = 0), SE 366, lunge 0.4 on its first frame then 0.5 a frame");
        check(fc == 73 && r.a().shield == 0 && fh == 74 && r.a().timer == 479,
              "EF §E1.1 step 3/4: caught when the mouth is within 2.0 (8 - 6.4 at lunge frame 13): 0x80 = 0, SHAKE (slot 12), 480 f");
    }
    const auto none = [](Rig&, int) {};
    const BiteRun n = run_bite(setup(0), none);
    check(n.caught == 73 && n.release == 480 && n.holds == 481 && n.dmg == 9 && n.fx279 == 17 && n.se304 == 17 && n.rumble2 == 9 &&
          n.sec4 == 481 && n.flag_on,
          "EF §E1.1 step 5: 481 hold frames, flag 134 = 1; damage (ATK 237) x9 (t % 60), FX2 279 / SE 304 x17 (t % 30), Rumble(2) x9");
    check(nearf(n.pitch20, -0.5235988f, 1e-6f) && nearf(n.h20.pos.y, 6.1f - 0.25f, 1e-4f) && nearf(n.h20.pos.z, 23.6f - 0.4330127f, 1e-3f) &&
          nearf(n.h20.facing, -1.5707964f, 1e-3f),
          "EF §E1.1 step 5: the head tips to -30 deg; the player sits at fire_efc + 0.5*aim, facing = atan2(fwd.z, fwd.x)");
    check(n.rel.vel && vnear(*n.rel.vel, {0, 0, -1.5f * 0.8660254f}, 1e-3f) && nearf(n.rel.pos.z, 23.6f - 0.8660254f, 1e-3f) &&
          n.exit == 73 + 1 + 480 + 60 && nearf(n.speed_end, 0.031f, 1e-6f) && n.free_out && n.bursts >= 6,
          "EF §E1.1 steps 6-8/0x80: spat out at fwd*1.5 from fire_efc + fwd, 60 f recovery (bursts), exit: 0x7f = 1, 0x80 = 1, f4 = f0 (+ one accel step)");
    const BiteRun ve = run_bite(setup(0, false, true), [](Rig& r, int) { r.in.pad_pressed = true; });
    const BiteRun ea = run_bite(setup(0, true), [](Rig& r, int) { r.in.pad_pressed = true; });
    const BiteRun no = run_bite(setup(0), [](Rig& r, int) { r.in.pad_pressed = true; });
    const BiteRun nm = run_bite(setup(2), [](Rig& r, int) { r.in.pad_pressed = true; });
    check(ve.release == 1 && ea.release == 3 && no.release == 7 && nm.release == 7,
          "EF §E1.1 escape: 2 / 4 / 8 / 8 button ticks (VE / Easy / Normal / NM) end the hold on that frame");
    const BiteRun st = run_bite(setup(0), [](Rig& r, int) { r.in.pad_x = 1; });
    const BiteRun alt = run_bite(setup(0), [](Rig& r, int h) { r.in.pad_x = (h & 1) ? -1 : 1; });
    const BiteRun xy = run_bite(setup(0, false, true), [](Rig& r, int) { r.in.pad_x = 1; r.in.pad_y = -1; });
    check(st.release == 480 && alt.release == 7 && xy.release == 1,
          "EF §E1.1 escape: a held stick counts once per axis; each NEW edge (x, then y) counts");
    const BiteRun bu = run_bite(setup(0), [](Rig& r, int) { r.in.player_section = 0x139; });
    const BiteRun gd = run_bite(setup(0), [](Rig& r, int) { r.in.player_guard = true; });
    check(bu.release == 480 && bu.dmg == 0 && bu.fx279 == 0 && bu.sec4 == 0 && bu.se304 == 17 && gd.dmg == 0 && gd.fx279 == 0 &&
          gd.sec4 == 481,
          "EF §E1.1: a burst (Yunica 0x139) suppresses damage, FX 279 and the forced section 4, not the hold; +0x399 the first two");
    const BiteRun dead = run_bite(setup(0), [](Rig& r, int h) { if (h == 5) r.in.player_hp = 0; });
    check(dead.release == 5 && !dead.rel.vel, "EF §E1.1: P.HP == 0 ends the hold at once; no spit velocity / section 9 for a dead player");
    {
        Rig r;                                                     // the miss
        r.step();
        r.in.player_pos = {r.a().pos.x, 6.1f, 0.0f};
        r.start(0, 1);
        int puffs = 0; float smin = 9, smax = 0;
        r.draws = 0;
        for (int f = 0; f <= 80; ++f) {
            const Boss06Output o = r.step();
            for (const Boss06Effect& e : o.effects)
                if (e.id == 19) { ++puffs; smin = std::min(smin, e.scale.x); smax = std::max(smax, e.scale.x);
                                  if (e.pos.y != 6.3f || e.call != Boss06Call::kInline) puffs = -99; }
        }
        const bool miss = r.a().step == 7 && nearf(30.0f - r.a().pos.z, 9.9f, 1e-3f) && puffs >= 2 && puffs == r.draws &&
                          smin >= 1.0f && smax < 2.0f;
        Boss06Output o = r.step();
        check(miss && n_clip(o, 0, kWait) == 1 && r.a().shield == 1 && r.a().rearm == 59 && !o.release,
              "EF §E1.1: a miss = 20 lunge frames, reach 9.9; dust FX 19 at y 6.3 scale [1, 2) every 8th frame (1 draw each)");
        r.a().saved_speed = 0.02f;
        o = r.abort(0);
        check(o.player_free && flag(o, 134, 0) && r.a().move == 0 && r.a().free == 1 && r.a().speed == 0.02f && r.a().rearm == 60 &&
              n_clip(o, 0, kWait) == 1,
              "EF §E1.1 step 0x80 (AbortMove): player flags released, flag 134 = 0, StartMove(0), 0x7f = 1, 0x7a = 60, f4 = f0");
    }
}

static void test_laser() {
    for (int h = 0; h < 2; ++h) {
        for (int side = 0; side < (h == 0 ? 2 : 1); ++side) {
            Rig r;
            r.hb(1 - h).pos = h == 0 ? kFarB : Vec3{-48.0f, 6.1f, 21.0f};
            r.step();
            const Vec3 hp = r.hb(h).pos;
            r.in.player_pos = {hp.x, 6.1f, hp.z - 10.0f};
            r.start(h, 3);
            const int l0 = h == 0 ? 150 : 164;                     // LASER_0: a 28 ticks (< the 30 f timer), b 44
            const int l3 = h == 0 ? 32 : 20;
            int glows = 0, f322 = -1, fbeam = -1, fl3 = -1, fwait = -1, fend = -1, l2 = 0, tell = 0, shakes60 = 0;
            Boss06Effect beam{}, te{}; float y0 = 0, yend = 0; bool tell_killed = false, se_tell = false, shake30 = false;
            bool fx0 = true, exit_ok = false; Vec3 v8c{};
            for (int f = 0; f <= l0 + 45 + l3 + 120 + 1 && fend < 0; ++f) {
                if (f == 130) r.in.player_pos = {hp.x + (side ? -5.0f : 5.0f), 6.1f, hp.z - 10.0f};
                if (f == l0) y0 = r.hb(h).yaw;
                const Boss06Output o = r.step();
                glows += n_fx(o, 389 + (h == 0));
                if (fx(o, 322)) { f322 = f; te = *fx(o, 322); tell = te.handle; se_tell = snd(o, 365) != nullptr; }
                if (n_clip(o, h, kLaser1)) {
                    fbeam = f; tell_killed = has(o.kill_handles, tell);
                    if (fx(o, h == 0 ? 394 : 391)) beam = *fx(o, h == 0 ? 394 : 391);
                    shake30 = o.shake && o.shake->first == 30 && veq(o.shake->second, {0, 0.5f, 0}) && snd(o, h == 0 ? 294 : 293);
                    v8c = r.hb(h).v8c;
                }
                if (n_clip(o, h, kLaser2)) { ++l2; shakes60 += o.shake && o.shake->first == 60 && o.shake->second.x == 0.05f; }
                if (fbeam >= 0 && fl3 < 0) fx0 = fx0 && r.hb(h).fwd.x == 0.0f && r.hb(h).free == 0;
                if (n_clip(o, h, kLaser3)) { fl3 = f; yend = r.hb(h).yaw; }
                if (fl3 >= 0 && fwait < 0 && n_clip(o, h, kWait)) fwait = f;
                if (fwait >= 0 && f > fwait && r.hb(h).move == 0) {
                    fend = f;
                    exit_ok = has(o.kill_ids, 389 + (h == 0)) && has(o.kill_ids, 391) && r.hb(h).laser_cd == 239 &&
                              r.hb(h).hold == 119 && r.hb(h).free == 1 && r.hb(h).speed == 0.001f && r.hb(h).slots[0].id == 0;
                }
            }
            const float sgn = side ? 1.0f : -1.0f;                 // the player's side: +x -> the yaw decreases
            if (side == 0) {
                check(glows >= 8 && f322 == 120 && veq(te.scale, {1, 1, 2}) && te.knock == 0 && te.parent == Boss06Parent::kSlot &&
                      te.at_mode == 1 && te.flag201 && se_tell,
                      h == 0 ? "EF §E1.3 (a): charge 120 f with FX2 390 glows every 8th frame; LASER_0 + tell beam 322 (1,1,2) + SE 365"
                             : "EF §E1.3 (b): charge 120 f with FX2 389 glows; LASER_0 + tell beam FX2 322 + SE 365");
                check(fbeam == l0 && tell_killed && shake30 && beam.rtype == 3 && beam.power == (h == 0 ? 9.0f : 12.0f) &&
                      beam.knock == 0 && beam.stat && beam.flag201 && beam.parent == Boss06Parent::kSlot && beam.at_mode == 1,
                      h == 0 ? "EF §E1.3 step 2 / case 391 (a): at f 150 the tell is soft-killed; LASER_1, Shake 30, SE 294, FX2 394 power 9 rtype 3"
                             : "EF §E1.3 step 2 / case 391 (b): at f 164 (LASER_0 = 44 ticks); SE 293, FX2 391 power 12 rtype 3");
                check(fl3 == l0 + 45 && l2 == (h == 0 ? 5 : 3) && shakes60 == l2 && fx0 && fwait == l0 + 45 + l3 &&
                      fend == l0 + 45 + l3 + 120 && exit_ok,
                      h == 0 ? "EF §E1.3 steps 3-5 (a): sweep 45 f (LASER_2 + Shake 60 at 12, then every 8), LASER_3 32 f, hold 120 f, cd 240 / 120, kills 390 + 391"
                             : "EF §E1.3 steps 3-5 (b): LASER_2 at 28 / 36 / 44, LASER_3 20 f, hold 120 f, cool-down 240 / 120, kills 389 + 391");
            }
            if (h == 0)
                check(nearf(wrap(v8c.y - y0), sgn * 0.6981317f, 1e-5f) && nearf(wrap(yend - y0), sgn * 46 * 0.008f, 1e-3f),
                      side == 0 ? "EF §E1.3: sweep target = yaw - 40 deg with the player on the +x side; 46 TurnAng steps of 0.008"
                                : "EF §E1.3: sweep target = yaw + 40 deg with the player on the -x side");
        }
    }
    {
        Rig r, ve(setup(0, false, true));                          // pattern 3: short charge, slow sweep, no cool-down
        r.hb(1).pos = kFarB; ve.hb(1).pos = kFarB;
        r.b.set_pattern(0, 3); ve.b.set_pattern(0, 3);
        r.in.player_pos = {r.a().pos.x, 6.1f, r.a().pos.z - 7.9f};
        ve.in.player_pos = r.in.player_pos;
        r.step(); ve.step();
        const bool started = r.a().move == 3 && r.a().timer == 59 && ve.a().timer == 77;
        int fl1 = -1, fl3 = -1, fend = -1; float y0 = 0, y1 = 0;
        for (int f = 1; f <= 167 && fend < 0; ++f) {
            if (f == 90) y0 = r.a().yaw;
            const Boss06Output o = r.step();
            if (n_clip(o, 0, kLaser1)) fl1 = f;
            if (n_clip(o, 0, kLaser3)) { fl3 = f; y1 = r.a().yaw; }
            if (fl3 >= 0 && f > fl3 && r.a().move == 0) fend = f;
        }
        check(started && fl1 == 90 && fl3 == 135 && nearf(std::fabs(wrap(y1 - y0)), 46 * 0.007f, 1e-3f) && fend == 167 &&
              r.a().hold == 0 && r.a().laser_cd == 0,
              "D §3.3 / EF §E1.3: pattern 3 lasers inside 8 u: charge trunc(60k) (60; 78 at k 1.3), rate 0.007, no hold, no cool-down");
        Rig far;
        far.hb(1).pos = kFarB;
        far.b.set_pattern(0, 3);
        far.in.player_pos = {far.a().pos.x, 6.1f, far.a().pos.z - 8.1f};
        far.step();
        Rig q;
        q.hb(1).pos = kFarB;
        q.step();
        q.hb(1).move = 11; q.hb(1).step = 5;
        q.start(0, 3); q.step();
        check(far.a().move == 0 && q.a().timer == 59, "D §3.3: no laser beyond 8 u; EF §E1.3 step 0: the other head in move 11 -> charge 60");
    }
    {
        Rig r;                                                     // the pattern-2 ending
        r.hb(1).pos = kFarB;
        r.a().queued = 2;
        int fend = -1;
        for (int f = 0; f <= 120 + 347 && fend < 0; ++f) { r.step(); if (f > 120 && r.a().move == 0) fend = f; }
        check(fend == 467 && r.a().hold == 59 && r.a().laser_cd == 239,
              "EF §E1.3 step 5: in pattern 2 the laser (347 f) ends with hb+0x7c = 240 and hb+0x78 = trunc(60k)");
    }
}

static void test_floor() {
    for (int h = 0; h < 2; ++h) {
        Rig r;
        r.step();
        r.in.fog_colour = Vec3{0.1f, 0.2f, 0.3f};
        r.fixed = 0;
        r.start(h, 11);
        Boss06Output o = r.step();                                 // frame 0
        const Vec3 hp = r.hb(h).pos;
        const Boss06Effect* e = fx(o, 441);
        const bool s0 = n_clip(o, h, kLaser3) == 1 && r.hb(h).busy == 1 && r.hb(h).free == 0 && e && veq(e->pos, {hp.x, 6.05f, hp.z}) &&
                        !e->stat && snd(o, 295) && veq(snd(o, 295)->pos, hp);
        for (int f = 1; f < 30; ++f) r.step();
        const bool still = r.hb(h).pos.y == 6.1f && r.hb(h).alpha_target == 1.0f;
        r.step();
        const bool dive = r.hb(h).alpha_target == 0.9f && nearf(r.hb(h).pos.y, 6.0f, 1e-5f);
        for (int f = 31; f < 111; ++f) r.step();
        const bool not_yet = r.hb(h).step == 2;
        r.draws = 0;
        o = r.step();                                              // frame 111: S3 + the first S4
        const Boss06Head& hb = r.hb(h);
        const Boss06Effect* fl = fx(o, h == 0 ? 427 : 428);
        check(s0 && still && dive && not_yet && hb.step == 4 && hb.alpha_target == 0.0f && hb.pos.y + 8.0f <= 6.0f &&
              hb.pos.y > -2.2f && r.hb(1 - h).queued == 3,
              h == 0 ? "E2 §3.1 (a): LASER_3, FX2 441 at y 6.05 + SE 295; dive from f 30, 82 steps of 0.1; f 111: hidden, other head <- pattern 3"
                     : "E2 §3.1 (b): the same dive: S3 and the first S4 tick at f + 111");
        check(fl && veq(fl->pos, {-34.0f, 6.01f, 29.0f}) && fl->stat && hb.slots[0].id == (h == 0 ? 427 : 428) && has(o.rumble, 3) &&
              snd(o, h == 0 ? 297 : 300) && o.zoom && o.zoom->first == 0.5f && o.zoom->second == -0.001f && hb.fog.dur == 60 &&
              veq(hb.fog.to, h == 0 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}) && veq(hb.fog.saved, {0.1f, 0.2f, 0.3f}),
              h == 0 ? "E2 §3.1 S3 / EF case 427 (a): floor FX2 427 at (-34, 6.01, 29), SE 297, Rumble 3, ZoomBlur(.5, -.001), fog -> (0, 0, 1)"
                     : "E2 §3.1 S3 / EF case 427 (b): FX2 428 (slot id rewritten), SE 300, fog -> (1, 0, 0)");
        // The floor: 300 frames with the player standing on it.
        r.in.player_grounded = true; r.in.player_pos = {-30.0f, 6.0f, 30.0f}; r.in.player_vel = {0.01f, 0, 0};
        int ice = 0, brk = 0, burns = 0, shakes = 0, spawns = 0; bool spawn_ok = true, clock_ok = true;
        for (int f = 0; f < 300; ++f) {
            const std::uint32_t fc = hb.fc; const int iv = hb.pil_interval; const Vec3 sample = hb.v8c;
            o = r.step();
            ice += o.surface_ice; brk += o.pillar_break_test;
            for (const Boss06Damage& d : o.damage) burns += d.head == h && d.atk == 226.0f && d.or12c_40;
            shakes += o.shake && o.shake->first == 1000 && o.shake->second.x == 0.05f;
            const Boss06Effect* p = fx(o, h == 0 ? 290 : 256);
            if (p) {
                ++spawns;
                clock_ok = clock_ok && fc % std::uint32_t(iv) == 0 && hb.pil_interval == (h == 0 ? 60 : 20);
                const Vec3 want = h == 0 ? Vec3{-30.0f + 0.8f, 6.02f, 30.0f} : Vec3{sample.x, 6.02f, sample.z};
                spawn_ok = spawn_ok && vnear(p->pos, want, 1e-5f) && p->stat && p->rtype == 3 && p->knock == 0 &&
                           p->power == (h == 0 ? 4.0f : 10.0f) && p->scale.x == (h == 0 ? 1.2f : 1.5f) &&
                           snd(o, h == 0 ? 298 : 301) && veq(snd(o, h == 0 ? 298 : 301)->pos, p->pos);
            } else clock_ok = clock_ok && (fc % std::uint32_t(iv) != 0 || hb.pil_count >= 0x40);
        }
        if (h == 0)
            check(ice == 300 && brk == 300 && burns == 0 && shakes == 0 && spawns >= 5 && spawn_ok && clock_ok && hb.pil_count == spawns,
                  "E2 §3.2 (a): ice surface + break test every frame; pillar 290 (power 4, scale 1.2, rtype 3, SE 298) 80 f ahead of the player, on fc % interval");
        else
            check(ice == 0 && brk == 0 && burns == 10 && shakes == 10 && spawns >= 14 && spawn_ok && clock_ok && hb.pil_count == 0,
                  "E2 §3.2 (b): the burn (ATK 226, +0x12c |= 0x40, Shake 1000) every 30th fc; flame 256 (power 10, scale 1.5, SE 301) at the previous sample");
        if (h == 0) {
            r.in.player_pos = {-18.5f, 6.0f, 30.0f};               // the lead leaves the arena box -> the player's position
            const Boss06Effect* p = nullptr;
            for (int f = 0; f < 80 && !p; ++f) { o = r.step(); p = fx(o, 290); }
            const bool fallback = p && vnear(p->pos, {-18.5f, 6.02f, 30.0f}, 1e-5f);
            r.hb(0).pil_count = 0x40; r.draws = 0;
            int more = 0;
            for (int f = 0; f < 200; ++f) more += n_fx(r.step(), 290);
            check(fallback && more == 0 && r.draws == 0, "E2 §3.2: lead outside the arena -> the player's position; head a stops after 64 pillars (no draw)");
            r.in.player_grounded = false;
            o = r.step();
            const bool air = !o.surface_ice && o.pillar_break_test;
            r.in.player_grounded = true; r.in.player_pos.y = 7.5f; r.in.player_r324 = 0.4f;
            o = r.step();
            check(air && !o.surface_ice, "E2 §3.2: on the floor = grounded and feet (y - P+0x324) below 7.0");
        } else {
            r.in.player_grounded = false;
            int b2 = 0;
            for (int f = 0; f < 90; ++f) b2 += int(r.step().damage.size());
            check(b2 == 0, "E2 §3.2 (b): no burn while the player is airborne");
        }
        // The fog fader: the first fade is gradual (61 ticks), every later one snaps.
        Vec3 rec{0.1f, 0.2f, 0.3f};
        r.b.fog_tick(h, &rec);
        const bool t0 = veq(rec, {0.1f, 0.2f, 0.3f});
        for (int k = 1; k <= 30; ++k) r.b.fog_tick(h, &rec);
        const Vec3 to = hb.fog.to;
        const bool mid = vnear(rec, {(to.x - 0.1f) * 0.5f + 0.1f, (to.y - 0.2f) * 0.5f + 0.2f, (to.z - 0.3f) * 0.5f + 0.3f}, 1e-6f);
        for (int k = 31; k <= 60; ++k) r.b.fog_tick(h, &rec);
        const bool full = veq(rec, to) && hb.fog.dur == 60;
        r.b.fog_tick(h, &rec);
        const bool off = hb.fog.dur == 0 && hb.fog.t == 62;
        // Surface: AbortMove at the start of frame A.
        r.in.fog_colour = rec; r.in.zoom_active = true;
        o = r.abort(h);
        const bool s80 = o.shake && o.shake->first == 0 && o.zoom && o.zoom->second == -0.01f && hb.slots[1].id == 429 &&
                         hb.step == 0x81 && hb.timer == 30;
        o = r.step();                                              // frame A
        const Boss06Effect* bu = fx(o, h == 0 ? 429 : 438);
        const bool burst = bu && veq(bu->pos, {-34.0f, 6.02f, 29.0f}) && snd(o, h == 0 ? 299 : 302) && has(o.rumble, 3) && hb.slots[1].id == 0;
        for (int f = 1; f < 30; ++f) o = r.step();
        const bool quiet = o.kill_ids.empty();
        o = r.step();                                              // A + 30
        const bool s81 = has(o.kill_ids, 291) && has(o.kill_ids, 290) && has(o.kill_ids, h == 0 ? 427 : 428) && fx(o, 441) &&
                         snd(o, 295) && hb.slots[0].id == 0 && veq(hb.fog.to, {0.1f, 0.2f, 0.3f}) && hb.fog.dur == 60;
        r.b.fog_tick(h, &rec);
        const bool snap = veq(rec, {0.1f, 0.2f, 0.3f}) && hb.fog.dur == 0;
        for (int f = 31; f < 60; ++f) r.step();
        const bool hidden = hb.alpha_target == 0.0f;
        r.step();                                                  // A + 60
        const bool s82 = hb.alpha_target == 1.0f && hb.step == 0x83;
        for (int f = 61; f < 141; ++f) r.step();
        const bool rising = hb.move == 11 && hb.pos.y < 6.1f;
        o = r.step();                                              // A + 141
        if (h == 0) {
            check(t0 && mid && full && off && snap,
                  "E2 §3.5: fog fader: t = 0/60 .. 60/60 (61 ticks) then one clamped tick; hb+0x3e4 is never reset, so the fade back snaps");
            check(s80 && burst && quiet && s81,
                  "E2 §3.3 S80/S81 (a): Shake off, blur -> -0.01, slot 1 <- 429 (FX2 429, SE 299, Rumble 3); +30: kill 291 / 290 / 427, FX2 441, SE 295");
        } else {
            check(s80 && burst && s81, "E2 §3.3 (b): floor burst FX2 438 + SE 302; the slot kill goes by the rewritten id 428");
        }
        check(hidden && s82 && rising && hb.move == 0 && hb.pos.y == 6.1f && r.hb(1 - h).queued == 1 && hb.busy == 1 && hb.free == 0 &&
              n_clip(o, h, kWait) == 1,
              h == 0 ? "E2 §3.3 S82/S83 (a): visible at +60, 82 steps up, +141: y = 6.1, other head <- pattern 1, idle with 0x114 = 1 / 0x7f = 0"
                     : "E2 §3.3 S82/S83 (b): back at y 6.1 and idle at abort + 141");
    }
    {
        struct Iv { Boss06Setup s; int head; std::uint32_t v; int want; };
        const Iv ivs[] = {{setup(0), 0, 20, 80}, {setup(0), 0, 21, 60}, {setup(0), 1, 40, 60}, {setup(0), 1, 41, 20},
                          {setup(0, true), 0, 60, 180}, {setup(0, true), 0, 61, 120}, {setup(0, false, true), 1, 80, 180},
                          {setup(0, true), 1, 81, 100}, {setup(2), 0, 20, 80}};
        bool ok = true;
        for (const Iv& c : ivs) {
            Rig r(c.s);
            r.step();
            r.hb(c.head).pos.y = -2.0f; r.hb(c.head).step = 3; r.hb(c.head).move = 11; r.hb(c.head).fc = 40;
            r.fixed = c.v;
            r.step();
            ok = ok && r.hb(c.head).pil_interval == c.want && r.draws == 1;
        }
        check(ok, "E2 §3.2 [0x5411b7]: interval = R(21)+60 (a) / R(41)+20 (b); Very Easy / Easy R(61)+120 (a) / R(81)+100 (b)");
        Rig r;
        r.step();
        r.in.zoom_active = false;
        r.hb(0).move = 11; r.hb(0).step = 1; r.hb(0).timer = 20;
        const Boss06Output o = r.abort(0);
        check(!o.zoom && o.shake && r.hb(0).step == 0x81, "E2 §3.3 / E15: S80 rewrites the blur only when one is live ([0x76a550] != 0)");
    }
}

static void test_dive() {
    Rig r;
    r.step();
    r.start(1, 17); r.start(0, 17);
    Boss06Output o = r.step();
    const Vec3 q{r.a().pos.x, 6.05f, r.a().pos.z};
    const bool s0 = n_clip(o, 0, kWait) == 1 && n_fx(o, 441) == 2 && fx(o, 441, 0) && veq(fx(o, 441, 0)->pos, q) && snd(o, 295) &&
                    veq(snd(o, 295)->pos, q) && r.a().busy == 1 && r.a().free == 0;
    for (int f = 1; f < 111; ++f) r.step();
    const bool s2 = r.a().step == 2;
    r.step();
    const bool s4 = r.a().step == 4 && r.hb(1).step == 4 && r.a().alpha_target == 0.0f;
    for (int f = 0; f < 200; ++f) o = r.step();
    check(s0 && s2 && s4 && r.a().step == 4 && r.a().move == 17 && o.effects.empty(),
          "E2 §4 move 17: WAIT, FX2 441 + SE 295 at (x, 6.05, z); hidden at f + 111 (step 4), then waits for an AbortMove");
    r.abort(1); r.abort(0);
    int f441 = -1, f296 = -1, fend = -1, snds30 = 0;
    for (int f = 0; f <= 141; ++f) {
        o = r.step();
        if (fx(o, 441, 0)) { f441 = f; snds30 = int(o.sounds.size()); }
        if (snd(o, 296)) f296 = f;
        if (fend < 0 && r.a().move == 0) fend = f;
    }
    check(f441 == 30 && snds30 == 0 && f296 == 60 && fend == 141 && r.a().pos.y == 6.1f && r.hb(1).pos.y == 6.1f &&
          r.a().queued == 1 && r.hb(1).queued == 1 && r.a().busy == 1 && r.a().free == 0,
          "E2 §4: abort + 30: FX2 441 (no sound); + 60: visible, SE 296; + 141: y 6.1, the other head's queued pattern = 1");
}

static void test_swap() {
    for (int both = 0; both < 2; ++both) {
        Rig r;
        r.step();
        r.z.present = true; r.z.hits = 5;
        Boss06Head& a = r.a(); Boss06Head& b = r.hb(1);
        a.shield = 1; b.shield = std::uint8_t(both);
        a.wp = 3; b.wp = 9; a.target = {1, 2, 3}; b.target = {4, 5, 6}; a.yaw = 0.5f; b.yaw = -0.25f; a.rot = {7, 8, 9};
        a.speed_target = 0.01f; a.accel = 0.5f; a.speed = 0.02f; b.speed = 0.03f; a.fwd = {1, 0, 0}; a.aim = {0, 1, 0};
        const Vec3 pa = a.pos, pb = b.pos;
        r.start(1, 12); r.start(0, 12);
        const Boss06Output o = r.step();
        if (!both) {
            check(veq(a.pos, pb) && veq(b.pos, pa) && a.wp == 9 && b.wp == 3 && veq(b.target, {1, 2, 3}) && b.yaw == 0.5f &&
                  a.yaw == -0.25f && veq(a.rot, {7, 8, 9}) && b.speed_target == 0.01f && b.accel == 0.5f && a.speed == 0.001f &&
                  b.speed == 0.01f && veq(b.fwd, {1, 0, 0}) && veq(b.aim, {0, 1, 0}) && a.hold == 59 && b.hold == 59 && o.zava_reseat,
                  "E2 §5 S0: the fused head swaps position, target, forward, angles (no SetRot), index, f8 / fc; holds 60; Zava re-seated");
            const Boss06Effect* e = fx(o, 439, 0);
            bool blend0 = false;
            for (const Boss06Clip& c : o.clips) blend0 = blend0 || (c.head == 0 && c.slot == kWait && c.blend == 0);
            check(e && !e->stat && veq(e->pos, pa) && snd(o, 306) && blend0 && a.busy == 1 && a.free == 0 && n_fx(o, 439) == 2,
                  "E2 §5 S0: SetClip(WAIT, blend 0), FX2 439 at body_efc, SE 306, 0x114 = 1, 0x7f = 0");
            for (int f = 1; f < 60; ++f) r.step();
            const bool running = a.move == 12 && b.move == 12 && r.z.hits == 5;
            r.step();
            check(running && a.move == 0 && b.move == 0 && a.busy == 1 && b.busy == 0 && a.free == 1 && b.free == 1 && r.z.hits == 0,
                  "E2 §5 FIN: after 60 f: 0x7f = 1, Z+0x7a8 = 0, 0x114 cleared only on the head that is not fused");
        } else {
            check(veq(a.pos, pa) && veq(b.pos, pb) && a.wp == 3 && b.wp == 9,
                  "E2 §5: two fused heads both swap: a one-frame exchange, net none");
        }
    }
}

static void test_suction_and_bomb() {
    {
        Rig r;
        r.hb(1).pos = kFarB;
        r.b.set_pattern(1, 2);
        r.a().queued = 1;
        r.in.player_pos = {-20.0f, 7.1f, 20.0f};                   // dist^2 = 478 > 289
        Boss06Output o = r.step();
        const Vec3 d = {r.in.player_pos.x - r.a().pos.x, 1.0f, r.in.player_pos.z - r.a().pos.z};
        const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const Boss06Effect* e270 = fx(o, 270); const Boss06Effect* e348 = fx(o, 348);
        check(r.a().move == 13 && r.a().step == 2 && r.a().free == 0 && r.a().timer == 299 && r.a().speed_target == 0.0f &&
              r.a().accel == 0.01f && o.player_vel && vnear(*o.player_vel, {-0.1f * d.x / l, -0.1f / l, -0.1f * d.z / l}, 1e-6f),
              "D §3.1 / E2 §6: a idle, b in pattern 2, d > 17 -> move 13: brake, 300 f, player velocity SET to -0.1 * DirToP (3-D)");
        check(e270 && !e270->stat && e270->parent == Boss06Parent::kSlot && e270->parent_index == 0 && e270->at_scale.x == 1.5f &&
              e270->at_mode == 1 && snd(o, 303) && e348 && !e348->stat && veq(e348->scale, {1.5f, 1.5f, 1.5f}) &&
              e348->parent_index == 1 && e348->at_mode == 2 && e348->flag203 && veq(e348->pos, r.in.player_pos) &&
              nearf(r.a().slots[1].m.m[3][2], r.in.player_pos.z - 1.0f),
              "EF §F.2 cases 270 / 348: SE 303 + FX2 270 on the eye matrix (scale 1.5); disarmed marker 348 at the player + camera dir");
        r.in.player_section = 0x139;
        o = r.step();
        const bool burst = !o.player_vel;
        r.in.player_section = 0;
        r.in.player_pos = {r.a().pos.x + 3.0f, 6.1f, r.a().pos.z + 3.9f};   // 4.92 < 3 + 2
        o = r.step();
        const bool closed = r.a().step == 3 && r.a().timer == 119 && has(o.kill_ids, 270) && has(o.kill_ids, 348) && o.player_vel;
        for (int f = 1; f < 120; ++f) o = r.step();
        const bool waiting = r.a().move == 13;
        r.hb(1).pattern = 0; r.hb(1).queued = 0;
        o = r.step();
        check(burst && closed && waiting && r.a().move == 0 && r.a().free == 1 && has(o.kill_ids, 270) && has(o.kill_ids, 348),
              "E2 §6: no pull during a burst; the player within radius + 2 ends it; 120 f later StartMove(0), kill 270 + 348");
        Rig t;
        t.step();
        t.a().speed = 0.05f; t.start(0, 13);
        t.step();
        const bool braking = t.a().slots[0].id == 0 && t.a().step == 1 && nearf(t.a().speed, 0.04f, 1e-6f);
        for (int f = 0; f < 10; ++f) t.step();
        int fend = -1;
        for (int f = 0; f < 320 && fend < 0; ++f) { t.step(); if (t.a().step == 3) fend = f; }
        check(braking && fend >= 280 && fend <= 300, "E2 §6 S1/S2: waits until hb+0xf4 < 0.01 (0.01 a frame), then times out after 300 f");
    }
    {
        Rig r;
        r.a().pos = {-48.0f, 6.1f, 21.0f};
        r.b.set_pattern(0, 2);
        r.hb(1).queued = 1; r.forced = {0};
        r.in.player_pos = {r.hb(1).pos.x, 6.1f, 10.0f};            // dist^2 = 400: beyond the breath ring and > 100
        Boss06Output o;
        for (int f = 0; f < 60; ++f) o = r.step();
        const bool held = r.hb(1).move == 0 && r.hb(1).hold == 0;
        o = r.step();                                              // frame 60
        const bool started = r.hb(1).move == 14 && n_clip(o, 1, kAttack1) == 1 && r.hb(1).timer == 44;
        for (int f = 61; f < 105; ++f) o = r.step();
        const bool none = o.bullets.empty();
        o = r.step();                                              // frame 105
        const Vec3 m = r.hb(1).pos;
        check(held && started && none && o.bullets.size() == 1,
              "D §3.1 / E2 §7: b idle, hold 0, a in pattern 2, d > 10 -> move 14: ATTACK1, the bomb 45 f later");
        if (o.bullets.size() == 1) {
            const Boss06Bullet& b = o.bullets[0];
            check(b.kind == 4 && b.effect == 442 && vnear(b.pos, m, 1e-2f) && veq(b.dir, r.hb(1).fwd) && b.f[0] == 0.15f && b.f[1] == -0.0005f &&
                  b.f[2] == 0 && b.f[3] == 8.0f && b.f[4] == 3.0f && bits(b.f[5]) == 0 && bits(b.f[6]) == 0 && bits(b.f[7]) == 3 &&
                  bits(b.f[8]) == 0x3dd67750 && b.f[9] == 1.0f && b.f[10] == 20.0f && b.f[11] == 100.0f && b.f[12] == 0,
                  "E2 §7 [0x5421c7]: BUL kind 4, effect 442: 0.15, -0.0005, 0, 8, 3, (0, 0, 3), 0.10472, 1, 20, 100, 0");
            const Boss06Effect* g = fx(o, 321);
            check(g && g->stat && g->rot.y == 1.5707964f && g->parent == Boss06Parent::kNode && g->parent_index == kFireEfc &&
                  veq(g->at_off, {0, 0, -1.0f}) && g->at_mode == 1 && g->knock == 1.0f && snd(o, 305),
                  "E2 §7: glow FX2 321 rot (0, pi/2, 0), stat, attached to fire_efc mode 1 at (0, 0, -1); SE 305");
        }
        for (int f = 106; f < 152; ++f) r.step();
        const bool busy = r.hb(1).move == 14;
        r.a().pattern = 0; r.a().queued = 0;
        r.step();                                                  // frame 152 = 60 + 92
        const int holds[3] = {60, 78, 48};
        const Boss06Setup ss[3] = {setup(0), setup(0, true), setup(2)};
        bool hk = busy && r.hb(1).move == 0 && r.hb(1).hold == 59;
        for (int k = 1; k < 3; ++k) {
            Rig q(ss[k]);
            q.step(); q.start(1, 14);
            for (int f = 0; f <= 92; ++f) q.step();
            hk = hk && q.hb(1).move == 0 && q.hb(1).hold == holds[k] - 1;
        }
        check(hk, "E2 §7 S2: at ATTACK1's end (92 f) hold = trunc(hb+0x104 * 60): 60, 78 (VE / Easy), 48 (NM)");
    }
}

static void test_death() {
    Rig r;
    r.step();
    r.a().orb = {31, 32, 0, 0}; r.a().slots[0].id = 426; r.a().slots[0].handle = 77; r.a().slots[0].count = 1;
    r.a().queued = 0xff; r.hb(1).queued = 0xff;
    r.draws = 0;
    int f1[2] = {-1, -1}, f2[2] = {-1, -1}, fd[2] = {-1, -1}, black[2] = {-1, -1}, n67 = 0, rumA = 0, se572 = 0, se571 = 0;
    Boss06Output o0; Boss06Effect ring{}; float pitch_a = 0; bool alpha_ok = true, pre_ok = true;
    for (int f = 0; f <= 525; ++f) {
        const Boss06Output o = r.step();
        if (f == 0) o0 = o;
        for (int h = 0; h < 2; ++h) {
            if (o.materials_black[std::size_t(h)]) black[h] = f;
            if (n_clip(o, h, kPiyori1)) f1[h] = f;
            if (n_clip(o, h, kPiyori2)) {
                f2[h] = f;
                alpha_ok = alpha_ok && r.hb(h).alpha_target == 0.0f && bits(r.hb(h).alpha_step) == 0x3b888889;
                if (h == 0 && fx(o, 422, 0)) ring = *fx(o, 422, 0);
            }
            if (fd[h] < 0 && r.hb(h).dead) fd[h] = f;
            if (fd[h] < 0 && f2[h] >= 0 && f > f2[h]) pre_ok = pre_ok && r.hb(h).step == 4 && r.hb(h).pos.y > -1.0f;
        }
        n67 += n_fx(o, 67); se572 += snd(o, 572) != nullptr; se571 += snd(o, 571) != nullptr;
        for (int x : o.rumble) rumA += x == 4;
        if (f == 60) pitch_a = r.a().pitch;
    }
    check(has(o0.kill_handles, 31) && has(o0.kill_handles, 32) && has(o0.kill_ids, 426) && has(o0.kill_ids, 442) && has(o0.kill_ids, 321) &&
          snd(o0, 186) && n_clip(o0, 0, kLaser2) == 1 && n_clip(o0, 1, kLaser2) == 1 && !r.a().hittable && !r.hb(1).hittable,
          "D §2 DEATH / E2 §8 S0: entries un-hittable; orbiters, slots, 442, 321 killed; SE 186; LASER_2");
    int big = 0, small = 0;
    for (const Boss06Effect& e : o0.effects) if (e.id == 303) { big += e.scale.x == 3.0f && !e.stat && e.knock == 1.0f; small += e.scale.x == 2.0f; }
    check(black[1] == 0 && f1[1] == 0 && black[0] == 90 && f1[0] == 90 && big >= 5 && small == 1 && pitch_a > 0.3f,
          "E2 §8: head b dies at once (T = 0), head a 90 f later: materials black, FX2 303 (scale 3, 3 and 2), PIYORI_1; nose up");
    check(f2[0] == 170 && f2[1] == 100 && alpha_ok && ring.id == 422 && ring.pos.y == 6.01f && se571 == 2 && se572 == 1,
          "E2 §8 S3: at PIYORI_1's end (a 80 / b 100 f): alpha -> 0 at 1/240, PIYORI_2, FX2 422 at y 6.01, SE 571 (+ 572 for a)");
    check(fd[0] == 525 && fd[1] == 455 && pre_ok && r.a().step == 0x80 && r.hb(1).step == 0x80 && !r.a().collide && r.a().pos.y <= -1.0f,
          "E2 §8 S4/S5: 356 sink steps of 0.02 -> step 0x80, H+0x11 |= 2, H+0x60 &= ~0x80 (a at f + 525, b at f + 455)");
    check(n67 >= 22 && r.draws == 3 * n67 && rumA >= 5,
          "E2 §8 / §13: FX2 67 at body_efc +- 2 on every 30th fc of the sink (3 draws each); Rumble(4) for head a only");
}

static void test_orbiters() {
    Rig r;
    r.step();
    r.a().lives = 4; r.hb(1).lives = 4;
    r.feed();
    Boss06Output o;
    r.b.spawn_orbiters(0, r.in, o);
    r.b.spawn_orbiters(1, r.in, o);
    bool ok = n_fx(o, 424, 0) == 4 && n_fx(o, 425, 1) == 4 && o.effects.size() == 8;
    for (std::size_t k = 0; k < o.effects.size(); ++k) {
        const Boss06Effect& e = o.effects[k];
        ok = ok && e.call == Boss06Call::kFx2 && veq(e.rot, {1, 1, 1}) && veq(e.scale, {0.9f, 0.9f, 0.9f}) && e.stat && e.power == 0 &&
             e.knock == 0 && e.parent == Boss06Parent::kOrbit && e.parent_index == int(k % 4) && e.at_mode == 1 && e.flag201 &&
             e.handle != 0 && e.handle == r.hb(e.head).orb[k % 4] && veq(e.pos, r.hb(e.head).pos);
    }
    check(ok, "EF §F.3 FUN_0053eab0: one FX2 424 (a) / 425 (b) per life at body_efc, rot (1,1,1), scale 0.9, mode-1 attach to orbit k");
    r.z.present = true; r.z.hp = r.z.max_hp = 6000;
    const std::uint32_t fc = r.a().fc;
    o = r.step();
    const Vec3 body = r.a().pos;
    auto P = [&](int k) { return Vec3{r.a().orbit[std::size_t(k)].m[3][0], r.a().orbit[std::size_t(k)].m[3][1], r.a().orbit[std::size_t(k)].m[3][2]}; };
    auto rad = [&](int k) { const Vec3 e = P(k) - body; return std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z); };
    const Vec3 s01{P(0).x + P(1).x, P(0).y + P(1).y, P(0).z + P(1).z}, s23{P(2).x + P(3).x, P(2).y + P(3).y, P(2).z + P(3).z};
    // Orbiter 0: (0, 0, 3.5) yawed by a = trunc(1.5 * fc), tilted -45 deg about X, then the head's yaw pi (fwd = 0).
    const float a0 = float(int(1.5f * float(fc)) % 360) * 0.017453292f;
    const Vec3 l0{3.5f * std::sin(a0), 0, 3.5f * std::cos(a0)};
    const float c45 = 0.70710678f;
    const Vec3 t0{l0.x, l0.z * c45, l0.z * c45};                   // pitch -45: (x, y c + z s, -y s + z c) with s = -sin
    check(nearf(rad(0), 3.5f) && nearf(rad(1), 3.5f) && nearf(rad(2), 3.5f) && nearf(rad(3), 3.5f) &&
          vnear(s01, {2 * body.x, 2 * body.y, 2 * body.z}, 1e-4f) && vnear(s23, {2 * body.x, 2 * body.y, 2 * body.z}, 1e-4f) &&
          nearf(std::fabs(P(0).y - body.y), std::fabs(t0.y), 1e-3f) && o.effects.empty(),
          "EF §F.4: radius 3.5 around body_efc, phases 0 / 180 / 90 / 270, plane tilted 45 deg; no puffs above 40 % HP");
    r.z.hp = 0.3f * 6000;
    int a393 = 0, b385 = 0, other = 0, frames = 0; bool args = true;
    for (int f = 0; f < 40; ++f) {
        const std::uint32_t c = r.a().fc;
        o = r.step();
        frames += c % 10 == 0;
        for (const Boss06Effect& e : o.effects) {
            if (e.id == 393 && e.head == 0) { ++a393; args = args && e.call == Boss06Call::kFx && veq(e.scale, {1, 1, 1}) && veq(e.rot, {1, 1, 1}) && e.stat && e.knock == 0; }
            else if (e.id == 385 && e.head == 1) { ++b385; args = args && e.call == Boss06Call::kFx && e.scale.x == 0.01f; }
            else ++other;
        }
    }
    Rig e(setup(0, true));
    e.step(); e.a().orb = {1, 2, 3, 4}; e.z.present = true; e.z.hp = 1; e.z.max_hp = 100;
    int ep = 0;
    for (int f = 0; f < 20; ++f) ep += int(e.step().effects.size());
    check(frames == 4 && a393 == 8 && b385 == 8 && other == 0 && args && ep == 0,
          "EF §F.4: Normal+, Zava < 40 % HP: orbiters 0 and 2 drop FX 393 (a) / 385 scale 0.01 (b) every 10th frame; never on Easy");
    Rig q;
    q.step();
    q.a().orb = {0, 0, 9, 0}; q.z.present = true; q.z.hp = q.z.max_hp = 100;
    q.a().fc = 100;
    q.step();
    const Vec3 p2{q.a().orbit[2].m[3][0] - q.a().pos.x, q.a().orbit[2].m[3][1] - q.a().pos.y, q.a().orbit[2].m[3][2] - q.a().pos.z};
    // one orb: speed min(3.0, (4 - 1 + 1) * 1.5) = 3 deg/f -> a = 300 + 90 = 30 deg
    const float a2 = 30.0f * 0.017453292f;
    check(nearf(p2.x, -3.5f * std::sin(a2), 1e-3f) && q.a().orbit[0].m[3][0] == 0.0f,
          "EF §F.4: speed = min(3.0, (4 - n + 1) * factor) deg/f, angle = trunc(spd * fc) + phase; dead orbiters are skipped");
}

#include "zava.inl"   // Zava herself (brains/m870.cpp)

int main() {
    test_ctor();
    test_pattern0_and_queue();
    test_pattern1_and_mover();
    test_breath();
    test_ride_and_shake();
    test_moves_9_10();
    test_life_tick();
    test_fusion_break();
    test_stun_and_hit();
    test_pattern2();
    test_bite();
    test_laser();
    test_floor();
    test_dive();
    test_swap();
    test_suction_and_bomb();
    test_death();
    test_orbiters();
    zava_tests();
    std::printf("%d checks, %d failed\n%s\n", total, fails, fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
