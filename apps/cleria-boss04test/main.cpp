// Headless tests for src/cleria/boss04 (Khonsclard) against build/boss_re/KHONSCLARD.md.
#include <cmath>
#include <cstdio>

#include "cleria/boss04.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss04] %-66s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

struct XorShift {
    std::uint32_t x = 123456789, y = 362436069, z = 521288629, w = 88675123;
    int n = 0;
    std::uint32_t operator()() {
        ++n;
        const std::uint32_t t = x ^ (x << 11);
        x = y; y = z; z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        return w;
    }
};

struct Rig {
    XorShift rng;
    Boss04Input in;
    Rig() {
        in.rng = [this] { return rng(); };
        in.fight = true;
        in.clip = boss04::kStand;
        in.player_pos = {0, 1.075f, 30};
        for (int k = 0; k < boss04::kNodes; ++k) in.nodes[std::size_t(k)] = {0, 1, 41};
        for (int i = 0; i < 8; ++i) in.nodes[std::size_t(boss04::kNodeU1 + i)] = {float(i) - 3.5f, 2, 40};
        for (int n = 0; n < boss04::kPods; ++n) in.pods[std::size_t(n)] = {0, 0.3f - 999.0f, 43};
    }
    // Apply the pods' moves the way the host does (teleport, then the move-to lands at once [test]).
    void apply(const Boss04Output& o) {
        for (const Boss04PodMove& m : o.pod_moves) {
            if (m.teleport) in.pods[std::size_t(m.pod)] = *m.teleport;
            if (m.move_y) in.pods[std::size_t(m.pod)].y = *m.move_y;
        }
        if (!o.clips.empty()) { in.clip = o.clips.back(); in.clip_done = false; }
    }
};

