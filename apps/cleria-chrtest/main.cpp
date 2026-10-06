// cleria-chrtest — the cutscene actor ("Chr_") ops: the facing model (FUN_00443a80, the look
// modes, FUN_005eee60's turn), Chr_Pattern, Chr_RunOrWalk, Chr_Tremble and Chr_FlyTo, against
// build/event_re/CHR_OPS.md. Pure: no assets (the argument is accepted and ignored).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "cleria/chr_state.hpp"
#include "cleria/world.hpp"
#include "cleria/xso.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[chr] %-76s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static constexpr float kPi = 3.14159265f;
// An engine heading compared modulo 2 pi (FUN_005eee60 may store a mid-turn heading in (pi, 3pi]).
static bool same_angle(float a, float b, float e = 1e-3f) {
    float d = std::fmod(a - b, 2.0f * kPi);
    if (d > kPi) d -= 2.0f * kPi;
    if (d < -kPi) d += 2.0f * kPi;
    return std::fabs(d) <= e;
}

// A synthetic XSR (the cleria-cotest builder): class-2 ops with their operand-type words.
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
    std::vector<std::uint8_t> bytes() {
        code.push_back(std::int32_t(1u << 24));   // end
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
using I_ = Asm;

static Actor& add_actor(ActorWorld& w, int id, Vec3 pos, float yaw) {
    w.actors().emplace_back();
    Actor& a = w.actors().back();
    a.instance_id = id; a.cast_id = 200; a.pos = pos; a.yaw = yaw;
    a.look.cmd = a.look.dir = heading_of_yaw(yaw);   // the spawn's FUN_00459ff0
    a.speed30c = 0.035f;
    return a;
}
static auto no_ai = [](const Actor&) { return false; };

int main(int, char**) {
    // ---- FUN_00443a80: Falcom degrees -> engine heading [BYTES 0x443a80] -----------------------
    check(near(falcom_heading(0), kPi / 2), "deg 0 -> heading +pi/2 (+Z)");
    check(near(falcom_heading(90), 0.0f), "deg 90 -> heading 0 (+X)");
    check(near(falcom_heading(180), -kPi / 2), "deg 180 -> heading -pi/2");
    check(near(falcom_heading(270), -kPi), "deg 270 -> v = -180 -> heading -pi (range [-180, 180))");
    check(near(falcom_heading(-90), -kPi), "deg -90 -> v = 180 -> wraps to -180");
    check(near(falcom_heading(315), 135.0f * 0.01745329238f), "deg 315 -> v = -225 -> 135");
    check(near(falcom_heading(450), 0.0f), "deg 450 -> v = -360 -> 0");
    {
        std::uint32_t rng[4] = {1, 2, 3, 4}, before[4] = {1, 2, 3, 4};
        const float h = falcom_heading(-1, rng);
        check(h == 0.0f && std::memcmp(rng, before, sizeof rng) != 0,
              "deg -1: one xorshift draw, ((r>>17)>>16) % 7 = 0 -> heading 0 always");
        check(falcom_heading(360, nullptr) == 0.0f, "deg 360: the same 'random' path -> 0");
    }
    // The CleriaCore yaw of a Chr_Dir is still deg (memory: yaw = deg, no 90 flip).
    for (int d : {0, 45, 90, 135, 180, 225, 270, 315}) {
        const float y = yaw_of_heading(falcom_heading(d));
        if (!same_angle(y, float(d) * kPi / 180.0f)) { check(false, "yaw_of_heading(falcom_heading(d)) == d"); break; }
        if (d == 315) check(true, "yaw_of_heading(falcom_heading(d)) == d degrees for the 8 compass dirs");
    }

    // ---- FUN_005eee60 via look_turn -------------------------------------------------------------
    {
        float h = 0.0f;
        int n = 0;
        while (h != kPi / 2 && n < 100) { h = look_turn(h, kPi / 2, 0.18f); ++n; }
        check(n == 9, "90 degrees at 0.18 rad/tick: 9 ticks (8 steps + the snap)");
        h = kPi / 2; n = 0;
        const float goal = -kPi / 2;
        while (!(h == goal) && n < 100) { h = look_turn(h, goal, 0.18f); ++n; }
        check(n == 18, "an exact 180 at 0.18: 18 ticks");
        check(near(look_turn(kPi / 2, -kPi / 2, 0.18f), kPi / 2 - 0.18f + 2 * kPi, 1e-4f) ||
              near(look_turn(kPi / 2, -kPi / 2, 0.18f), kPi / 2 - 0.18f, 1e-4f),
              "the exact opposite from +pi/2 turns -step (diff == 0x2000 takes the else arm)");
        check(look_turn(0.3f, 1.0f, 0.0f) == 0.3f, "rate 0 (TurnSpeed 0) never turns");
        check(look_turn(0.0f, 3.0f, 100.0f * 0.18f) == 3.0f, "TurnSpeed 100: one tick");
        check(look_turn(1.0f, 1.0f, 0.18f) == 1.0f, "cur == goal: untouched");
        check(near(npc_turn_rate(LookState{}, true), 0.35f) && near(npc_turn_rate(LookState{}, false), 0.18f),
              "NPC rate 0.35 running / 0.18 walking x +0x36c (1.0)");
        LookState s; s.mult = 0.5f;
        check(near(npc_turn_rate(s, false), 0.09f) && near(player_turn_rate(false), 0.18f),
              "TurnSpeed scales the NPC rate; the player's rate has no +0x36c");
    }

    // ---- the look modes ---------------------------------------------------------------------------
    {
        LookState s;
        look_dir_cmd(s, 1.0f);
        check(s.mode == 0 && look_goal(s, 0, 0, nullptr) == 1.0f, "mode 0: the goal is +0x308");
        look_at_point(s, 0.0f, 5.0f);
        check(s.mode == 2 && near(look_goal(s, 0, 0, nullptr), kPi / 2), "LookAtPos: mode 2, atan2(dz, dx)");
        const float t[2] = {-3.0f, 0.0f};
        look_at_actor(s, 87, true);
        check(s.mode == 1 && s.target == 87 && near(look_goal(s, 0, 0, t), kPi) && s.px == -3.0f,
              "LookAtChr: mode 1, the target's position copied into +0x350");
        check(near(look_goal(s, 0, 0, nullptr), kPi), "mode 1 with the target gone keeps the last copy");
        look_at_actor(s, 55, false);
        check(s.mode == 1 && s.target == 87, "LookAtChr on a missing target changes nothing");
        look_at_actor(s, 0, false);
        check(s.mode == 0 && s.target == 0, "LookAtChr(id, 0) releases: mode 0, +0x370 = 0");
        look_fixed_dir(s, -0.5f);
        check(s.mode == 3 && look_goal(s, 9, 9, t) == -0.5f, "LookAtDir: mode 3 on +0x360");
    }

    // ---- the world: ops turn, they do not snap --------------------------------------------------
    {
        ActorWorld w(WorldTables{});
        Actor& a = add_actor(w, 50, {0, 0, 0}, 0.0f);   // facing +Z
        w.chr_dir(50, 180.0f);
        check(a.look.mode == 0 && near(a.look.cmd, -kPi / 2) && a.yaw == 0.0f, "Chr_Dir writes +0x308 only");
        w.tick_facing(no_ai);
        check(near(heading_of_yaw(a.yaw), kPi / 2 - 0.18f, 1e-4f) || same_angle(heading_of_yaw(a.yaw), kPi / 2 - 0.18f),
              "one tick later: turned 0.18 rad");
        int n = 1;
        while (!same_angle(a.yaw, kPi, 1e-5f) && n < 100) { w.tick_facing(no_ai); ++n; }
        check(n == 18, "the 180 completes in 18 ticks");
        w.chr_dir(50, 29.9f);
        check(near(a.look.cmd, falcom_heading(29)), "the float operand is truncated (cvttss2si): 29.9 -> 29");
        w.chr_turn_speed(50, 100.0f);
        w.chr_dir(50, 0.0f);
        w.tick_facing(no_ai);
        check(same_angle(a.yaw, 0.0f, 1e-5f), "TurnSpeed(100): a 180 in one tick");
        w.chr_turn_speed(50, 1.0f);
        // Chr_Pos snaps the facing to +0x308.
        w.chr_dir(50, 90.0f);
        w.chr_pos(50, 1, 0, 1);
        check(same_angle(a.yaw, kPi / 2, 1e-5f) && near(a.look.dir, 0.0f), "Chr_Pos: +0x314 = +0x360 = +0x308 at once");
        // LookAtChr tracks a moving target, and the AI-owned actors are left alone.
        Actor& b = add_actor(w, 60, {1, 0, 5}, 0.0f);
        w.chr_turn_speed(50, 100.0f);
        w.chr_lookat(50, 60);
        w.tick_facing(no_ai);
        check(same_angle(heading_of_yaw(a.yaw), kPi / 2, 1e-4f), "LookAtChr: faces the target");
        b.pos = {6, 0, 1};
        w.tick_facing(no_ai);
        check(same_angle(heading_of_yaw(a.yaw), 0.0f, 1e-4f), "...and keeps tracking it as it moves");
        w.chr_lookat(50, 0);
        w.chr_dir_chr(50, 60);
        check(a.look.mode == 0 && same_angle(a.look.cmd, 0.0f), "Chr_DirChr: a one-shot +0x308 toward the target");
        b.pos = {1, 0, 9};
        w.tick_facing(no_ai);
        check(same_angle(heading_of_yaw(a.yaw), 0.0f, 1e-4f), "...which does not track");
        const float by = b.yaw;
        w.chr_dir(60, 180.0f);
        w.tick_facing([](const Actor& x) { return x.instance_id == 60; });
        check(b.yaw == by && same_angle(b.look.cmd, heading_of_yaw(by)), "an AI-owned actor: +0x308 follows its facing");
    }

    // ---- the walk: +0x308 is the path, the facing turns toward it -------------------------------
    {
        ActorWorld w(WorldTables{});
        Actor& a = add_actor(w, 70, {0, 0, 0}, 0.0f);   // facing +Z, then walk -Z
        w.chr_moveto(70, 0.0f, -5.0f, 0.1f, 1.0f);
        w.advance_cutscene_moves();
        w.tick_facing(no_ai);
        check(near(a.pos.z, -0.035f, 1e-5f) && near(a.pos.x, 0.0f, 1e-5f), "MoveTo steps along +0x308 (0.035/tick)");
        check(!same_angle(a.yaw, kPi, 1e-3f), "...while the facing only starts turning (no snap)");
        for (int k = 0; k < 30; ++k) { w.advance_cutscene_moves(); w.tick_facing(no_ai); }
        check(same_angle(a.yaw, kPi, 1e-4f), "...and has turned into the path after 18 ticks");
        w.chr_run(70, true);
        check((a.flags12c & 0x20u) != 0, "Chr_RunOrWalk(1) sets +0x12c & 0x20");
        const float z0 = a.pos.z;
        w.advance_cutscene_moves();
        check(near(a.pos.z, z0 - 0.035f, 1e-5f), "an NPC's run does not change its speed (no +0x410 in the mover)");
        w.chr_run(70, false);
        check((a.flags12c & 0x20u) == 0, "Chr_RunOrWalk(0) clears it");
    }

    // ---- Chr_Pattern / Chr_Animate --------------------------------------------------------------
    {
        ActorWorld w(WorldTables{});
        Actor& a = add_actor(w, 80, {0, 0, 0}, 0.0f);
        w.chr_pattern(80, 304, 3);
        check(a.pattern.pinned() && a.pattern.tag == 304 && a.pattern.frame == 3, "Chr_Pattern pins (tag, frame)");
        w.chr_animate(80, 400);
        check(!a.pattern.pinned() && a.pattern.frame == 0 && a.anim_motion == 400,
              "Chr_Animate sets the motion and releases the pin (+0xa1c = -1, +0xa20 = 0)");
        w.chr_animate(80, -1);
        check(a.anim_motion == -1, "Chr_Animate(-1): back to the locomotion motion");
        w.chr_pattern(80, -1, 0);
        check(!a.pattern.pinned(), "Chr_Pattern(id, -1, 0) releases too");
    }

    // ---- Chr_Tremble [BYTES 0x447306..0x447344, 0x4463be] ---------------------------------------
    {
        Tremble t;
        tremble_set(t, 30, 3, 1.0f, 0.0f);
        check(t.cnt == 6 && t.period == 3, "set (30, 3, 1, 0): counter = 2 x period");
        std::string wave;
        for (int k = 0; k < 12; ++k) { tremble_step(t); wave += tremble_active(t) ? '1' : '0'; }
        check(wave == "111000111000", "a square wave: offset while counter >= period (3 on / 3 off)");
        for (int k = 0; k < 18; ++k) tremble_step(t);
        check(t.ticks == 0 && t.dx == 0.0f, "after 30 ticks the offsets are zeroed");
        tremble_set(t, 0, 3, 1.0f, 2.0f);
        check(t.period == 0 && t.dx == 0.0f && t.dy == 0.0f && !tremble_active(t), "ticks 0 stops it");
        tremble_set(t, -1, 2, 1.5f, 0.0f);
        for (int k = 0; k < 500; ++k) tremble_step(t);
        check(t.ticks == -1 && t.dx == 1.5f, "ticks -1 trembles forever");
    }

    // ---- Chr_FlyTo / Chr_FlyOnOff [BYTES 0x45f6d3..0x45f80b] ------------------------------------
    {
        ActorWorld w(WorldTables{});
        Actor& a = add_actor(w, 90, {0, 0, 0}, 0.0f);
        w.chr_fly(90, true);
        w.chr_flyto(90, 3.0f, 4.0f, 0.0f, 0.01f, 2.0f);
        check(near(a.fly_r2, 0.025f * 0.025f) && w.chr_moving(90), "FlyTo: radius clamped to 0.025; Wait_ChrMoving waits");
        w.chr_flyto(90, 3.0f, 4.0f, 0.0f, 0.1f, 1.0f);
        int n = 0;
        while (w.chr_moving(90) && n < 1000) { w.advance_cutscene_moves(); ++n; }
        const float d = std::sqrt((a.pos.x - 3) * (a.pos.x - 3) + (a.pos.y - 4) * (a.pos.y - 4) + a.pos.z * a.pos.z);
        check(d <= 0.1f && n >= 140 && n <= 146, "a flyer flies the 3D line at 0.035 per tick and arrives (5 / 0.035)");
        Actor& b = add_actor(w, 91, {0, 0, 0}, 0.0f);
        w.chr_flyto(91, 3.0f, 4.0f, 0.0f, 0.1f, 1.0f);
        w.advance_cutscene_moves();
        check(b.pos.y == 0.0f && near(b.pos.x, 0.035f * 0.6f, 1e-4f), "a non-flyer keeps only v cos(pitch), on the ground");
    }

    // ---- the VM dispatch ------------------------------------------------------------------------
    {
        struct Rec : XsoHost {
            std::vector<std::string> log;
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int) const override { return false; }
            void put(const char* f, int a, double b = 0, double c = 0, double d = 0, double e = 0) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s %d %g %g %g %g", f, a, b, c, d, e);
                log.push_back(buf);
            }
            void chr_turn_speed(int id, float m) override { put("turn", id, m); }
            void chr_dir_chr(int id, int t) override { put("dirchr", id, t); }
            void chr_lookat(int id, int t) override { put("lookat", id, t); }
            void chr_look_dir(int id, float d) override { put("lookdir", id, d); }
            void chr_run(int id, bool r) override { put("run", id, r); }
            void chr_pattern(int id, int t, int f) override { put("pattern", id, t, f); }
            void chr_foot_sound(int id, int on) override { put("foot", id, on); }
            void chr_fly(int id, bool on) override { put("fly", id, on); }
            void chr_flyto(int id, float x, float y, float z, float r, float s) override { put("flyto", id, x, y + z, r, s); }
            void chr_tremble(int id, int t, int p, float dx, float dy) override { put("tremble", id, t, p, dx, dy); }
        } h;
        Asm as;
        as.op(0x132, {I_::I(198), I_::F(0.7f)});
        as.op(0x134, {I_::I(13), I_::I(998)});
        as.op(0x136, {I_::I(92), I_::I(13)});
        as.op(0x138, {I_::I(199), I_::F(180.0f)});
        as.op(0x145, {I_::I(13), I_::I(1)});
        as.op(0x152, {I_::I(87), I_::I(304), I_::I(5)});
        as.op(0x159, {I_::I(199), I_::I(3)});
        as.op(0x15d, {I_::I(203), I_::I(7)});
        as.op(0x144, {I_::I(203), I_::F(1.0f), I_::F(2.0f), I_::F(3.0f), I_::F(0.1f), I_::F(6.0f)});
        as.op(0x162, {I_::I(196), I_::I(60), I_::I(5), I_::F(1.0f), I_::F(0.0f)});
        XsoVm vm; vm.host = &h;
        vm.run(Xso::parse(as.bytes()));
        const std::vector<std::string> want = {
            "turn 198 0.7 0 0 0", "dirchr 13 998 0 0 0", "lookat 92 13 0 0 0", "lookdir 199 180 0 0 0",
            "run 13 1 0 0 0", "pattern 87 304 5 0 0", "foot 199 3 0 0 0", "fly 203 1 0 0 0",
            "flyto 203 1 5 0.1 6", "tremble 196 60 5 1 0"};
        bool ok = h.log.size() == want.size();
        for (std::size_t k = 0; ok && k < want.size(); ++k) ok = h.log[k] == want[k];
        if (!ok) for (const std::string& s : h.log) std::printf("    got: %s\n", s.c_str());
        check(ok, "0x132/134/136/138/145/152/159/15d/144/162 reach their host hooks with the stub layouts");
    }

    std::printf("[chr] %s (%d failed)\n", fails ? "FAIL" : "all OK", fails);
    return fails ? 1 : 0;
}
