// Headless tests for src/cleria/boss03.hpp — Gelaldy's object logic (build/boss_re/GELALDY.md),
// driven with a deterministic xorshift128, a stand-in clip clock, stand-in node positions and a
// scripted player.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>
#include <vector>

#include "cleria/boss03.hpp"

using namespace cleria;
using namespace cleria::boss03;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss03] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near_eq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

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

// [STAND-IN] for the mesh layer's clip clock: F advances by rate x speed per frame from the
// section start; a stop clip clamps at its end, a loop wraps; either bumps the count.
struct Clock {
    int clip = kAll;
    float frame = 15.0f;
    int count = 0;
    float speed = 1.0f;
    void apply(const Boss03Output& o) {
        for (const Boss03ClipSet& c : o.clips) {
            clip = c.clip;
            count = 0;
            frame = c.frame ? *c.frame : float(kClipTable[c.clip].start);
        }
        if (o.anim_speed) speed = *o.anim_speed;
    }
    void step() {
        const ClipInfo& ci = kClipTable[clip];
        frame += ci.rate * speed;
        if (frame >= float(ci.end)) {
            if (ci.loop) frame -= float(ci.end - ci.start);
            else frame = float(ci.end);
            ++count;
        }
    }
};

// [STAND-IN] node offsets in the boss's model frame (front = -z at yaw 0).
static const Vec3 kNodeLocal[kNodes] = {
    {0, 3, -1.5f},  {-5, 3, -2}, {-6, 1, -5}, {5, 3, -2},  {6, 1, -5}, {-5.5f, 2, -4}, {5.5f, 2, -4}, {0, 5, -3},
    {0, 5.5f, -1},  {-6, 1, -6}, {6, 1, -6},  {-5, 2.5f, -3}, {5, 2.5f, -3}, {0, 6, -2},  {0, 0, 0},
};

struct Rig {
    XorShift rng;
    Clock clk;
    Boss03Input in;
    int shake_frames = 0;
    Vec3 shake_amp{};
    int shake_draws = 0;
    std::vector<Boss03Output> log;
    bool keep_log = false;
    Rig() {
        in.rng = [this] { return rng(); };
        in.player_pos = {0, -1.5f, -17.477f};
        in.player_hp = 100;
        in.fight = true;
        in.cam_pitch = 0.1f;
        in.cam_up = 2.0f;
        in.cam_dist = 20.0f;
    }
    void start(const Boss03& b) { clk.apply(b.ctor_requests()); }
    Boss03Output step(Boss03& b) {
        clk.step();
        in.clip = clk.clip;
        in.clip_done = clk.count != 0;
        in.clip_frame = clk.frame;
        const Mat4 m = b.matrix();
        for (int n = 0; n < kNodes; ++n) in.nodes[std::size_t(n)] = m.transform_point(kNodeLocal[n]);
        in.shake_amp = shake_amp;
        Boss03Output o = b.tick(in);
        clk.apply(o);
        if (o.camera.pitch) in.cam_pitch = *o.camera.pitch;
        if (o.camera.up) in.cam_up = *o.camera.up;
        if (o.camera.dist) in.cam_dist = *o.camera.dist;
        if (o.shake.frames) shake_frames = *o.shake.frames;
        if (o.shake.amp) shake_amp = *o.shake.amp;
        const int before = rng.draws;                  // the camera step (FUN_00427c90)
        camera_shake(shake_frames, shake_amp, in.rng);
        shake_draws += rng.draws - before;
        for (const auto& f : o.flags)
            if (f.first == 156) in.fight = f.second == 1;
        in.player_pos.x += o.player_push.x;
        in.player_pos.z += o.player_push.z;
        in.perrie_count += int(o.spawns.size());
        if (o.kill_perries) in.perrie_count = 0;
        if (keep_log) log.push_back(o);
        return o;
    }
};

static Boss03Setup setup(int diff, bool easy = false, int chr = 0) {
    Boss03Setup s;
    s.diff_index = diff;
    s.diff_easy = easy;
    s.character = chr;
    s.exp_b0 = 1.0f;
    return s;
}