int main() {
    {   // FUN_00443b80's scaling: 3000 / 3750 / 4500, Very Easy x0.75; STR +1 outside Easy.
        Boss04Setup s;
        Boss04 n(s);
        s.diff_index = 1; Boss04 h(s);
        s.diff_index = 2; Boss04 nm(s);
        s.diff_index = 0; s.diff_easy = true; s.diff_very_easy = true; Boss04 ve(s);
        check(n.max_hp() == 3000 && h.max_hp() == 3750 && nm.max_hp() == 4500 && ve.max_hp() == 2250,
              "body HP 3000 / 3750 / 4500, Very Easy 2250 (no ctor override)");
        check(n.str() == 177 && ve.str() == 175, "STR 176 + 1 outside Easy; Very Easy -1");
        check(n.part(1).max_hp == 600 && h.part(3).max_hp == 900 && nm.part(6).max_hp == 1100 &&
              n.part(1).def == 999 && n.part(0).def == 999 && n.part(7).max_hp == 600 && n.part(7).shape == 1 &&
              n.part(7).hurt_radius == 4.0f && n.part(7).half_h == 3.0f && n.part(0).resist[0] == 0.5f,
              "parts: pods 600/900/1100 DEF 999, core DEF 999, part 7 a cylinder r 4 h 3");
    }
    {   // The first fight frame: attack 0 (the memset's 0x160 = 0) fires 8 homing seeds on the fan.
        Rig r;
        Boss04 b(Boss04Setup{});
        const Boss04Output o = b.tick(r.in);
        int seeds = 0;
        for (const Boss04Bullet& bl : o.bullets) seeds += bl.kind == 4 && bl.effect == 366;
        const Boss04Bullet* first = nullptr;
        for (const Boss04Bullet& bl : o.bullets) if (bl.effect == 366) { first = &bl; break; }
        const float a = -20.0f * 3.14159265f / 180.0f;
        check(seeds == 8 && first && std::fabs(first->dir.x - std::cos(a)) < 1e-4f &&
                  std::fabs(first->dir.z - std::sin(a)) < 1e-4f && first->f[0] == 0.0425f && first->f[10] == 20.0f,
              "attack 0: 8 x EF2 366 kind 4, bullet 0 at -20 deg, speed 0.0425, homing ages 20..66");
        bool se = false;
        for (const Boss04Sound& s : o.sounds) se |= s.id == 254;
        check(se && b.shots() == 1, "attack 0: SE 254 every volley, one volley counted");
        int p = 0;
        for (const Boss04PodMove& m : o.pod_moves) p += m.teleport.has_value();
        check(b.band() == 1 && b.pod(0).enabled && b.pod(1).enabled && !b.pod(2).enabled,
              "band 0: pods 0 and 1 enabled (1000-frame respawn limit)");
        r.apply(o);
        const Boss04Output o2 = b.tick(r.in);
        p = 0;
        for (const Boss04PodMove& m : o2.pod_moves) p += m.teleport.has_value() && m.teleport->y == -3.0f;
        check(p == 2 && b.pod(0).state == 0 && b.pod(0).slot != b.pod(1).slot,
              "the next frame: both rise from two different holes (FUN_00528fb0)");
    }
    {   // The down: break both pods -> FALL + SE 265, the core takes the EEX DEF; damage reaches the body.
        Rig r;
        Boss04 b(Boss04Setup{});
        for (int t = 0; t < 3; ++t) r.apply(b.tick(r.in));
        b.add_part_damage(1, 700); b.add_part_damage(2, 700);
        Boss04Output o = b.tick(r.in);                             // the break and the down in one frame
        r.apply(o);
        bool fall = false, se265 = false;
        for (int c : o.clips) fall |= c == boss04::kFall;
        for (const Boss04Sound& s : o.sounds) se265 |= s.id == 265;
        check(b.pod(0).state == 1 && b.pod(1).state == 1 && b.down() == 1 && fall && se265 &&
                  b.part(0).def == 175.0f,
              "both pods broken: down (FALL, SE 265), core DEF = the EEX 175");
        b.add_part_damage(0, 200);
        b.tick(r.in);
        check(b.hp() == 2800, "the core's damage goes to the body HP (3000 -> 2800)");
        b.add_part_damage(0, 500);                                 // 2300 < 80 %: band 1 -> pod 2, the get-up
        b.tick(r.in);
        b.tick(r.in);                                              // the next frame re-armours the core
        check(b.band() == 2 && b.pod(2).enabled && b.down() == 0 && b.part(0).def == 999.0f,
              "crossing 80 % adds pod 2 and ends the down at once");
    }
    {   // The selector: the player within 10 u -> attack 5 (WAVE) once the rest is over.
        Rig r;
        r.in.player_pos = {0, 1, 36};
        Boss04 b(Boss04Setup{});
        int t = 0;
        for (; t < 600 && b.attack() != 5; ++t) r.apply(b.tick(r.in));
        check(b.attack() == 5 && b.prev_attack() == 0, "near player: attack 5 follows attack 0's rest");
        int pillars = 0, rain = 0;
        for (int k = 0; k < 200 && b.attack() == 5; ++k) {
            const Boss04Output o = b.tick(r.in);
            r.apply(o);
            for (const Boss04Bullet& bl : o.bullets) { pillars += bl.effect == 368; rain += bl.effect == 362; }
        }
        check(pillars == 8 && rain == 12 && b.prev_attack() == 5, "WAVE: 8 pillars (368) at t60, 12 rain shots (362) at t70");
    }
    {   // The pit: s ramps from 0 by 0.0001 a frame; the player within 30 u is pulled at 5s.
        Rig r;
        Boss04 b(Boss04Setup{});
        Boss04Output o;
        for (int t = 0; t < 30; ++t) o = b.tick(r.in);
        check(std::fabs(b.suction() - 0.003f) < 1e-5f && o.player_push && std::fabs(o.player_push->z - 0.015f) < 1e-5f,
              "pit: s +0.0001 a frame (30 frames: 0.003), push 5s toward the boss");
    }
    {   // Death: 223 on the kill frame, the 1/340 fade, 4 draws per model node per frame, destroyed ~340.
        Rig r;
        Boss04Setup s; s.exp_b0 = 100;
        Boss04 b(s);
        b.tick(r.in);
        b.add_part_damage(0, 5000);
        const int n0 = r.rng.n;
        Boss04Output o = b.tick(r.in);                             // the damage lands: the kill frame
        bool f223 = false;
        int timers = 0;
        for (auto& f : o.flags) { f223 |= f.first == 223 && f.second == 1; timers += f.first >= 529 && f.first <= 532 && f.second == 0; }
        check(f223 && o.exp && o.exp->level == 32, "kill frame: flag 223 = 1, EXP awarded (lv 32)");
        check(timers == 4, "kill frame, the player alive: the status timers 529..532 (poison, heavy, confuse, curse) = 0");
        check(r.rng.n - n0 >= 4 * 103, "debris: at least 4 draws per node (103) per death frame");
        int k = 1;
        for (; k < 400 && !b.destroyed(); ++k) b.tick(r.in);
        check(b.destroyed() && k >= 339 && k <= 342, "destroyed when the alpha reaches 0 (about frame 340)");
    }
    {   // Death in a boss rush [BYTES 0x52c8f0..; EXPORT go_mesh_boss04.c Khonsclard_Death]: no 223, no EXP, i-frames,
        // 194 = 0 every frame, 156 = 0 from alpha <= 0.2; then the brain's flag-156 gate freezes it, never destroyed.
        Rig r;
        Boss04Setup s; s.exp_b0 = 100; s.boss_rush = true;
        Boss04 b(s);
        b.tick(r.in);
        b.add_part_damage(0, 5000);
        Boss04Output o = b.tick(r.in);                             // the kill frame
        bool f223 = false, f194 = false;
        for (auto& f : o.flags) { f223 |= f.first == 223; f194 |= f.first == 194 && f.second == 0; }
        check(!f223 && !o.exp && o.player_iframes && f194, "boss rush kill frame: no flag 223, no EXP, i-frames 9999, flag 194 = 0");
        int k = 1, f156 = -1, story = 0, every194 = 1;
        for (; k < 400 && f156 < 0; ++k) {
            o = b.tick(r.in);
            bool z194 = false;
            for (auto& f : o.flags) { z194 |= f.first == 194 && f.second == 0; if (f.first == 156 && f.second == 0) f156 = k; }
            every194 &= z194;
            story += o.clear_buffs || o.achievement;
        }
        check(every194 && story == 0 && f156 >= 270 && f156 <= 274 && !b.destroyed(),
              "boss rush: flag 156 = 0 once alpha <= 0.2 (about death frame 272), no buff clear / achievement");
        r.in.fight = false;                                        // the host applied 156 = 0
        bool quiet = true;
        for (int t = 0; t < 200; ++t) { o = b.tick(r.in); quiet &= o.flags.empty() && !o.destroy; }
        check(quiet && !b.destroyed(), "boss rush: the gate then stops the death; the boss is left at alpha 0.2, never destroyed");
    }
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
