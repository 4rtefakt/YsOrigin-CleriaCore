// Headless tests for src/cleria/boss01.hpp — Velagunder's object logic (build/boss_re/VELAGUNDER.md),
// driven with a deterministic xorshift128, a stand-in clip clock and a scripted player.
#include <cmath>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>

#include "cleria/boss01.hpp"

using namespace cleria;
using namespace cleria::boss01;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[boss] %-66s %s\n", what, ok ? "OK" : "FAIL");
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

// [STAND-IN] for MeshWorld: the clip the boss asked for, advanced 0.5 x speed frames per
// step from `start` to `end + 1`; a loop clip wraps and counts, a stop clip holds and sets 1.
struct Clock {
    int clip = kWait;
    float frame = float(kClipTable[kWait].start);
    std::uint32_t count = 0;
    float speed = 1.0f;
    void apply(const Boss01Output& o) {
        for (const Boss01ClipSet& c : o.clips) {
            clip = c.clip;
            frame = float(kClipTable[c.clip].start);
            count = 0;
        }
        if (o.speed) speed = *o.speed;
    }
    void step() {
        const ClipInfo& ci = kClipTable[clip];
        frame += 0.5f * speed;
        const float endx = float(ci.end + 1);
        if (frame >= endx) {
            if (ci.loop) { frame -= endx - float(ci.start); ++count; }
            else { frame = float(ci.end); if (count == 0) count = 1; }
        }
    }
};

// A scripted world: the player standing still, bones on a fixed rig around the boss, the
// actor list (player + spawns) maintained from the boss's requests.
struct Rig {
    XorShift rng;
    Clock clk;
    Boss01Input in;
    std::vector<Boss01Output> log;
    bool keep_log = false;
    Rig() {
        in.rng = [this] { return rng(); };
        in.player_pos = {0, 2, 20.22f};
        in.player_hp = 100;
        in.actors.push_back({0x0d, in.player_pos, 100});
    }
    void pose(const Boss01& b) {
        const Vec3 p = b.pos();
        for (int i = 0; i < kBones; ++i)
            in.bones[std::size_t(i)] = {p.x + float(i % 5 - 2) * 0.8f, p.y + 1.0f + float(i) * 0.1f,
                                        p.z - 2.0f};
        in.bone114 = {p.x, p.y + 3.0f, p.z - 2.5f};
        in.bone115 = {p.x, p.y + 3.0f, p.z - 2.0f};
    }
    Boss01Output step(Boss01& b) {
        clk.step();                                   // FUN_005f6000 runs before the brain
        pose(b);
        in.clip = clk.clip;
        in.clip_count = clk.count;
        in.clip_frame = clk.frame;
        in.actors[0].pos = in.player_pos;
        in.actors[0].hp = in.player_hp;
        int spawns = 0;
        for (const Boss01Actor& a : in.actors) spawns += (a.cast == 0xaf && a.hp >= 1.0f);
        in.spawn_count = spawns;
        Boss01Output o = b.tick(in);
        clk.apply(o);
        b.camera().step_standin();
        // host: actor edits, spawns, the death sparks (FUN_00504c10 stand-in: no nodes)
        for (const Boss01ActorEdit& e : o.actor_edits) {
            Boss01Actor& a = in.actors[std::size_t(e.index)];
            if (e.set_pos) a.pos = e.pos;
            if (e.eaten) a.hp = 0;
            if (a.cast == 0x0d) in.player_pos = a.pos;
        }
        for (const Boss01Spawn& s : o.spawns) in.actors.push_back({s.cast, s.pos, 58});
        if (o.kill_minions)
            for (Boss01Actor& a : in.actors) if (a.cast == 0xaf || a.cast == 0x19) a.hp = 0;
        in.player_took_damage = false;
        if (keep_log) log.push_back(o);
        return o;
    }
};