static bool has_effect(const Boss03Output& o, int id) {
    for (const Boss03Effect& e : o.effects) if (e.id == id) return true;
    return false;
}
static bool has_sound(const Boss03Output& o, int id) {
    for (const Boss03Sound& s : o.sounds) if (s.id == id) return true;
    return false;
}

// Runs until pred() or n frames; returns the frames run.
template <class P>
static int run_until(Boss03& b, Rig& r, int n, P pred) {
    for (int i = 0; i < n; ++i) {
        if (pred()) return i;
        r.step(b);
    }
    return n;
}

// Breaks arm a (0 right / 1 left) with 100-damage hits while it is unguarded.
static void break_arm(Boss03& b, Rig& r, int a) {
    const int part = a == 0 ? 1 : 3;
    for (int i = 0; i < 4000 && b.arm_alive(a); ++i) {
        if (b.part_hittable(part) && b.part(part).hurt_centre.y > -500.0f && i % 4 == 0) b.add_part_damage(part, 100);
        r.step(b);
    }
}

int main() {
    // ---------------------------------------------------------------- §2.4 stats by difficulty
    {
        struct Row {
            const char* name; int diff; bool easy; bool f201; float arm, rc0, ra0, off;
            int n02, n13, rnd, aim, cd, shrt, nearr, perr, punch; float far_, near_;
        };
        const Row rows[] = {
            {"Normal", 0, false, false, 800, .37f, .35f, 16, 20, 35, 15, 25, 90, 15, 2, 1, 15, .00795f, .0098f},
            {"Hard", 1, false, false, 900, .67f, .5f, 16, 25, 35, 15, 20, 80, 15, 3, 2, 15, .00795f, .00988f},
            {"Nightmare", 2, false, false, 1800, .67f, .5f, 16, 30, 40, 15, 20, 70, 18, 5, 5, 15, .00795f, .0099f},
            {"Easy", 0, true, false, 800, .37f, .35f, 20, 15, 25, 20, 80, 150, 10, 2, 1, 14, .00795f, .0098f},
            {"flag 201 (bb5c 3)", 3, false, true, 800, .37f, .35f, 16, 30, 40, 15, 20, 70, 18, 5, 5, 15, .00795f, .0099f},
        };
        for (const Row& w : rows) {
            Boss03Setup s = setup(w.diff, w.easy);
            s.flag201 = w.f201;
            Boss03 b(s);
            char what[200];
            std::snprintf(what, sizeof what, "%-17s body 1500, arms %.0f, core res %.2f, arm res %.2f, off %.0f", w.name,
                          w.arm, w.rc0, w.ra0, w.off);
            check(b.hp() == 1500.0f && b.max_hp() == 1500.0f && b.part(0).hp == 20000.0f && b.part(2).hp == 15000.0f &&
                      b.part(1).hp == w.arm && b.part(3).max_hp == w.arm && near_eq(b.part(0).resist[0], w.rc0) &&
                      near_eq(b.part(1).resist[0], w.ra0) && b.dmg_offset() == w.off,
                  what);
            std::snprintf(what, sizeof what, "%-17s FIRE %d/%d, pillars %d/%d cd %d, hold %d, near %d, perrie %d, aim %d",
                          w.name, w.n02, w.n13, w.rnd, w.aim, w.cd, w.shrt, w.nearr, w.perr, w.punch);
            check(b.fire_n(0) == w.n02 && b.fire_n(1) == w.n13 && b.pillar_every(false) == w.rnd &&
                      b.pillar_every(true) == w.aim && b.pillar_cooldown() == w.cd && b.short_hold() == w.shrt &&
                      b.near_steps() == w.nearr && b.perrie_n() == w.perr && b.punch_aim() == w.punch &&
                      b.nagi_far() == w.far_ && b.nagi_near() == w.near_,
                  what);
        }
        {
            Boss03 h(setup(0, false, 2));
            check(h.part(0).resist[0] == 0.0f && h.part(1).resist[0] == 0.0f && near_eq(h.part(1).resist[1], .39f) &&
                      h.punch_aim() == 13 && h.nagi_near() == .0091f,
                  "Hugo: resist slot 0 = 0 on the core and both arms; PUNCH aim 13");
            Boss03 hh(setup(1, false, 2));
            check(hh.punch_aim() == 14 && hh.nagi_near() == .0099f, "Hard Hugo uses the Nightmare row (jump table 0x5210e8)");
            Boss03 y(setup(0, false, 1));
            check(y.punch_aim() == 14 && y.nagi_far() == .0085f && y.nagi_near() == .0105f,
                  "Normal Yunica takes the Easy row's code path");
            Boss03 t(setup(1, false, 3));
            check(t.punch_aim() == 16 && t.nagi_far() == .01f && t.nagi_near() == .0121f, "Hard Toal: 16 / .01 / .0121");
        }
        {
            Boss03Setup s = setup(0, true);
            s.retries = 6;
            Boss03 e6(s);
            s.retries = 30;
            Boss03 e30(s);
            s.diff_very_easy = true;
            s.retries = 16;
            Boss03 ve(s);
            s.boss_rush = true;
            Boss03 rush(s);
            check(e6.str() == 147.0f && e6.def() == 137.0f && e6.part(1).str == 147.0f && e6.part(0).def == 137.0f,
                  "Easy, 6 retries: STR/DEF -3 (n>>1), copied into every part");
            check(e30.str() == 130.0f && e30.def() == 120.0f, "Easy, 30 retries: -(n-10) = -20, no clamp");
            check(near_eq(ve.str(), 118.0f) && near_eq(ve.def(), 108.0f), "Very Easy, 16 retries: -pow(16, 1.25) = -32");
            check(rush.str() == 150.0f && rush.def() == 140.0f, "boss rush: no retry softening");
        }
    }

    // ---------------------------------------------------------------- §4.3 the sequences
    {
        const auto n = sequence(false, 0), e = sequence(true, 0), en = sequence(true, 2), nm = sequence(false, 2);
        const int ph0[11] = {2, 3, 4, 2, 3, 5, 2, 3, 0xc, 2, 6};
        bool ok0 = true;
        for (int i = 0; i < 11; ++i) ok0 &= n[std::size_t(i)] == ph0[i];
        check(ok0 && n[12] == 0xd && n[17] == 0xb && n[27] == 7 && n[34] == 1 && n[36] == 0xb,
              "Normal: R P S R P B R P F R M | R B' ... N ... | R T R M R T | R T - R N R M R T");
        check(e[10] == 3 && e[34] == 1 && en[10] == 3 && en[34] == 0xc, "Easy: idx 10 M -> P, idx 34 F -> skip (not with bb5c 2)");
        check(nm[17] == 3 && nm[18] == 0xc && nm[19] == 2 && nm[20] == 0xb && nm[34] == 2 && nm[35] == 0xb && nm[36] == 2 &&
                  nm[37] == 6 && nm[38] == 0xc && nm[10] == 6,
              "Nightmare: idx 17..20 P F R N, idx 34..38 R N R M F");
        Boss03 b(setup(1));
        check(b.seq() == sequence(false, 1) && b.seq_index() == 1 && b.code() == kCodePunch && b.phase() == 0,
              "ctor: phase 0, the first attack is seq[1] = PUNCH (index starts at 1)");
    }

    // ---------------------------------------------------------------- idle, intro hold, ctor
    {
        Boss03 b(setup(0));
        Rig r;
        const Boss03Output& c = b.ctor_requests();
        check(c.clips.size() == 1 && c.clips[0].clip == kWait1 && c.effects.size() == 1 && c.effects[0].id == 377 &&
                  c.effects[0].node == kNCore && c.effects[0].slot == Boss03Effect::kCore,
              "ctor: clip WAIT1, core glow 377 attached to Core (slot)");
        check(near_eq(b.facing(), -1.5707964f) && std::fabs(b.yaw()) < 1e-5f, "ctor: facing -pi/2, body yaw 0");
        r.start(b);
        r.in.fight = false;
        Boss03Output o;
        bool quiet = true;
        for (int i = 0; i < 50; ++i) {
            o = r.step(b);
            quiet &= o.clips.empty() && o.effects.empty() && !o.gauge && !o.pose && o.sounds.empty();
        }
        check(quiet && r.rng.draws == 0 && b.intro() == 0, "flag 156 = 0: the brain returns at once (no tail, no RNG)");
        r.in.fight = true;
        o = r.step(b);
        check(o.gauge && o.gauge->hp == 1500.0f && o.gauge->max == 1500.0f && o.gauge->len_scale == 5500.0f &&
                  o.gauge->emblem == 3 && o.pose && o.arm_gauge,
              "flag 156: HP bar 1500/1500 (len 5500, emblem 3), poses, the driver runs");
        int hold = 1;
        while (b.intro() < 120 && hold < 500) { r.step(b); ++hold; }
        check(hold == 120 && b.code() == kCodePunch && r.clk.clip == kWait1 && r.rng.draws == 0,
              "intro: WAIT1 held for 120 brain frames, no attack");
        r.step(b);
        check(r.clk.clip == kRPunchi || r.clk.clip == kLPunchi, "frame 121: PUNCH starts");
        check(b.arm_guard(0) && b.arm_guard(1), "PUNCH guards both arms from its first frame");
        // The guarded-arm rule: a hit consumed while the arm is guarded leaves it at 1 HP.
        b.add_part_damage(1, 100);
        r.step(b);
        check(b.part(1).hp == 1.0f && b.arm_alive(0), "a guarded arm hit drops it to exactly 1 HP (HP != 0 -> 1.0, literal)");
    }

    // ---------------------------------------------------------------- §3.2 double-counted arm damage, §5.10 the break
    {
        Boss03 b(setup(0));
        Rig r;
        r.start(b);
        for (int i = 0; i < 10; ++i) r.step(b);           // inside the intro hold: arms unguarded
        b.add_part_damage(1, 100);
        check(b.part(1).hp == 700.0f && b.part(1).dmg == 100.0f, "hit path: right arm 800 -> 700, +0x134 = 100");
        Boss03Output o = r.step(b);
        check(b.part(1).hp == 600.0f && b.part(1).dmg == 0.0f, "driver subtracts +0x134 again: 700 -> 600 (G9)");
        check(has_effect(o, 431) && has_sound(o, 239) && b.flash() == 0.0f, "arm hit: blood 431 at RightForeArm1, SE 239, no flash");
        check(o.arm_gauge && o.arm_gauge->right && o.arm_gauge->timer == 254 && o.arm_gauge->hp == 600.0f &&
                  o.arm_gauge->max == 800.0f,
              "arm mini-gauge: 255 frames (-1 this frame), 600 / 800, right");
        for (int k = 0; k < 2; ++k) { b.add_part_damage(1, 100); r.step(b); }
        check(b.part(1).hp == 200.0f && b.arm_alive(0), "300 dealt: right arm 200 / 800");
        b.add_part_damage(1, 100);                         // 400 dealt: 100 -> 0
        o = r.step(b);
        check(b.part(1).hp <= 0.0f && b.phase() == kPhaseSpecial && b.special() == 9 && b.arm_gauge().timer == 0,
              "arm break after 400 dealt (800 HP, double-counted): special 9, gauge cleared");
        check(r.clk.clip == kWait1, "the break during the intro: the hold still wins until frame 120");
        run_until(b, r, 400, [&] { return b.intro() >= 120; });
        o = r.step(b);
        check(r.clk.clip == kRDamage && !b.arm_alive(0) && b.arm_alive(1) && has_sound(o, 240),
              "special 9: RDAMAGE, right arm dead, SE 240");
        r.keep_log = true;
        run_until(b, r, 400, [&] { return b.phase() != kPhaseSpecial; });
        int neji = 0, maguma = 0, rumble0 = 0, se806 = 0;
        for (const auto& l : r.log) {
            for (const auto& e : l.effects) { neji += e.id == 432; maguma += e.id == 419; }
            for (int n : l.rumble) rumble0 += n == 0;
            se806 += has_sound(l, 806);
        }
        r.log.clear();
        r.keep_log = false;
        check(neji == 1 && maguma == 1 && se806 == 1 && rumble0 == 1,
              "the screw 432 and the hand-in-lava 419 (one-hit splash) once each, SE 806, rumble 0");
        check(b.phase() == 0 && b.core_guard() && !b.core_exposed(),
              "after RDAMAGE: next attack in phase 0 (one arm alive), core still guarded");
        const auto pose = r.step(b).pose;
        check(pose && pose->forearm_gone[0] && !pose->forearm_gone[1], "the broken arm's forearm is thrown away (-5000)");
    }

    // ---------------------------------------------------------------- §3.4 core exposure, the 375 stagger, the regrowth
    {
        Boss03 b(setup(0));
        Rig r;
        r.start(b);
        for (int i = 0; i < 5; ++i) r.step(b);
        b.add_part_damage(1, 400);
        b.add_part_damage(3, 400);                         // both arms in one frame
        r.step(b);
        check(b.special() == 9 && b.part(1).hp <= 0.0f && b.part(3).hp <= 0.0f, "both arms break the same frame");
        run_until(b, r, 400, [&] { return b.intro() >= 120; });
        r.keep_log = true;
        bool core02 = false;
        run_until(b, r, 400, [&] {
            if (b.core_exposed()) core02 = true;
            return b.phase() != kPhaseSpecial;
        });
        bool fx499 = false, se250 = false, dd = false;
        for (const auto& l : r.log) {
            fx499 |= has_effect(l, 499);
            se250 |= has_sound(l, 250);
            for (const auto& c : l.clips) dd |= c.clip == kDDamage;
        }
        r.log.clear();
        check(dd && core02 && fx499 && se250, "DDAMAGE: core exposed (0x583) at F 1265..1270 -> core02 499 + SE 250");
        check(b.phase() == 2 && !b.core_guard() && b.seq_index() >= 26 && b.seq_index() <= 31,
              "no arms, HP > 750: phase 2, core open, index in [26, 32)");
        r.step(b);
        check(b.part(0).hurt_radius == 1.0f && b.part(0).hurt_centre.y > -500.0f && b.part(0).hurt_centre.y < 100.0f,
              "the core's hurt sphere (r 1.0) is no longer buried");
        // Core damage feeds the body HP; 375 = 25 % of 1500 -> the stagger.
        run_until(b, r, 2000, [&] { return b.code() == kCodeRoll; });
        b.add_part_damage(0, 200);
        Boss03Output o = r.step(b);
        check(b.hp() == 1300.0f && b.core_damage() == 200 && near_eq(b.flash(), 1.65f - 0.075f) && o.gauge &&
                  o.gauge->hp == 1300.0f,
              "core hit 200: body HP 1300, accumulator 200, flash 1.65, HP bar");
        b.add_part_damage(0, 175);
        r.step(b);
        const bool teeth_now = b.special() == 10;
        check(b.hp() == 1125.0f && b.core_damage() == 0 && (b.stagger_pending() || teeth_now),
              "375 core damage: accumulator reset, the stagger is pending (special 8 at the boundary)");
        r.keep_log = true;
        run_until(b, r, 3000, [&] { return b.phase() == kPhaseSpecial; });
        check(b.special() == 8 || b.special() == 10, "next attack boundary: special 8 (stagger -> regrow)");
        run_until(b, r, 1000, [&] { return b.phase() != kPhaseSpecial; });
        bool t5 = false, t6 = false, se248 = false;
        for (const auto& l : r.log) {
            for (const auto& c : l.clips) { t5 |= c.clip == kTeeth5 || c.clip == kTeeth4; t6 |= c.clip == kTeeth6; }
            se248 |= has_sound(l, 248);
        }
        r.log.clear();
        r.keep_log = false;
        check(t5 && t6 && se248 && b.arm_alive(0) && b.arm_alive(1) && b.part(1).hp == 800.0f && b.part(3).hp == 800.0f,
              "regrowth: TEETH5 -> both arms back at 800 HP, SE 248 -> TEETH6");
        check(b.phase() == 0 && b.core_guard() && b.seq_index() <= 10, "arms back, HP 1125 > 750: phase 0, core guarded again");
        // TEETH interrupted: break the arms again, wait for TEETH, then 375 core damage.
        break_arm(b, r, 0);
        break_arm(b, r, 1);
        run_until(b, r, 3000, [&] { return b.phase() == 2 && b.code() == kCodeTeeth && b.sub() == 1; });
        check(b.code() == kCodeTeeth, "phase 2 reaches TEETH (the 200-frame core window)");
        b.add_part_damage(0, 375);
        r.step(b);
        check(b.phase() == kPhaseSpecial && b.special() == 10 && b.hp() == 750.0f,
              "375 core damage during TEETH: special 10 at once (driver), HP 750");
        run_until(b, r, 1000, [&] { return b.phase() != kPhaseSpecial; });
        check(b.phase() == 1 && b.seq_index() >= 11 && b.seq_index() <= 25 && b.arm_alive(0) && b.arm_alive(1),
              "regrown at HP 750 (<= 50 %): phase 1, index in [11, 26)");
        break_arm(b, r, 0);
        break_arm(b, r, 1);
        run_until(b, r, 1000, [&] { return b.phase() != kPhaseSpecial; });
        check(b.phase() == 3 && b.seq_index() >= 32 && !b.core_guard(), "arms broken at HP 750 from phase 1: phase 3, index in [32, 41)");
    }

    // ---------------------------------------------------------------- §4.2 the loop limit regrows a broken arm
    {
        Boss03 b(setup(0));
        Rig r;
        r.start(b);
        for (int i = 0; i < 5; ++i) r.step(b);
        b.add_part_damage(1, 400);
        r.step(b);
        run_until(b, r, 600, [&] { return b.intro() >= 120 && b.phase() != kPhaseSpecial; });
        std::set<int> idx;
        const int n = run_until(b, r, 40000, [&] {
            idx.insert(b.seq_index());
            return b.phase() == kPhaseSpecial && b.special() == 8;
        });
        check(n < 40000 && idx.count(0) && idx.count(10), "one arm broken in phase 0: special 8 after 2 passes of [0, 11)");
    }

    // ---------------------------------------------------------------- §6.2 BEAT and the ring wave
    {
        Boss03 b(setup(0));
        Rig r;
        r.start(b);
        const int n = run_until(b, r, 6000, [&] { return b.wave_on(); });
        const int base = [&] { for (int k = 0; k < kRings; ++k) if (b.ring_state(k) == 2) return k; return -1; }();
        check(n < 6000 && (b.code() == kCodeBeat) && base >= 0, "BEAT (idx 5): the impact strikes the ring in front, the wave starts");
        // The boss faces the player at (0, -17.477): ring 0's marker is at bearing -90 degrees.
        check(base == 0 || base == 35 || base == 1, "the struck ring is the one under the fist (sector of the facing)");
        r.keep_log = true;
        for (int i = 0; i < 95; ++i) r.step(b);
        int fx434 = 0;
        for (const auto& l : r.log) for (const auto& e : l.effects) fx434 += e.id == 434;
        r.log.clear();
        r.keep_log = false;
        const int bp = (base + 1) % kRings, bm = (base + kRings - 1) % kRings, far = (base + 18) % kRings;
        check(b.ring_hold(base) == 1000 && b.ring_hold(bp) == 500 && b.ring_hold(bm) == 500 && b.ring_hold(far) == 15,
              "BEAT holds: struck ring 1000, neighbours 500, the rest 15 (Normal near-steps 2)");
        check(fx434 == 35, "the wave erupts 434 on every other ring once (35), 18 steps x 5 frames");
        bool sunk = true;
        for (int k = 0; k < kRings; ++k) sunk &= b.ring_state(k) != 0;
        check(sunk && near_eq(b.ring_y(base), -2.75f), "every ring sinks to -2.75 (state 3 held / 4 rising)");
        run_until(b, r, 1200, [&] { return b.ring_state(base) == 4; });
        for (int i = 0; i < 60; ++i) r.step(b);
        check(b.ring_y(base) > -1.51f && b.ring_y(base) < -1.5f && b.ring_state(base) == 4,
              "the rise ends at -1.5000005: never back to state 0 in float32 (G13)");
    }

    // ---------------------------------------------------------------- §8 death and the flag-222 handshake
    for (int variant = 0; variant < 3; ++variant) {        // 0 story, 1 player dead, 2 boss rush
        Boss03Setup s = setup(0);
        s.boss_rush = variant == 2;
        Boss03 b(s);
        Rig r;
        r.start(b);
        for (int i = 0; i < 5; ++i) r.step(b);
        b.add_part_damage(1, 400);
        b.add_part_damage(3, 400);
        r.step(b);
        run_until(b, r, 1000, [&] { return b.intro() >= 120 && b.phase() == 2; });
        b.add_part_damage(0, 5000);                        // the killing blow
        Boss03Output o = r.step(b);
        check(b.hp() == 0.0f && o.player_iframes, "killing blow: HP clamped to 0 by the tail, player i-frames");
        if (variant == 1) r.in.player_hp = 0;
        int k = 0, f222 = -1, f156 = -1, f134on = -1, f134off = -1, destroy = -1, debris = 0, ts875 = 0;
        bool kill_p = false, hide = false, kill22 = false;
        std::optional<Boss03Exp> exp;
        const int draws0 = r.rng.draws, shake0 = r.shake_draws;
        while (k < 2000 && !b.destroyed() && r.in.fight) {
            o = r.step(b);
            ++k;
            for (const auto& f : o.flags) {
                if (f.first == 222 && f222 < 0) f222 = k;
                if (f.first == 156 && f.second == 0 && f156 < 0) f156 = k;
                if (f.first == 134 && f.second == 1 && f134on < 0) f134on = k;
                if (f.first == 134 && f.second == 0 && f134off < 0) f134off = k;
            }
            for (const auto& e : o.effects) debris += e.id == 506;
            ts875 += o.time_scale && *o.time_scale == 0.875f;
            kill_p |= o.kill_perries;
            hide |= o.gauge_hide;
            if (o.kills.size() >= 22) kill22 = true;
            if (o.exp) exp = o.exp;
            if (o.destroy) destroy = k;
        }
        const int brain_draws = (r.rng.draws - draws0) - (r.shake_draws - shake0);
        std::printf("[boss03] death variant %d: 222 @%d, 156=0 @%d, 134 %d..%d, destroy @%d, debris %d (draws %d)\n",
                    variant, f222, f156, f134on, f134off, destroy, debris, brain_draws);
        if (variant == 0) {
            check(f134on == 1 && ts875 == 1 && kill_p, "death frame 1: DEAD, time scale 0.875, Perries killed, flag 134 = 1");
            check(f222 >= 220 && f222 <= 240 && f134off > 190 && f134off < f222,
                  "flag 222 first written ~230 frames after the kill (3 stutters + 30 final frames)");
            check(destroy >= 740 && destroy <= 765 && hide && kill22 && exp && exp->award && exp->level == 24 &&
                      exp->mult == 1.0f,
                  "destroyed at final frame 550 (~750): bar hidden, 22-id kill list, EXP award");
            check(brain_draws == debris * 7, "death RNG: 7 inline draws per debris burst (plus the camera's 3 per shake frame)");
        } else if (variant == 1) {
            check(f222 < 0 && destroy > 0, "player dead: no flag 222 (the game over wins), the boss still finishes");
        } else {
            check(f222 < 0 && f156 >= 220 && f156 <= 240 && destroy < 0 && !exp,
                  "boss rush: flag 156 = 0 instead of 222, no EXP; the brain stops before the destroy");
        }
    }

    // ---------------------------------------------------------------- the camera-shake RNG trap
    {
        XorShift x;
        std::function<std::uint32_t()> g = [&] { return x(); };
        int frames = 3;
        const Vec3 a = camera_shake(frames, {1, 1, 1}, g);
        camera_shake(frames, {1, 1, 1}, g);
        camera_shake(frames, {1, 1, 1}, g);
        const Vec3 z = camera_shake(frames, {1, 1, 1}, g);
        check(x.draws == 9 && frames == 0 && z.x == 0.0f && std::fabs(a.x) <= 1.0f,
              "camera shake: 3 shared-RNG draws per frame while 0x76a5ac != 0");
    }

    // ---------------------------------------------------------------- smoke: the whole fight
    {
        Boss03 b(setup(0));
        Rig r;
        r.start(b);
        std::set<int> codes, specials, phases;
        std::map<int, int> fx;
        int spawns = 0, frames = 0, pillars_rng = 0, rings_moved = 0;
        while (frames < 120000 && !b.destroyed()) {
            const float a = float(frames) * 0.003f;        // the player orbits at r 13
            r.in.player_pos = {std::cos(a) * 13.0f, -1.5f, std::sin(a) * 13.0f};
            // The first 6000 frames untouched (phase 0 cycles); then hit what is not buried.
            for (int arm = 0; arm < 2 && frames >= 6000; ++arm) {
                const int part = arm == 0 ? 1 : 3;
                if (frames % 40 == arm * 20 && b.part_hittable(part) && b.part(part).hurt_centre.y > -500.0f)
                    b.add_part_damage(part, 25);
            }
            if (frames >= 6000 && frames % 40 == 7 && b.part_hittable(0) && b.part(0).hurt_centre.y > -500.0f)
                b.add_part_damage(0, 12);
            if (b.phase() != kPhaseSpecial && b.intro() >= 120) codes.insert(b.code());
            if (b.phase() == kPhaseSpecial) specials.insert(b.special());
            phases.insert(b.phase());
            const Boss03Output o = r.step(b);
            ++frames;
            for (const auto& e : o.effects) ++fx[e.id];
            spawns += int(o.spawns.size());
            rings_moved += int(o.rings.size());
            (void)pillars_rng;
        }
        std::printf("[boss03] smoke: %d frames, codes", frames);
        for (int c : codes) std::printf(" %x", c);
        std::printf(", specials");
        for (int s : specials) std::printf(" %d", s);
        std::printf(", fx");
        for (const auto& [id, n] : fx) std::printf(" %d:%d", id, n);
        std::printf(", Perries %d, draws %d\n", spawns, r.rng.draws);
        check(b.destroyed(), "smoke: a scripted player wins the fight (the boss is destroyed)");
        check(codes.count(kCodeRoll) && codes.count(kCodePunch) && codes.count(kCodeSwing) && codes.count(kCodeBeat) &&
                  codes.count(kCodeFire) && codes.count(kCodeMaguma),
              "smoke: phase-0 attacks ROLL, PUNCH, SWING, BEAT, FIRE, MAGUMA all run");
        check(codes.count(kCodeTeeth) && codes.count(kCodeNagi) && codes.count(kCodeBeatPattern),
              "smoke: TEETH (phases 2/3), NAGI and BEAT' (phase 1) run");
        check(specials.count(9) && specials.count(8) && phases.count(1) && phases.count(2) && phases.count(3),
              "smoke: arm breaks (9), regrowths (8), phases 0..3");
        check(fx[430] > 0 && (fx[416] + fx[417]) > 0 && fx[413] > 0 && fx[434] > 0 && fx[444] > 0 && fx[415] > 0 &&
                  fx[412] > 0 && fx[436] > 0 && spawns > 0 && rings_moved > 0,
              "smoke: punch 430, swing 416/417, beat 413, wave 434, pillars 444, gero 415, teeth 412, nagi 436, Perries");
    }

    std::printf("[boss03] %s (%d failure%s)\n", fails ? "FAILED" : "ALL OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
