// cleria-accesstest — CleriaCore's accessibility assists and cheats (src/cleria/access.hpp,
// build/shell_re/ACCESS.md): the retail defaults, the multiplier math at the hook sites, the
// cutscene-skip state machine, the flash limiter, cleria.ini [cleria.access] / [cleria.cheats], and the
// library hooks (the SP rate in pickup(), the drop rate in roll_enemy_drops(), the text speed in the
// event windows). Usage: cleria-accesstest [ignored]
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/access.hpp"
#include "cleria/camrig.hpp"
#include "cleria/ef2_sim.hpp"
#include "cleria/enemies.hpp"
#include "cleria/inventory.hpp"
#include "cleria/items.hpp"
#include "cleria/pickup.hpp"
#include "cleria/settings.hpp"
#include "cleria/wnddialog.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[access] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

int main() {
    using access::SkipGate;
    using access::SkipMode;

    {   // retail defaults
        const access::Settings s;
        check(s.exp_rate == 1 && s.sp_rate == 1 && s.drop_rate == 1 && s.damage_dealt == 1 && s.damage_taken == 1 &&
                  !s.inf_hp && !s.inf_mp && !s.inf_boost && !s.inf_o2 && !s.one_hit_kill && !s.cheats_active(),
              "every cheat defaults to retail (1x / off)");
        check(s.game_speed == 1 && s.shake == 1 && !s.reduce_flashes && s.text_speed == 0 && !s.mod_save_retry &&
                  s.cutscene_skip == int(SkipMode::Double),
              "assists default to retail; the skip defaults to Double press");
    }
    {   // the multipliers
        check(access::damage_dealt(0, 5) == 0 && access::damage_dealt(17, 1) == 17 && access::damage_dealt(17, 2) == 34 &&
                  access::damage_dealt(10, 0.25f) == 3 && access::damage_dealt(1, 0.25f) == 1,
              "damage dealt: a guarded 0 stays 0, x1 identity, x2, x0.25 rounds, min 1");
        check(access::damage_taken(40, 0) == 0 && access::damage_taken(40, 1) == 40 && access::damage_taken(40, 0.5f) == 20 &&
                  access::damage_taken(1, 0.25f) == 1 && access::damage_taken(0, 4) == 0 && access::damage_taken(9, 4) == 36,
              "damage taken: x0 = none, x1 identity, x0.5, min 1, x4");
    }
    {   // the skip gate
        SkipGate g;
        check(g.step(SkipMode::Single, true, true) && !g.step(SkipMode::Single, false, true),
              "Single: the press edge skips at once (CleriaCore's skip)");
        g.reset();
        check(g.step(SkipMode::Always, false, false) && g.step(SkipMode::Always, false, false) &&
              g.prompt(SkipMode::Always) == SkipGate::Prompt::None, "Always: skips with no press, every tick, no prompt");
        check(!g.step(SkipMode::Off, true, true) && g.prompt(SkipMode::Off) == SkipGate::Prompt::None,
              "Off: a press never skips (retail), no prompt");
        g.reset();
        bool first = g.step(SkipMode::Double, true, true);
        const bool armed = g.prompt(SkipMode::Double) == SkipGate::Prompt::PressAgain && g.window == access::kDoubleWindow;
        for (int i = 0; i < 60; ++i) first |= g.step(SkipMode::Double, false, i < 5);
        const bool second = g.step(SkipMode::Double, true, true);
        check(!first && armed && second && g.prompt(SkipMode::Double) == SkipGate::Prompt::None,
              "Double: press 1 shows 'Press again', press 2 within the window skips");
        g.reset();
        g.step(SkipMode::Double, true, true);
        bool late = false;
        for (int i = 0; i < access::kDoubleWindow; ++i) late |= g.step(SkipMode::Double, false, false);
        const bool expired = g.prompt(SkipMode::Double) == SkipGate::Prompt::None;
        late |= g.step(SkipMode::Double, true, true);
        check(!late && expired && g.prompt(SkipMode::Double) == SkipGate::Prompt::PressAgain,
              "Double: after the 2 s window the next press only re-arms the prompt");
        g.reset();
        int at = -1;
        for (int i = 0; i < 200 && at < 0; ++i)
            if (g.step(SkipMode::Hold, i == 0, true)) at = i + 1;
        check(at == access::kHoldTicks, "Hold: skips on the 60th tick held");
        g.reset();
        bool early = false;
        for (int i = 0; i < 30; ++i) early |= g.step(SkipMode::Hold, i == 0, true);
        const float frac = g.hold_fraction();
        early |= g.step(SkipMode::Hold, false, false);   // released at half
        const bool hint = g.prompt(SkipMode::Hold) == SkipGate::Prompt::Hold && g.hold == 0;
        for (int i = 0; i < 40; ++i) early |= g.step(SkipMode::Hold, i == 0, true);   // a new hold restarts
        check(!early && std::fabs(frac - 0.5f) < 1e-6f && hint && g.hold == 40,
              "Hold: a release resets the fill (hint stays), a new press starts over");
        g.reset();
        int carried = -1;                                  // no press edge: held from before the cutscene
        for (int i = 0; i < 200 && carried < 0; ++i)
            if (g.step(SkipMode::Hold, false, true)) carried = i + 1;
        g.reset();                                         // the next scene of a chain, still held
        int chained = -1;
        for (int i = 0; i < 200 && chained < 0; ++i)
            if (g.step(SkipMode::Hold, false, true)) chained = i + 1;
        check(carried == access::kHoldTicks && chained == access::kHoldTicks,
              "Hold: a hold carried in (or kept across a chain of scenes) skips each after 60 ticks");
    }
    {   // the flash limiter
        access::FlashLimiter f;
        std::uint32_t rgb = 0;
        float a = 0;
        f.step(false, 0xffffff, 1.0f, &rgb, &a);
        check(rgb == 0xffffff && a == 1.0f, "ReduceFlashes off: the fader is drawn as is");
        f = {};
        f.step(true, 0x000000, 1.0f, &rgb, &a);
        check(rgb == 0 && a == 1.0f, "a fade to black is untouched");
        f = {};
        f.step(true, 0xffffff, 1.0f, &rgb, &a);
        const float a1 = a;
        const float lum = access::luminance(rgb);
        int frames = 1;
        while (a < 1.0f && frames < 100) { f.step(true, 0xffffff, 1.0f, &rgb, &a); ++frames; }
        check(std::fabs(a1 - access::kFlashRise) < 1e-6f && lum <= access::kFlashLum + 0.01f && frames == 9,
              "a white flash: dimmed to <= 0.55 luma, rises 0.12 / frame (9 frames to full)");
        f.step(true, 0xffffff, 0.0f, &rgb, &a);
        check(a == 0.0f, "the flash falls at once");
    }
    {   // cleria.ini
        Settings s;
        s.access.cutscene_skip = int(SkipMode::Hold);
        s.access.text_speed = 2;
        s.access.reduce_flashes = true;
        s.access.shake = 0.3f;
        s.access.game_speed = 0.5f;
        s.access.inf_hp = true;
        s.access.one_hit_kill = true;
        s.access.exp_rate = 2.0f;
        s.access.sp_rate = 3.0f;
        s.access.drop_rate = 0.0f;
        s.access.damage_dealt = 1.5f;
        s.access.damage_taken = 0.0f;
        Ini c;
        write_cleria(c, s);
        Settings t;
        read_cleria(c, t);
        const access::Settings& r = t.access;
        check(r.cutscene_skip == 2 && r.text_speed == 2 && r.reduce_flashes && std::fabs(r.shake - 0.3f) < 1e-6f &&
                  r.game_speed == 0.5f && r.inf_hp && !r.inf_mp && r.one_hit_kill && r.exp_rate == 2 && r.sp_rate == 3 &&
                  r.drop_rate == 0 && r.damage_dealt == 1.5f && r.damage_taken == 0 &&
                  c.has("cleria.access", "CutsceneSkip") && c.has("cleria.cheats", "ExpRate") &&
                  c.has("cleria.cheats", "InfiniteHP"),
              "cleria.ini round trip: [cleria.access] / [cleria.cheats]");
        Ini bad;
        bad.parse("[cleria.cheats]\r\nExpRate=99\r\nDamageTaken=-3\r\n[cleria.access]\r\nGameSpeed=0\r\nCutsceneSkip=7\r\n");
        Settings u;
        read_cleria(bad, u);
        check(u.access.exp_rate == access::kRateMax && u.access.damage_taken == 0 &&
                  u.access.game_speed == access::kSpeedMin && u.access.cutscene_skip == 4,
              "out-of-range values clamp to the offered ranges");
        Settings d;
        Ini none;
        read_cleria(none, d);
        check(!d.access.cheats_active() && d.access.game_speed == 1, "no keys: the retail defaults");
        access::Settings k;
        check(access::set_by_key(k, "exprate", "2") && k.exp_rate == 2 && access::set_by_key(k, "OneHitKill", "1") &&
                  k.one_hit_kill && !access::set_by_key(k, "Nope", "1"),
              "set_by_key (the drive's opt=Key=value), case-insensitive");
        k.reset_cheats();
        check(!k.cheats_active(), "Reset all cheats");
    }
    {   // the SP rate in pickup() (FUN_00585ff0's mag * SPrate 0x76a5f8 * s)
        ItemTable items;
        items.items.resize(0x80);
        items.items[0x7a].magnitude = 100;
        InvState inv;
        EffectEnv env;
        int pop = 0;
        pickup(inv, items, env, 0x7a, &pop);
        const int base = inv.sp;
        env.sp_rate = 2.0f;
        pickup(inv, items, env, 0x7a, &pop);
        check(base == 100 && inv.sp == 300 && pop == 200, "SP x1 = the gold's magnitude; x2 doubles the wallet gain + popup");
    }
    {   // the drop rate in roll_enemy_drops() (FUN_00478510's slot chance)
        ef2sim::Rng rng;
        Enemy e;
        e.drops[0] = {0x78, 30.0f};
        auto count = [&](float rate) {
            DropContext c;
            c.boss_lock = true;
            c.rate = rate;
            int n = 0;
            for (int i = 0; i < 2000; ++i) {
                std::vector<Pickup> out;
                roll_enemy_drops(rng, e, 14, {0, 0, 0}, c, out);
                n += int(out.size());
            }
            return n;
        };
        const int r1 = count(1.0f), r2 = count(2.0f), r0 = count(0.0f), r10 = count(10.0f);
        check(r1 > 500 && r1 < 700 && r2 > 1100 && r2 < 1300 && r0 == 0 && r10 == 2000,
              "drop rate: 30% slot x1 ~600/2000, x2 ~1200, x0 none, x10 always");
    }
    {   // the shake scale in the camera rig (FUN_00427c90's offset, applied by FUN_0042a080)
        auto eye_dx = [](float scale, int* draws) {
            Scm scm;
            CameraRegion r;
            r.target.mode = 1;
            r.eye.mode = 3; r.eye.pitch = 0.5f; r.eye.dist = 20; r.fov = 0.5f; r.blend = 0.015f;
            scm.regions.push_back(r);
            int n = 0;
            const auto rng = [&n] { ++n; return 0xC0000000u; };
            CamRig rig; rig.reset(scm);
            rig.shake_scale = scale;
            const CamRig::Out o0 = rig.step({0, 0, 0}, {0, 1, 0}, rng);
            rig.shake({1.0f, 1.0f, 1.0f}, 2);
            const CamRig::Out o1 = rig.step({0, 0, 0}, {0, 1, 0}, rng);
            *draws = n;
            return std::sqrt((o1.eye.x - o0.eye.x) * (o1.eye.x - o0.eye.x) + (o1.eye.y - o0.eye.y) * (o1.eye.y - o0.eye.y) +
                             (o1.eye.z - o0.eye.z) * (o1.eye.z - o0.eye.z));
        };
        int d1 = 0, d0 = 0, dh = 0;
        const float full = eye_dx(1.0f, &d1), none = eye_dx(0.0f, &d0), half = eye_dx(0.5f, &dh);
        check(full > 0.01f && none < 1e-5f && std::fabs(half - full * 0.5f) < 1e-3f && d1 == 3 && d0 == 3 && dh == 3,
              "screen shake: 100% retail, 0% no displacement, 50% half; the 3 RNG draws always happen");
    }
    {   // the text speed in the message box (FUN_0057a0e0's reveal)
        RichMetrics m;
        m.height = 32; m.ascent = 26; m.space = 10;
        m.advance = [](unsigned char) { return 16.0f; };
        auto reveal_ticks = [&](int speed) {
            WndText t;
            t.text_speed = speed;
            t.message(0x57, 1, 2, "Cecilia", "Hello world, a longer line of text", m);
            std::vector<int> snd;
            for (int i = 0; i < 14; ++i) t.update({}, snd);   // the fade-in
            t.arm();
            WndInput press; press.confirm_pressed = true; press.confirm_held = true;
            for (int i = 1; i < 200; ++i) {
                WndText probe = t;
                probe.update(press, snd);
                if (probe.accepted()) return i;          // accepted only once fully revealed
                t.update({}, snd);
            }
            return -1;
        };
        const int retail = reveal_ticks(0), fast = reveal_ticks(1), instant = reveal_ticks(2);
        check(retail > fast && fast > instant && instant <= 2 && retail > 20,
              "text speed: Original types it out, Fast is quicker, Instant shows it at once");
    }
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
