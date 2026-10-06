// Headless tests for src/cleria/boss02.hpp — Nygtilger's object logic (build/boss_re/NYGTILGER.md),
// driven with a deterministic xorshift128, a stand-in clip clock and a scripted player.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>

#include "cleria/boss02.hpp"

using namespace cleria;
using namespace cleria::boss02;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss02] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near_eq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static float dist(const Vec3& a, const Vec3& b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

// The engine's xorshift128 (0x74622c), seeded deterministically; counts its draws.
struct XorShift {
    std::uint32_t x = 123456789, y = 362436069, z = 521288629, w = 88675123;
    int draws = 0;
    std::uint32_t operator()() {
        const std::uint32_t t = x ^ (x << 11);
        x = y; y = z; z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        ++draws;
        return w;
    }
};

// [STAND-IN] for MeshWorld's clip clock: a set clip is "done" once its length in ticks has run
// (frames / 0.25; §2.3), loops after one pass.
struct Clock {
    std::array<int, kObjects> clip{};
    std::array<int, kObjects> age{};
    Clock() { clip.fill(kStop); }
    static int ticks(int o, int c) {
        const bool head = o == 0;
        switch (c) {
            case kWalk: return head ? 64 : 60;
            case kStop: return 4;
            case kAttack: return 44;
            case kDamage: return 84;
            default: return 44;
        }
    }
    void apply(const Boss02Output& out) {
        for (const Boss02ClipSet& c : out.clips) { clip[std::size_t(c.object)] = c.clip; age[std::size_t(c.object)] = 0; }
    }
    void step(Boss02Input& in) {
        for (int o = 0; o < kObjects; ++o) {
            ++age[std::size_t(o)];
            in.clip_done[std::size_t(o)] = age[std::size_t(o)] >= ticks(o, clip[std::size_t(o)]);
        }
    }
};

// A scripted world: the player, the flags, the live bullets (a crude stand-in list).
struct Rig {
    XorShift rng;
    Clock clk;
    Boss02Input in;
    std::vector<Boss02Output> log;
    bool keep_log = false;
    int cosmetic_calls = 0;
    Rig() {
        in.rng = [this] { return rng(); };
        in.cosmetic = [this](const Boss02Cosmetic&) { ++cosmetic_calls; };
        in.player_pos = {0, 0, -17.265f};
        in.player_hp = 100;
    }
    Boss02Output step(Boss02& b) {
        clk.step(in);
        for (int u = 1; u < kPieces; ++u) {           // the segments' entry bones sit at the piece height
            const float y = b.object(u).pos().y;
            in.entry_bone_y[std::size_t(u)] = {y, y};
        }
        Boss02Output o = b.tick(in);
        clk.apply(o);
        if (o.player_pos) in.player_pos = *o.player_pos;
        for (const auto& f : o.flags) {
            if (f.first == 221) in.flag221 = f.second;
            if (f.first == 156) in.fight = f.second != 0;
        }
        if (keep_log) log.push_back(o);
        return o;
    }
};

static Boss02Setup setup(int diff, bool easy = false, bool vez = false) {
    Boss02Setup s;
    s.diff_index = diff;
    s.diff_easy = easy;
    s.diff_very_easy = vez;
    return s;
}

static bool has_sound(const Boss02Output& o, int id, Boss02Sound::Kind k = Boss02Sound::k2D) {
    for (const Boss02Sound& s : o.sounds) if (s.id == id && s.kind == k) return true;
    return false;
}
static bool has_effect(const Boss02Output& o, int id) {
    for (const Boss02Effect& e : o.effects) if (e.id == id) return true;
    return false;
}

// Kill every segment through its plate (and the tail through its part), one per tick.
static void kill_segments(Boss02& b, Rig& r) {
    for (int j = 1; j <= 17; ++j) {
        b.add_damage(kPieces - 1 + j, 400);
        r.step(b);
        r.step(b);
    }
    b.add_part_damage(kPieces - 1, 400);
    r.step(b);
    r.step(b);
}

int main() {
    // ---------------------------------------------------------------- §2.2 stats / the pool
    {
        struct Row { const char* name; int diff; bool easy; float head, seg, pool, def80, laser_cd; };
        const Row rows[] = {
            {"Normal", 0, false, 700, 350, 7000, 80, 140},
            {"Hard", 1, false, 750, 375, 7500, 81, 125},
            {"Nightmare", 2, false, 800, 400, 8000, 82, 110},
            {"Easy (0 retries)", 0, true, 600, 275, 5550, 75, 170},
        };
        for (const Row& r : rows) {
            Boss02 b(setup(r.diff, r.easy));
            char what[160];
            std::snprintf(what, sizeof what, "%-17s head %.0f seg %.0f pool %.0f DEF80 %.0f laser cd %.0f", r.name,
                          r.head, r.seg, r.pool, r.def80, r.laser_cd);
            check(near_eq(b.object(0).max_hp, r.head) && near_eq(b.object(1).max_hp, r.seg) &&
                      near_eq(b.object(18).max_hp, r.seg) && near_eq(b.pool(), r.pool) &&
                      near_eq(b.pool_max(), r.pool) && near_eq(b.stats().head_def_open, r.def80) &&
                      b.stats().laser_cd == int(r.laser_cd),
                  what);
        }
        Boss02 nm(setup(2));
        check(nm.stats().nightmare && near_eq(nm.stats().hoop_rate, 0.01f), "Nightmare: byte 0x767bae set, hoop lerp 0.01/frame");
        Boss02Setup e = setup(0, true);
        e.retries = 6;
        { Boss02 b(e); check(near_eq(b.stats().head_str, 95) && near_eq(b.stats().charged_str, 102) &&
                                 near_eq(b.stats().head_def_open, 72) && near_eq(b.stats().tail_def, 90),
                             "Easy, 6 retries: STR / DEF lowered by n>>1 = 3 (charged by 2n)"); }
        e.retries = 300;
        { Boss02 b(e); check(b.stats().head_str == 1.0f && b.stats().head_def_open == 0.0f,
                             "Easy, 300 retries: STR clamped to 1, DEF to 0"); }
        e.retries = 6; e.time_attack = true;
        { Boss02 b(e); check(near_eq(b.stats().head_str, 98), "Easy, time-attack: retries ignored"); }
    }

    // ---------------------------------------------------------------- §2.1 the object model
    {
        Boss02 b(setup(0));
        bool roles = b.object(0).role == kHead && b.object(18).role == kTail;
        for (int j = 1; j <= 17; ++j)
            roles &= b.object(j).role == kBody && b.object(18 + j).role == kTwin && b.object(18 + j).owner == j &&
                     b.object(j).twin == 18 + j;
        check(roles, "36 objects: head, b01..b17, c18 in the vector; d01..d17 twins of b01..b17");
        check(std::string_view(object_name(0)) == "boss02_h" && std::string_view(object_name(17)) == "boss02_b17" &&
                  std::string_view(object_name(18)) == "boss02_c18" && std::string_view(object_name(35)) == "boss02_d17",
              "object names boss02_h / b17 / c18 / d17");
        bool kp = true;
        for (int u = 0; u < kPieces; ++u) kp &= b.object(u).kp == 30 * u + 5;
        check(kp && b.object(19).idx == 2, "blk+8 = 30 idx + 5; a twin's idx = the next segment's");
        const auto& ord = b.tick_order();
        check(ord[0] == 0 && ord[1] == 1 && ord[2] == 19 && ord[3] == 2 && ord[35] == 18,
              "update order: head, b01, d01, b02, d02, ..., c18 [H U14]");
        check(b.object(0).def == 999.0f && b.object(1).def == 999.0f && near_eq(b.object(18).def, 98) &&
                  near_eq(b.object(19).def, 98),
              "DEF: head 999, bodies 999, tail 98, plates 98");
        check(b.object(0).has_part && b.object(18).has_part && !b.object(1).has_part &&
                  near_eq(b.object(0).part.hp, 7000) && near_eq(b.object(18).part.hp, 3500),
              "hurt parts only on the head and the tail, part HP = 10 x HP");
        check(b.object(0).frozen && b.object(0).stone == 1.0f && b.object(18).cracked && !b.object(1).cracked,
              "ctor: head frozen, stone shell 1.0, the tail starts 'cracked'");
        // The story pre-roll: coiled around the pillar on the annulus.
        bool ring = true;
        for (int u = 0; u < kPieces; ++u) {
            const Vec3 p = b.object(u).pos();
            const float r = std::sqrt(p.x * p.x + p.z * p.z);
            ring &= r > 8.5f && r < 12.0f && p.y >= 1.65f;
        }
        check(ring, "pre-roll(2): every piece coiled around the pillar (r 8.5..12), y >= 1.65");
        const float lr = std::sqrt(b.group().lead.x * b.group().lead.x + b.group().lead.z * b.group().lead.z);
        check(lr >= 9.25f - 1e-3f && lr <= 9.6f, "pre-roll(2): the leader on the inner clamp r ~ 9.25 (coil 1)");
        {
            Boss02Setup s = setup(0);
            s.character = 3;
            Boss02 t(s);
            check(near_eq(t.object(1).resist[0], -0.33333f) && near_eq(t.object(0).part.resist[2], -0.66667f),
                  "Toal: resist +0xb4 -1/3, +0xbc -2/3 on pieces and parts");
        }
    }

    // ---------------------------------------------------------------- dormant, wake, fight start
    {
        Boss02 b(setup(0));
        Rig r;
        for (int i = 0; i < 30; ++i) r.step(b);
        check(r.rng.draws == 0 && b.mode() == 0 && b.object(0).frozen, "dormant statue: mode 0, frozen, no RNG draw");
        const Vec3 p5 = b.object(5).pos();
        r.in.wake = 1;
        Boss02Output o = r.step(b);
        check(b.group().awake && b.group().dormant && !b.object(0).frozen && b.mode() == 0,
              "flag 166: awake, mode 0, unfrozen, the dormant snapshot");
        // The head rolled its stone before the driver woke it: 18 R(300), then each body's
        // glow roll R(150) (mode 0 is not 4; no ball without G+0xd0) — unless a stone cracked.
        int crack0 = 0;
        for (const auto& e : o.effects) crack0 += e.id == 0x156;
        check(crack0 == 0 && r.rng.draws == 18 + 17, "wake frame: 18 stone rolls R(300) + 17 glow rolls R(150)");
        int cracks = 0;
        float drift = 0;
        for (int i = 0; i < 300; ++i) {
            o = r.step(b);
            for (const auto& e : o.effects) cracks += e.id == 0x156;
            drift = std::max(drift, dist(b.object(5).pos(), p5));
        }
        std::printf("[boss02] dormant: b05 strays at most %.3f from its snapshot, %d stone cracks\n", drift, cracks);
        // Bounded, not accumulating: M = RotX(v) x snapshot each frame, plus the 70 % chain pull
        // (the snapshot was snapped with a = -1.0 for every piece, the dormant follow uses -1.2).
        check(drift < 0.5f && cracks > 0 && b.object(1).stone < 1.0f,
              "dormant wobble keeps the pose; the stone shells crack (effect 342)");
        r.in.fight = true;
        o = r.step(b);
        check(b.mode() == 1 && b.group().started && !b.group().dormant && o.gauge && near_eq(o.gauge->hp, 7000) &&
                  near_eq(o.gauge->max, 7000) && near_eq(o.gauge->len_scale, 4500) && o.gauge->strip == 2,
              "flag 156: mode 1, follow on, HP bar = pool 7000 / 7000, len 4500, strip 2");
        check((b.object(1).f298 & 8) && (b.object(19).f298 & 8), "fight start: +0x298 |= 8 on pieces and twins");
        r.in.player_pos = {0, 0, -2};
        o = r.step(b);
        check(o.player_pos && near_eq(std::sqrt(o.player_pos->x * o.player_pos->x + o.player_pos->z * o.player_pos->z), 9.2f),
              "player ring clamp: pushed out to r = 9.2");
        r.in.player_pos = {0, 0, -25};
        o = r.step(b);
        check(o.player_pos && near_eq(-o.player_pos->z, 19.45f), "player ring clamp: pulled in to r = 19.45");
        r.in.player_pos = {0, 0, -14};
        // The chain: run the coil a while and measure the spacing (§5.2).
        for (int i = 0; i < 400; ++i) r.step(b);
        float mn = 1e9f, mx = 0, gap = 0, sum = 0;
        for (int u = 1; u < kPieces; ++u) {
            const float d = dist(b.object(u).pos(), b.object(u - 1).pos());
            mn = std::min(mn, d); mx = std::max(mx, d); sum += d;
            gap = std::max(gap, dist(b.object(u).front, b.object(u - 1).rear));
        }
        std::printf("[boss02] chain: centre spacing %.3f..%.3f (mean %.3f), max front-to-rear gap %.4f\n", mn, mx,
                    sum / 18.0f, gap);
        // Front / rear sit 1.2 ahead / behind each centre (head front 1.0): a straight chain is
        // 2.4 apart; the coil's curvature and the r 9.25 / 9.6 clamps (they move only the centre)
        // shorten some chords.
        check(mn > 2.1f && mx < 2.45f && sum / 18.0f > 2.3f && gap < 0.1f,
              "chain follow: each front pulled 70 %/frame onto the previous rear, spacing ~2.4");
        bool fl = true;
        for (int u = 0; u < kPieces; ++u) fl &= b.object(u).pos().y >= 1.65f - 1e-4f;
        check(fl, "floor: every piece y >= 1.65");
        check(b.object(0).hurts && !b.object(1).hurts && b.object(0).str == 100.0f && b.object(1).str == 103.0f,
              "stats: the head hurts on touch (STR 100), bodies don't (STR 103)");
    }

    // ---------------------------------------------------------------- §3 damage routing
    {
        Boss02 b(setup(0));
        Rig r;
        r.in.fight = true;
        r.step(b);
        const int d01 = kPieces;                       // object 19
        check(b.mesh_hittable(d01) && b.mesh_hittable(1), "plates and bodies take mesh rays after the fight start");
        Boss02Output fb;
        check(b.accept_hit(1, {1, 2, 3}, 1000, fb) && has_effect(fb, 0x158) && fb.sounds.empty(),
              "accept-hit on an armoured body: accepted, body fluid only (DEF 999)");
        Boss02Output fb2;
        check(b.accept_hit(d01, {1, 2, 3}, 1100, fb2) && fb2.effects.empty(), "accept-hit: the 150 ms feedback throttle");
        b.add_damage(d01, 100);
        r.step(b);
        check(near_eq(b.object(1).hp, 250) && near_eq(b.object(d01).hp, 250) && near_eq(b.pool(), 7000) &&
                  b.object(1).flash > 1.5f && !b.object(1).cracked,
              "plate hit 100 -> its segment HP 350 -> 250 (after b01 ticked); the pool is untouched");
        Boss02Output o = r.step(b);                     // b01 latches the crack (< 75 %), d01 swaps the same frame
        check(b.object(1).cracked && has_sound(o, 212) && b.object(d01).broken,
              "segment below 75 %: cracked; its plate swaps to BBOSS02D with SE 212");
        bool armor = false;
        for (int i = 0; i < 14; ++i) { o = r.step(b); armor |= has_effect(o, 0x15a); }
        check(armor, "12 frames after the crack: effect 346 bs_02_armorbreak");
        b.add_damage(d01, 300);
        o = r.step(b);                                  // the plate passes it on
        check(b.object(1).hp == 0.0f && near_eq(b.pool(), 7000), "kill frame: segment HP clamped to 0 (< 2)");
        o = r.step(b);                                  // the segment sees its HP change next tick
        check(near_eq(b.pool(), 6650) && b.object(1).broken && has_sound(o, 211) && has_effect(o, 0x159),
              "a dead segment: pool -= its max 350; BBOSS02B swap, SE 211, crush smoke 345");
        o = r.step(b);                                  // the head (first in order) refreshes the bar
        check(o.gauge && near_eq(o.gauge->hp, 6650), "HP bar drops by the whole 350 at once (next head tick)");
        int n = 0;
        while (b.object(d01).exists && n < 100) { r.step(b); ++n; }
        check(!b.object(d01).exists && b.object(1).twin == -1 && n > 25 && n < 40,
              "the dead segment's plate fades (0.03/frame) and destroys itself");
        // Every other segment and the tail.
        for (int j = 2; j <= 17; ++j) { b.add_damage(kPieces - 1 + j, 400); r.step(b); r.step(b); }
        check(b.phase() == 2 && b.object(0).def == 999.0f, "pool <= 50 %: phase 2; the head still DEF 999");
        b.add_part_damage(18, 400);
        r.step(b);
        o = r.step(b);
        check(near_eq(b.pool(), 700) && b.phase() == 3, "all 18 segments dead: pool = head max 700, phase 3");
        o = r.step(b);
        check(near_eq(b.object(0).def, 80) && near_eq(b.object(0).part.def, 80) && b.object(0).broken &&
                  !b.object(0).hurts,
              "phase 3: the head's shell breaks, DEF 80 (part too), no touch damage");
        b.add_part_damage(0, 100);
        o = r.step(b);
        check(near_eq(b.pool(), 600) && o.gauge && near_eq(o.gauge->hp, 600), "head hits move the pool continuously");
        r.in.player_hp = 0;
        r.step(b);
        check(b.object(0).def == 999.0f, "player dead: head DEF 999");
    }

    // ---------------------------------------------------------------- §8 death and flag 221
    for (int variant = 0; variant < 3; ++variant) {    // 0 story, 1 time-attack, 2 player dead
        Boss02Setup s = setup(0);
        s.time_attack = variant == 1;
        Boss02 b(s);
        Rig r;
        r.in.fight = true;
        r.step(b);
        kill_segments(b, r);
        r.step(b);
        if (variant == 2) r.in.player_hp = 0;
        b.add_part_damage(0, 5000);
        int k = 0, flag221_tick = -1, f156_tick = -1, sting = 0, exp = 0, end = 0;
        bool kill_all = false, iframes = false;
        while (!b.finished() && k < 2000) {
            const Boss02Output o = r.step(b);
            ++k;
            for (const auto& f : o.flags) {
                if (f.first == 221 && f.second == 1 && flag221_tick < 0) flag221_tick = k;
                if (f.first == 156 && f.second == 0 && f156_tick < 0) f156_tick = k;
            }
            sting += has_sound(o, 228);
            exp += o.award_exp;
            end += o.end_block;
            kill_all |= o.kill_all_effects;
            iframes |= o.player_iframes;
        }
        std::printf("[boss02] death variant %d: finished after %d ticks, 221 @%d, 156=0 @%d\n", variant, k, flag221_tick,
                    f156_tick);
        if (variant == 0) {
            check(b.phase() == 4 && flag221_tick > 0 && flag221_tick <= 2 && sting == 1 && iframes && kill_all,
                  "story kill: phase 4, flag 221 = 1 on the head's first death frame, SE 228 once, i-frames");
            check(exp == 1 && end == 1 && k > 200 && k < 320, "the head fades out ~250 frames later: EXP once, the end block");
        } else if (variant == 1) {
            check(flag221_tick < 0 && f156_tick > 0 && exp == 0, "time-attack: no flag 221; flag 156 = 0 at head alpha <= 0.2");
        } else {
            check(flag221_tick < 0 && exp == 0 && sting == 0, "player dead on the kill: no flag 221, no EXP, no sting");
        }
        bool hidden = true;
        for (int u = 0; u < kPieces; ++u) hidden &= (b.object(u).f298 & 0x40) != 0;
        check(hidden, "death: every piece uncollided (+0x298 |= 0x40)");
    }

    // ---------------------------------------------------------------- the kind-0x24 marker
    {
        Boss02Plasma m;
        m.carrier = 3;
        m.local = {0, 2.7f, 0.8f};
        m.target = {12, 10, 0};
        Mat4 w;
        w.m[12] = 11; w.m[13] = 3; w.m[14] = 0;
        int sec = 0;
        Boss02Plasma::Step st;
        for (int i = 0; i < 59; ++i) st = m.step({15, 0, 0}, &w, 350, {}, sec);
        check(near_eq(m.pos.x, 11) && near_eq(m.pos.y, 5.7f) && !st.killed, "0x24: glued to its segment for 60 frames");
        for (int i = 0; i < 240; ++i) st = m.step({15, 0, 0}, &w, 350, {}, sec);
        check(!st.strike && !m.dead && m.pos.y > 9.0f, "0x24: drifts to T (y 10) by frame 299");
        auto ground = [](const Vec3&, float& t) { t = 0.5f; return true; };
        st = m.step({15, 0, 0}, &w, 350, ground, sec);
        check(st.strike && st.killed && near_eq(st.strike_pos.y, 4.0f) && st.strike_se == 221 && sec == 1,
              "0x24: strike at frame 300 (y = t + 3.5), SE 221/222/223 by the counter, then dies");
    }

    // ---------------------------------------------------------------- smoke: modes 1, 3, 4
    {
        Boss02 b(setup(0));
        Rig r;
        r.in.fight = true;
        r.in.player_pos = {0, 0, -14};
        std::set<int> modes;
        int balls24 = 0, balls23 = 0, lasers = 0, bites = 0, smokes = 0, ticks = 0;
        bool hoop_ok = false, charged = false, back3 = false, killed_segs = false;
        int mode4_ticks = 0;
        while (ticks < 20000) {
            // The player orbits slowly at r 14.
            const float a = float(ticks) * 0.004f;
            r.in.player_pos = {std::cos(a) * 14.0f, 0, std::sin(a) * 14.0f};
            if (!killed_segs && ticks == 2000) {       // halve the pool: arm the roll
                for (int j = 1; j <= 11; ++j) b.add_damage(kPieces - 1 + j, 400);
                killed_segs = true;
            }
            const int m0 = b.mode();
            const Boss02Output o = r.step(b);
            ++ticks;
            modes.insert(b.mode());
            for (const Boss02Bullet& bl : o.bullets) { balls24 += bl.kind == 0x24; balls23 += bl.kind == 0x23; }
            lasers += has_effect(o, 0x15f);
            smokes += has_effect(o, 0x15e);
            for (const Boss02ClipSet& c : o.clips) bites += c.object == 0 && c.clip == kAttack;
            if (b.mode() != m0) std::printf("%d->%d@%d ", m0, b.mode(), ticks);
            if (b.mode() == 4) {
                ++mode4_ticks;
                charged |= b.group().charged;
                if (b.group().hs == 2 && !hoop_ok) {
                    const Vec3 c{b.group().hp_at.x, 8.9f, b.group().hp_at.z};
                    bool on = true;
                    for (int u = 0; u < kPieces; ++u) on &= near_eq(dist(b.object(u).pose_pos, c), 7.25f, 1e-2f);
                    hoop_ok = on;
                }
            }
            if (m0 == 4 && b.mode() == 3) back3 = true;
            if (back3 && ticks > 2000 && modes.count(1) && modes.count(3)) break;
        }
        std::printf("\n[boss02] smoke: %d ticks, plasma %d, poison %d, laser %d, bite %d, smoke %d, mode-4 ticks %d\n",
                    ticks, balls24, balls23, lasers, bites, smokes, mode4_ticks);
        check(modes.count(1) && modes.count(3), "smoke: mode 1 (coil) -> mode 3 (hunt)");
        check(balls24 > 0, "smoke: segments fire plasma balls (kind 0x24) from the coil");
        check(lasers > 0 && bites > 0, "smoke: the mouth laser (351) and the bite lunge in mode 3");
        check(balls23 > 0, "smoke: dead husks spit poison balls (kind 0x23)");
        check(modes.count(4) && hoop_ok && charged && smokes > 0, "smoke: mode 4 hoop (r 7.25 about y 8.9), charged, smoke puffs");
        check(back3, "smoke: the hoop brakes and returns to mode 3");
    }

    std::printf("[boss02] %s (%d failure%s)\n", fails ? "FAILED" : "ALL OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