static Boss01Setup setup(int diff, bool easy, bool vez, int character = 1) {
    Boss01Setup s;
    s.character = character;
    s.diff_index = diff;
    s.diff_easy = easy;
    s.diff_very_easy = vez;
    s.player_pos = {0, 2, 20.22f};
    return s;
}

static bool has_sound(const Boss01Output& o, int id, Boss01Sound::Kind k = Boss01Sound::k3D) {
    for (const Boss01Sound& s : o.sounds) if (s.id == id && s.kind == k) return true;
    return false;
}

int main() {
    // ---------------------------------------------------------------- §2.2 stats
    {
        struct Row { const char* name; int diff; bool easy, vez; float hp, str, def; };
        const Row rows[] = {
            {"Normal", 0, false, false, 700, 70, 53},
            {"Hard", 1, false, false, 875, 63, 54},
            {"Nightmare", 2, false, false, 1050, 65, 54},
            {"Easy (0 retries)", 0, true, false, 700, 71, 52},
            {"Very Easy (0 retries)", 0, true, true, 525, 70, 51},
        };
        for (const Row& r : rows) {
            Boss01 b(setup(r.diff, r.easy, r.vez));
            char what[128];
            std::snprintf(what, sizeof what, "ctor %-22s maxHP %.0f STR %.0f DEF %.0f", r.name, r.hp, r.str, r.def);
            check(near_eq(b.max_hp(), r.hp) && near_eq(b.hp(), r.hp) && near_eq(b.str(), r.str) &&
                      near_eq(b.def(), r.def),
                  what);
            if (r.diff == 0 && !r.easy)
                check(near_eq(b.stun_gauge(), 560.0f) && b.level() == 8 && near_eq(b.rec4(), 140.0f),
                      "ctor: stun gauge = maxHP x 0.8, level 8, +0xc4 = 140");
        }
        Boss01Setup s = setup(0, true, false);
        s.retries = 4;
        { Boss01 b(s); check(near_eq(b.str(), 69) && near_eq(b.def(), 50), "Easy, 4 retries: STR/DEF -= n>>1 (69/50)"); }
        s.retries = 25;
        { Boss01 b(s); check(near_eq(b.str(), 56) && near_eq(b.def(), 37), "Easy, 25 retries: STR/DEF -= n-10 (56/37)"); }
        s.retries = 200;
        { Boss01 b(s); check(b.str() == 1.0f && b.def() == 1.0f, "Easy, 200 retries: clamped to 1.0"); }
        s.retries = 4; s.time_attack = true;
        { Boss01 b(s); check(near_eq(b.str(), 71) && near_eq(b.def(), 52), "Easy, time-attack: retries ignored"); }
        Boss01Setup v = setup(0, true, true);
        v.retries = 16;
        { Boss01 b(v); check(near_eq(b.str(), 70 - 32) && near_eq(b.def(), 51 - 32), "Very Easy, 16 retries: -= 16^1.25 = 32"); }
    }

    // ---------------------------------------------------------------- §3.1 parts
    {
        Boss01 b(setup(0, false, false));
        bool bones_ok = true;
        const int T[21] = {10, 14, 15, 16, 17, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 18, 19, 20, 21, 22, 23};
        for (int i = 0; i < kParts; ++i) bones_ok &= b.part(i).bone == T[i];
        check(bones_ok, "part -> bone table DAT_0069cc58");
        check(std::string_view(kBoneNames[b.part(0).bone]) == "Null(237)" &&
                  std::string_view(kBoneNames[b.part(1).bone]) == "Null(241)" &&
                  std::string_view(kBoneNames[b.part(2).bone]) == "Null(242)",
              "core / stun part / mouth sit on Null(237/241/242)");
        bool def_ok = true;
        for (int i = 2; i < kParts; ++i) def_ok &= b.part(i).def == 999.0f && b.part(i).armor;
        check(def_ok, "parts 2..20: DEF 999, armour");
        check(near_eq(b.part(1).resist[3], 0.35f) && near_eq(b.part(1).resist[0], 0.5f) &&
                  near_eq(b.part(1).hp, 7000.0f),
              "ctor part resist (Yunica) 0.5 / 0.35, part HP = 10 x HP");
        Rig r;
        r.step(b);   // fight flag off: idle, but the parts are placed
        check(r.rng.draws == 0 && b.state() == 0, "fight flag off: idle state 0, no RNG draw");
        const Boss01Part& p1 = b.part(1);
        check(near_eq(p1.hurt_radius, 2.1f) && near_eq(p1.hurt_centre.y, p1.pos.y - 0.5f),
              "part 1 hurt sphere r 2.1, centre y -0.5");
        check(b.part(0).def == 999.0f && b.part(0).armor && near_eq(p1.def, b.def()) && !p1.armor,
              "outside the stun: core DEF 999, stun part DEF = boss DEF");
        check(near_eq(b.part(0).resist[3], 0.7f), "core melee resist slot = 0.7 (Yunica) per frame");
        bool rad = true;
        for (int i = 15; i < kParts; ++i) rad &= b.part(i).hurt_radius == 0.0f && !b.part_hittable(i);
        check(rad, "parts 15..20: hurt radius 0, not hittable");
        bool unarmed = true;
        for (int i = 0; i < kParts; ++i) unarmed &= !b.part(i).armed;
        check(unarmed, "WAIT clip: no part armed");
    }

    // ---------------------------------------------------------------- fight start + first decision
    {
        Boss01 b(setup(0, false, false));
        Rig r;
        for (int i = 0; i < 5; ++i) r.step(b);
        r.in.fight = true;
        Boss01Output o = r.step(b);
        check(b.state() == 10 && o.camera_written && b.camera().cur == 1 && b.camera().prev == 0xc &&
                  near_eq(b.camera().rate, 0.02f) && b.f7_pending(),
              "fight flag 156 -> state 10, camera 0xc -> 1 at 0.02, ATTACK_06 pending");
        int n = 0;
        while (b.state() == 10 && n < 200) { r.step(b); ++n; }
        check(b.state() == 0 && n == 61, "intro: bc > 60 and the camera settled -> state 0 (61 ticks)");
        r.step(b);   // sub 0: WAIT, cooldown 0 (F7)
        check(b.sub() == 1 && b.aux() == 0 && r.clk.clip == kWait, "decision sub 0: WAIT clip, cooldown 0 while F7");
        XorShift ref = r.rng;
        const int before = r.rng.draws;
        o = r.step(b);
        ref(); ref();
        check(b.state() == 7 && r.rng.draws - before == 2 && ref.w == r.rng.w,
              "first decision commits 7 (bubble spray) after exactly R1, R2");
        check(b.camera().cur == 3 && near_eq(b.camera().rate, 0.025f) && !b.f7_pending(),
              "commit 7: camera region 3 at 0.025, F7 cleared");
        // The spray: 29 bubble bullets (kind 0x1e), the drool while bc 1..34, then wait 264.
        int bubbles = 0, drool = 0, puffs = 0, ticks = 0;
        bool se196 = false;
        while (b.state() == 7 && ticks < 2000) {
            o = r.step(b);
            ++ticks;
            for (const Boss01Bullet& bl : o.bullets) { bubbles += bl.kind == 0x1e; drool += bl.kind == 0x1f; }
            for (const Boss01Effect& e : o.effects) puffs += e.id == 0x145;
            se196 |= has_sound(o, 196);
        }
        check(bubbles == 29, "state 7 emits 29 bubble bullets (17 E1 + 12 E2)");
        check(puffs == 18 && drool == 33 && se196, "state 7: 18 puffs (EF2 325), drool on bc 1..34, SE 196");
        check(b.state() == 0, "state 7 ends on clip completion -> state 0");
        r.step(b);
        check(b.aux() == 264, "after ATTACK_06 the wait is 264 frames (Yunica)");
    }

    // ---------------------------------------------------------------- stun loop, phases (§3.3, §5.3)
    {
        Boss01 b(setup(0, false, false));
        Rig r;
        const float M = b.max_hp();
        r.step(b);
        b.add_part_damage(1, 100);
        Boss01Output o = r.step(b);
        check(b.phase() == 1 && near_eq(b.stun_gauge(), 460) && b.stun_hits_a() == 1 && b.stun_hits_b() == 1,
              "first stun-part hit: phase 0 -> 1, gauge 560 -> 460, hit counters");
        check(near_eq(b.flash(), 1.65f - 0.075f), "hit flash 1.65, decays 0.075 per frame");
        for (int i = 0; i < 4; ++i) { b.add_part_damage(1, 100); r.step(b); }
        check(b.phase() == 1 && !b.stunned(), "gauge 60 left: not stunned yet");
        b.add_part_damage(1, 100);
        o = r.step(b);   // FUN_005084a0 runs before the stun test: entry on the same frame
        check(b.phase() == 2 && b.stunned() && b.stun_gauge() == 0.0f,
              "gauge broken: phase 2, stun flag set");
        check(b.state() == 3 && b.sub() == 0 && b.camera().cur == 5 && o.sounds.size() == 7,
              "stun entry on the same frame: state 3, camera 5, 7 attack SEs stopped");
        o = r.step(b);
        check(r.clk.clip == kPiyo1 && has_sound(o, 200) && has_sound(o, 201), "stun sub 0: PIYOPIYO_1, SE 200/201");
        int n = 0;
        while (b.sub() != 2 && n < 500) { o = r.step(b); ++n; }
        check(r.clk.clip == kPiyo2 && o.minion_730 == 100 && has_sound(o, 184) && !o.effects.empty() &&
                  o.effects[0].id == 0x3c,
              "PIYOPIYO_1 done -> PIYOPIYO_2 loop, stars, M890 stunned (+0x730 = 100)");
        check(near_eq(b.part(0).def, b.def()) && !b.part(0).armor && b.part(1).def == 999.0f,
              "dizzy loop: core exposed (DEF = boss DEF), stun part armoured");
        b.add_part_damage(0, 0.24f * M);
        r.step(b);
        check(near_eq(b.hp(), 0.76f * M) && b.phase() == 2 && b.sub() == 2 && near_eq(b.stun_damage(), 0.24f * M),
              "core hit 24%: HP 76%, phase 2, still dizzy");
        b.add_part_damage(0, 0.02f * M);
        o = r.step(b);
        check(b.phase() == 3 && b.sub() == 3 && !b.stunned() && o.minion_730 == 1 && r.clk.clip == kPiyo3,
              "HP < 75%: phase 3; core damage > 25% M ends the stun (PIYOPIYO_3)");
        check(b.part(0).def == 999.0f, "core armoured again once the dizzy loop ends");
        n = 0;
        while (b.state() == 3 && n < 500) { r.step(b); ++n; }
        check(b.state() == 0 && b.fr_pending() && near_eq(b.stun_gauge(), 0.5f * M) && b.stun_damage() == 0.0f,
              "stun over: state 0, reposition pending, gauge refilled to 0.5 M (D > 25%)");
        b.add_part_damage(0, 0.25f * M);
        r.step(b);
        check(b.phase() == 4, "HP < 50%: phase 4");
        b.add_part_damage(0, 0.25f * M);
        r.step(b);
        check(b.phase() == 5 && near_eq(b.hp_bar_fraction(), 0.24f), "HP < 25%: phase 5; bar fraction = HP/max");
    }

    // ---------------------------------------------------------------- death timeline (§8)
    for (int variant = 0; variant < 3; ++variant) {   // 0 story, 1 time-attack, 2 player dead
        Boss01Setup s = setup(0, false, false);
        s.time_attack = variant == 1;
        Boss01 b(s);
        Rig r;
        r.in.fight = true;
        r.step(b);
        b.add_part_damage(0, 5000);
        Boss01Output o = r.step(b);
        bool parts_dead = true;
        for (int i = 0; i < kParts; ++i) parts_dead &= !b.part(i).alive;
        if (variant == 0) {
            check(b.hp() == 0.0f && b.state() == 9 && has_sound(o, 206) && has_sound(o, 577) && o.tilt &&
                      o.tilt->frames == 300 && o.kill_minions && parts_dead,
                  "kill frame: HP 0, state 9, SE 206 + 577, tilt 300, minions killed, parts dead");
            check(o.gauge_hide, "HP bar hidden once HP < 1");
        }
        if (variant == 2) r.in.player_hp = 0;
        int k = 0, sparks = 0;
        bool se576 = false, se188 = false, alpha = false;
        int flag_tick = -1;
        std::vector<std::pair<int, int>> flags;
        while (!b.destroyed() && k < 2000) {
            const int draws0 = r.rng.draws;
            o = r.step(b);
            ++k;
            if (o.death_sparks) { ++sparks; if (r.rng.draws - draws0 != 1) sparks = -100; }
            se576 |= has_sound(o, 576) && k == 80;
            se188 |= has_sound(o, 188) && k == 440;
            alpha |= o.alpha_target.has_value() && k == 142;
            if (!o.flags.empty() || o.destroy) { flag_tick = k; flags = o.flags; }
        }
        if (variant == 0) {
            check(flag_tick == 580 && flags.size() == 1 && flags[0].first == 220 && flags[0].second == 1,
                  "story: flag 220 = 1 on the 580th frame after the kill frame");
            check(se576 && se188 && alpha && sparks == 6,
                  "death timeline: SE 576 @80, alpha fade @142, SE 188 @440, 6 spark rolls");
        } else if (variant == 1) {
            check(flag_tick == 580 && flags.size() == 1 && flags[0].first == 156 && flags[0].second == 0,
                  "time-attack: flag 156 = 0 instead of 220");
        } else {
            check(flag_tick == 580 && flags.empty(), "player dead at k = 580: no flag 220");
        }
        check(b.destroyed() && b.tick(r.in).sounds.empty(), "destroyed: further ticks are no-ops");
    }

    // ---------------------------------------------------------------- whole-fight smoke run
    {
        Boss01 b(setup(0, false, false));
        Rig r;
        r.in.fight = true;
        std::set<int> seen;
        int ticks = 0;
        bool flag220 = false;
        const float players[4][2] = {{0, 20.22f}, {6, 26}, {-8, 24}, {0, 29}};
        while (!b.destroyed() && ticks < 60000) {
            r.in.player_pos = {players[(ticks / 900) % 4][0], 2, players[(ticks / 900) % 4][1]};
            // A player who hits the stun part every 20 frames and the core when it is exposed.
            if (ticks % 90 == 0 && b.part(1).def < 999.0f) b.add_part_damage(1, 25);
            if (ticks % 30 == 0 && b.part(0).def < 999.0f) b.add_part_damage(0, 12);
            if (ticks % 97 == 0) r.in.player_took_damage = true;
            const int prev = b.state();
            const Boss01Output o = r.step(b);
            if (b.state() != prev && b.state() != 0) std::printf("%d@%d(p%d) ", b.state(), ticks, b.phase());
            seen.insert(b.state());
            for (const auto& f : o.flags) flag220 |= f.first == 220 && f.second == 1;
            ++ticks;
        }
        std::printf("[boss] smoke: %d ticks, states seen:", ticks);
        for (int s : seen) std::printf(" %d", s);
        std::printf(", spawns alive at end %d\n", r.in.spawn_count);
        check(flag220 && b.destroyed(), "smoke: the scripted fight ends in the death handshake (flag 220)");
        check(seen.count(1) && seen.count(3) && seen.count(5) && seen.count(7) && seen.count(9),
              "smoke: run, stun, laser, bubble spray and death all reached");
    }

    std::printf("[boss] %s (%d failure%s)\n", fails ? "FAILED" : "ALL OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
