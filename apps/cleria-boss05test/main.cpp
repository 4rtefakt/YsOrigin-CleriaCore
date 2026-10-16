// Headless tests for src/cleria/boss05 (Pictimos) against build/boss_re/PICTIMOS.md.
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/boss05.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss05] %-66s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
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

struct Rig {
    XorShift rng;
    Boss05Input in;
    std::vector<Boss05Minion> mins;
    int next_id = 200, spawns = 0;
    Rig() {
        in.rng = [this] { return rng(); };
        in.fight = true;
        in.player_pos = {0, 1.075f, -10};
        in.section = boss05::kStand;
        for (auto& s : in.slots) s = {0, 5, 17};
        in.nodes.assign(65, Vec3{0, 3, 17});
        in.spawn = [this](const Vec3& p, int, int, float* def) {
            Boss05Minion m; m.id = ++next_id; m.pool = int(mins.size()) + 1; m.hp = m.max_hp = 100; m.def = 50; m.pos = p;
            mins.push_back(m); ++spawns;
            if (def) *def = 50;
            return m.id;
        };
    }
    // The host's clip model: a SetClip restarts the count; a stop clip completes after its length.
    // The anim advances 0.25 x speed frames a tick (A+0x24) after the brain [H, PICTIMOS U1].
    float frame = 0, speed = 1;
    Boss05Output step(Boss05& b) {
        const boss05::SectionInfo& si = boss05::kSectionTable[in.section];
        in.count = (!si.loop && frame >= float(si.end - si.start)) ? 1u : 0u;
        Boss05Output o = b.tick(in, mins);
        if (!o.sections.empty()) { in.section = o.sections.back(); frame = 0; }
        if (o.anim_speed) speed = *o.anim_speed;
        frame += 0.25f * speed;
        return o;
    }
};

int main() {
    {
        Boss05Setup s; Boss05 n(s);
        s.diff_index = 1; Boss05 h(s);
        s.diff_index = 2; Boss05 nm(s);
        s.diff_index = 0; s.diff_easy = s.diff_very_easy = true; Boss05 ve(s);
        check(n.max_hp() == 4000 && h.max_hp() == 5000 && nm.max_hp() == 6000 && ve.max_hp() == 3000,
              "body HP 4000 / 5000 / 6000, Very Easy 3000");
        check(n.pos().z == 17.0f && n.part(0).def == 230 && n.part(1).def == 230 && n.part(2).def == 999 &&
              n.part(12).def == 999 && n.part(0).resist[0] == -0.3f && n.part(0).radius == 1.6f,
              "z 17; parts 0 / 1 DEF 230, parts 2..12 DEF 999; Yunica's -0.3 resist");
    }
    {   // The intro hold: 700 frames under the cutscene's Fix, 101 without.
        Rig r; Boss05 b(Boss05Setup{});
        r.in.fix_held = true;
        for (int t = 0; t < 700; ++t) r.step(b);
        const bool held = b.intro() == 700;
        Rig r2; Boss05 b2(Boss05Setup{});
        r2.step(b2);
        check(held && b2.intro() == 600, "intro: 700 frames with Fix held, else bc jumps to 600 (101 frames)");
    }
    {   // The first picks: near and facing -> a swing (dc 650 blocks UTIAGE), with its shock-wave bullet.
        Rig r; Boss05 b(Boss05Setup{});
        r.in.player_pos = {0, 1.075f, 8};                         // 9 u in front (yaw 0 faces -z)
        int t = 0, swing = 0, shots = 0;
        for (; t < 600; ++t) {
            const Boss05Output o = r.step(b);
            for (int sct : o.sections) swing += sct == boss05::kYokonagi || sct == boss05::kTatenagi;
            for (const Boss05Bullet& bl : o.bullets) shots += bl.effect == 380 || bl.effect == 381;
        }
        check(swing >= 1 && shots >= 1, "near & facing: YOKONAGI / TATENAGI with 380 / 381 shock waves");
    }
    {   // The jump arc: vy 2.0, -0.05 a frame, y += vy/2 from b4 38: apex ~20.5 at b4 78, landing at b4 118.
        Rig r; Boss05 b(Boss05Setup{});
        r.in.player_pos = {0, 1.075f, 40};                        // far, behind: turns, then the picks
        for (int t = 0; t < 110; ++t) r.step(b);
        Boss05Output o; float top = 0; int land = -1;
        // Force a JUMP through the far rule: drive until JUMP is the state.
        for (int t = 0; t < 3000 && b.state() != boss05::kJump; ++t) r.step(b);
        for (int t = 0; t < 200; ++t) {
            o = r.step(b);
            top = std::max(top, b.pos().y);
            for (int s : o.sounds) if (s == 191 && land < 0 && b.pos().y == 0.0f && b.frames() == 300) land = t;
        }
        check(b.state() == boss05::kJump || top > 20.0f, "a JUMP was picked");
        check(top > 20.0f && top < 21.0f && land >= 110 && land <= 120, "the arc: apex ~20.5, landing ~b4 118 (b4 -> 300)");
    }
    {   // Below half HP a completed clip starts ZOROMEKA; round 0 spawns one Chryolos every 6 frames: 27.
        Rig r; Boss05 b(Boss05Setup{});
        r.in.player_pos = {0, 1.075f, 30};
        r.step(b);
        b.add_part_damage(0, 2100);
        for (int t = 0; t < 600 && b.state() != boss05::kZoromeka; ++t) r.step(b);
        const bool z = b.state() == boss05::kZoromeka;
        for (int t = 0; t < 300 && b.state() == boss05::kZoromeka; ++t) r.step(b);
        std::printf("[boss05]   ZOROMEKA spawns %d, HP %.0f\n", r.spawns, b.hp());
        check(z && r.spawns == 27, "ZOROMEKA at <= 50 %: 27 summons (b4 72..228 every 6)");
        // The guts: HP 0 with live summons -> HP 1, DEAD, the absorb heals 40 each.
        b.add_part_damage(0, 5000);
        const int before = int(r.mins.size());
        for (int t = 0; t < 700; ++t) r.step(b);
        int dead = 0;
        for (const Boss05Minion& m : r.mins) dead += m.hp < 1.0f && (m.or130 & 0x2000u);
        std::printf("[boss05]   guts: %d of %d absorbed, HP %.0f, guts %d\n", dead, before, b.hp(), b.guts());
        check(b.guts() == 1 && dead > 0 && b.hp() >= 1.0f + 40.0f * float(dead) - 0.5f && !b.destroyed(),
              "guts: survives at 1 HP, absorbs its summons for +40 HP each");
    }
    {   // The death with no summons: flag 224 at b4 500, then destroyed.
        Rig r; Boss05 b(Boss05Setup{});
        r.step(b);
        b.add_part_damage(0, 9000);
        bool f224 = false, exp = false; int t = 0;
        for (; t < 600 && !b.destroyed(); ++t) {
            const Boss05Output o = r.step(b);
            for (auto& f : o.flags) f224 |= f.first == 224 && f.second == 1;
            exp |= o.exp;
        }
        check(f224 && exp && b.destroyed() && t >= 500 && t <= 503, "death: EXP at b4 2, flag 224 and the destroy at b4 500");
    }
    {   // The death in a boss rush [EXPORT go_mesh_boss05.c Pictimos_Brain; PICTIMOS.md C13]: flag 156 = 0 when the
        // alpha (target 0 at 1/4 a frame from b4 350) reaches <= 0.2 at b4 354; the gate then freezes the brain, so
        // b4 500 (flag 224, the destroy) is never reached.
        Boss05Setup s; s.boss_rush = true;
        Rig r; Boss05 b(s);
        r.step(b);
        b.add_part_damage(0, 9000);
        bool f224 = false; int t = 0, f156 = -1;
        for (; t < 600 && f156 < 0; ++t) {
            const Boss05Output o = r.step(b);
            for (auto& f : o.flags) { f224 |= f.first == 224; if (f.first == 156 && f.second == 0) f156 = b.frames(); }
        }
        r.in.fight = false;                                        // the host applied 156 = 0
        for (int k = 0; k < 300; ++k) for (auto& f : r.step(b).flags) f224 |= f.first == 224;
        check(f156 == 354 && !f224 && !b.destroyed() && b.frames() == 354,
              "boss rush: flag 156 = 0 at b4 354; frozen after it: no flag 224, never destroyed");
    }
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
