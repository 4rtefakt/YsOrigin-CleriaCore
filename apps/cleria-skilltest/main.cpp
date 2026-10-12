// cleria-skilltest — the MP gauge, the skill charge/release machine and the Boost rules
// (src/cleria/skillmp) against the numbers in build/ability_re/ABILITIES_VERIFIED.md §1-§2.
#include <cmath>
#include <cstdio>
#include <string>

#include "cleria/extra_versions.hpp"
#include "cleria/skillmp.hpp"

using namespace cleria::skillmp;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[skill] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

int main(int, char**) {
    {   // costs
        CostIn c; c.chr = 1; c.slot = 0;
        check(cost(c) == 700, "Yunica Whirlwind costs 700");
        c.boosting = true;
        check(cost(c) == 350, "Yunica pays half while boosting");
        c.boosting = false; c.difficulty = 2;
        check(cost(c) == 1000, "Nightmare adds 300");
        CostIn h; h.chr = 2; h.slot = 0;
        check(cost(h) == 2000, "Hugo Force Shield costs 2000");
        h.boosting = true;
        check(cost(h) == 2000, "only Yunica gets the Boost discount");
        h.bc80 = 0x80000;
        check(cost(h) == 1600, "Reduce MP usage LV3: x0.80");
        CostIn t; t.chr = 3; t.slot = 0;
        check(cost(t) == 300, "Toal Godspeed costs 300");
    }
    {   // the charge: held 121 ticks reaches level 2 with a Lv2 bracelet
        Charge ch;
        bool ready = false; int at = -1;
        for (int k = 1; k <= 200 && !ready; ++k) if (ch.add(kChargeStep, true, 1, 0)) { ready = true; at = k; }
        check(ready && at == 121, "charged at the 121st held tick (T = 3000)");
        check(ch.amount == 3001.0f, "the amount clamps at T + 1");
        Charge c1;
        for (int k = 0; k < 300; ++k) c1.add(kChargeStep, true, 0, 0);
        check(c1.level == 1 && c1.amount == 2.0f, "a Lv1 bracelet stops at level 1");
        Charge c2;
        int at2 = -1;
        for (int k = 1; k <= 200; ++k) if (c2.add(kChargeStep, true, 1, 0x4000)) { at2 = k; break; }
        check(at2 == 81, "Hasten skill recovery (T 2000): 81 ticks");
    }
    {   // hold then release: the plain and the charged cast
        Mp mp; Charge ch;
        SkillIn in; in.magic = 0x74; in.bracelet_level = 1; in.cost.chr = 1; in.section = 2;
        in.skill_held = true;
        SkillOut o = skill_tick(in, mp, ch);
        check(!o.cast && ch.level == 1, "the first held tick arms level 1");
        in.skill_held = false;
        o = skill_tick(in, mp, ch);
        check(o.cast && o.section == 0x10d && o.cost == 700 && o.damage_class == 0 && o.level == 1,
              "release casts 0x10d for 700, damage class 0");
        check(ch.level == 0 && ch.amount == 0.0f, "the cast clears the charge");
        in.skill_held = true;
        for (int k = 0; k < 121; ++k) o = skill_tick(in, mp, ch);
        check(ch.level == 2, "held 121 ticks: charged");
        in.skill_held = false;
        o = skill_tick(in, mp, ch);
        check(o.cast && o.section == 0x110, "release casts the charged 0x110");
        // not affordable: no charge
        Mp low; low.mp = low.lag = 500;
        in.skill_held = true;
        o = skill_tick(in, low, ch);
        check(ch.level == 0, "no charge while MP < cost");
        // a skill section in progress (kind 5) does not charge
        in.kind = 5;
        o = skill_tick(in, mp, ch);
        check(ch.level == 0, "no charge during a skill (kind 5)");
        // release refused in section 5 with a Lv1 bracelet: stays armed (the early return)
        in.kind = 0; in.bracelet_level = 0;
        skill_tick(in, mp, ch);
        in.skill_held = false; in.section = 5;
        o = skill_tick(in, mp, ch);
        check(!o.cast && ch.level == 1, "a refused release keeps a Lv1 charge armed");
        in.section = 2;
        o = skill_tick(in, mp, ch);
        check(o.cast && o.section == 0x10d, "...and casts once the section allows it");
    }
    {   // MP regen and the lag stall
        Mp mp;
        mp.mp -= 700;
        int still = 0;
        while (mp.mp == 2300.0f && still < 1000) { mp.tick(0); ++still; }
        check(still == 71 && mp.mp == 2307.0f, "a 700 spend stalls 70 ticks, then +7 (Lv1)");
        mp.tick(0);
        check(mp.mp == 2307.0f && mp.lag == 2307.0f, "the next tick only resyncs the lag");
        mp.tick(0);
        check(mp.mp == 2314.0f, "regen on alternate ticks");
        Mp full; full.tick(3);
        check(full.mp == kMpMax, "no regen past max");
    }
    {   // Boost
        check(boost_passive(100, 100, 0, 0) == 1.0f, "passive fill at full HP: 1/tick");
        check(boost_passive(0, 100, 0, 0) == 26.0f, "passive fill at 0 HP: 26/tick");
        check(boost_passive(50, 100, 2, 0x10000) == 32.0f, "Nightmare B 30, blessing x2");
        check(boost_passive(150, 100, 0, 0) == -11.5f,
              "HP over the max: negative, unclamped as retail (0x467c43..; ROUTE_F13 D8)");
        check(boost_drain(0, false, false) == 30.0f && boost_drain(2, false, false) == 90.0f &&
              boost_drain(2, true, false) == 400.0f && boost_drain(0, false, true) == 18.0f,
              "drain 30 / 90 / 400, x0.6 with accessory 0x3b");
        check(!boost_can_add(false, false, false, false, 0) && boost_can_add(true, false, false, false, 0) &&
              !boost_can_add(true, true, false, false, 0) && !boost_can_add(true, false, false, true, 0) &&
              !boost_can_add(true, false, false, false, kBoostFull), "the add gates (187, 161, boosting, full)");
        check(attack_kind(0x100) == 1 && attack_kind(0x109) == 3 && attack_kind(0x10d) == 5 &&
              attack_kind(0x112) == 4 && attack_kind(2) == 0, "attack kinds from 0x69bc14");
        check(!boost_hit_counts(5) && boost_hit_counts(1), "skill hits build no Boost");
    }
    {   // BOOST_SKILLS.md §2.6 / §3.3 / §3.5 / §3.6
        check(attack_kind(0x12c) == 14 && attack_kind(0x139) == 0 && attack_kind(0x13a) == 8 &&
              attack_kind(0x13d) == 0 && attack_kind(0x134) == 13 && attack_kind(0x12b) == 0,
              "attack kinds past 0x12a (Hugo's super 300 = 14)");
        Armor a = armor(1, 0x10c, 0, 500.0f);
        check(a.active && a.guard && a.spend == 500.0f, "Yunica 0x10c with 500 MP: guard, 500 spent");
        a = armor(1, 0x10f, 0, 499.0f);
        check(a.active && !a.guard, "Yunica 0x10f under 500 MP: x 0.75 and 16 i-frames");
        a = armor(3, 0x10e, 0, 200.0f);
        check(a.active && a.guard && a.spend == 200.0f, "Toal 0x10e, Marker 0, 200 MP: guard");
        check(!armor(3, 0x10e, 1, 3000.0f).active, "Toal 0x10e past Marker 1: no armour");
        check(!armor(3, 0x10c, 0, 3000.0f).active && !armor(1, 0x10d, 0, 3000.0f).active && !armor(2, 0x10c, 0, 3000.0f).active,
              "no armour in the other sections / for Hugo");
        check(armor(3, 0x111, 0, 100.0f).active && !armor(3, 0x111, 0, 100.0f).guard, "Toal 0x111 under 200 MP: x 0.75");
        check(pull_weight(10.0f, false) == 0.0f && pull_weight(9.5f, false) == 0.1f && pull_weight(0.5f, false) == 1.0f,
              "pull weight: nothing at 10, 0.1 at 9.5, 1.0 inside 1");
        check(pull_weight(4.5f, false) == 0.6f && pull_weight(1.2f, true) == 1.0f && pull_weight(9.9f, true) == 0.4f,
              "pull weight: 0.6 at 4.5; boosting scales the thresholds x 1.5");
        check(slow_fall(-0.2f, false) == -0.2f * 0.94f + 0.006f && slow_fall(-0.2f, true) == -0.2f * 0.925f + 0.009f,
              "Toal's slow fall: t x 0.94 + 0.006, charged t x 0.925 + 0.009");
    }
    {   // Yunica Extra (EXTRA_VERSIONS.md §2, §5)
        namespace x = cleria::extra;
        x::Counters c;
        int at = -1, n = 0; x::YunicaOut v;
        for (int t = 1; t <= 40; ++t) { const x::YunicaOut o = x::yunica_tick(c, 0x10d); if (o.bullets) { at = t; ++n; v = o; } }
        check(at == 12 && n == 1 && v.bullets == 3 && v.a0 == 1.0471976f && v.step == 2.0943952f && v.speed == 0.12f &&
              v.accel == 0.002f && v.f168 == 6.0f, "Yunica Extra 0x10d: three shots at the 12th tick, 60 + 120 i degrees");
        x::yunica_tick(c, 0);
        check(c.c814 == 0, "the counter clears outside the section");
        for (int t = 1; t <= 12; ++t) v = x::yunica_tick(c, 0x110);
        check(v.bullets == 4 && v.a0 == 0.7853982f && v.step == 1.5707964f && v.speed == 0.2f && v.accel == 0.004f && v.f168 == 5.0f,
              "Yunica Extra 0x110: four shots, 45 + 90 i degrees, speed 0.2");
        x::Counters d; at = -1; x::Emitter em;
        for (int t = 1; t <= 40; ++t) { const x::YunicaOut o = x::yunica_tick(d, 0x10c); if (o.em.on) { at = t; em = o.em; } }
        check(at == 24 && em.ahead == 1.5f && em.c == 6 && em.p == 3 && em.effect == 0x76, "Yunica Extra 0x10c: the runner at tick 24, 1.5 ahead, C 6 P 3");
        x::Counters e; at = -1;
        for (int t = 1; t <= 40; ++t) { const x::YunicaOut o = x::yunica_tick(e, 0x10f); if (o.em.on) { at = t; em = o.em; } }
        check(at == 30 && em.ahead == 3.5f && em.c == 6 && em.p == 5, "Yunica Extra 0x10f: tick 30, 3.5 ahead, C 6 P 5");
        // The runner on a flat floor at y = 0: FUN_005916e0 starts it (P >> 1) steps ahead and 0.75 down; it pulses
        // on its ticks 1 + k (P + 1) while <= C P.
        auto floor0 = [](const cleria::Vec3& o, const cleria::Vec3& dd, cleria::Vec3* hit) {
            if (dd.y >= 0.0f || o.y < 0.0f || o.y + dd.y > 0.0f) return false;
            *hit = {o.x, 0.0f, o.z};
            return true;
        };
        x::Runner r = x::Runner::make({1.5f, 0.75f, 0.0f}, 0.0f, 0.3f, 0.0f, 1.0f, 6, 3, 0x76, 10.0f);
        check(r.life == 19 && r.pos.x == 1.5f + 0.3f && r.pos.y == 0.0f, "runner: life C P + 1, (P >> 1) steps ahead, 0.75 down");
        std::string ticks; int pulses = 0, lived = 0; float x0 = 0, x1 = 0;
        for (int t = 1; t <= 40 && !r.dead; ++t) {
            cleria::Vec3 p{};
            if (r.tick(floor0, &p)) { ticks += std::to_string(t) + " "; if (++pulses == 1) x0 = p.x; if (pulses == 2) x1 = p.x; }
            if (!r.dead) lived = t;
        }
        check(ticks == "1 5 9 13 17 " && lived == 18, "runner C 6 P 3: pulses on ticks 1 5 9 13 17, 18 live ticks");
        check(std::fabs(x0 - 1.8f) < 1e-5f && std::fabs(x1 - x0 - 1.2f) < 1e-5f, "runner: the first pulse where it was made, the next 4 steps on");
        x::Runner w = x::Runner::make({0, 0.75f, 0}, 0.0f, 0.3f, 0.0f, 1.0f, 6, 5, 0x76, 10.0f);
        auto wall = [&](const cleria::Vec3& o, const cleria::Vec3& dd, cleria::Vec3* hit) {
            if (dd.x > 0.0f && o.x < 2.0f && o.x + dd.x >= 2.0f) { *hit = {2.0f, o.y, o.z}; return true; }   // a wall at x = 2
            return floor0(o, dd, hit);
        };
        pulses = 0;
        for (int t = 1; t <= 40 && !w.dead; ++t) { cleria::Vec3 p{}; pulses += w.tick(wall, &p); }
        check(pulses == 2 && w.dead, "runner: the pulse past a wall is still spawned, then it dies");
        x::Runner a = x::Runner::make({0, 5.0f, 0}, 0.0f, 0.3f, 0.0f, 1.0f, 6, 3, 0x76, 10.0f);
        cleria::Vec3 p{}; a.tick(floor0, &p);
        check(a.vy == -0.75f && std::fabs(a.pos.y - (4.25f - 0.225f)) < 1e-5f, "runner off the floor: falls 0.75 x step a tick");
    }
    {   // Toal Extra (EXTRA_VERSIONS.md §4): which ticks of a section ask for what
        namespace x = cleria::extra;
        auto run = [](int sec, bool demon, int n) {
            x::Counters c;
            std::string s;
            for (int t = 1; t <= n; ++t) {
                const x::ToalOut o = x::toal_tick(c, sec, demon);
                for (int k = 0; k < 2; ++k)
                    if (o.fx[k]) s += "f" + std::to_string(o.fx[k]) + "x" + std::to_string(int(o.fx_scale[k] * 10.0f + 0.5f)) + "@" + std::to_string(t) + " ";
                if (o.em.on) s += "e" + std::to_string(o.em.c) + "/" + std::to_string(o.em.p) + "@" + std::to_string(t) + " ";
                if (o.bullet) s += "b" + std::to_string(int(o.r * 10.0f)) + "@" + std::to_string(t) + " ";
            }
            return s;
        };
        check(run(0x10d, false, 40) == "f174x10@1 " && run(0x110, false, 40) == "f174x10@1 f174x10@10 " && run(0x10d, true, 40).empty() &&
              run(0x110, true, 40).empty(), "Toal Extra Ventus: effect 174 at counter 0 (and 9 charged), human form only");
        check(run(0x10e, false, 30) == "f162x11@1 f162x11@13 f162x11@25 " && run(0x111, false, 14) == "f180x11@1 f180x11@13 " &&
              run(0x10e, true, 22) == "f175x12@1 f175x12@11 f175x12@21 " && run(0x111, true, 12) == "f179x12@1 f179x12@11 ",
              "Toal Extra Ignis: 162 / 180 every 12 ticks at 1.1, demon 175 / 179 every 10 at 1.2");
        check(run(0x10c, false, 22) == "b15@12 b15@15 e4/4@18 b15@18 b15@21 " && run(0x10f, false, 19) == "b25@12 b25@14 b25@16 e5/4@18 b25@18 ",
              "Toal Extra Terra: the runner at tick 18 (C 4 / 5, P 4), a bullet every 3rd / 2nd tick from 12");
        check(run(0x10c, true, 18) == "b15@12 b15@15 e5/5@18 b15@18 " && run(0x10f, true, 18) == "b25@12 b25@14 b25@16 e6/5@18 b25@18 ",
              "Toal Extra Terra, demon form: C + 1, P 5");
        x::Counters c;
        x::toal_tick(c, 0x10c, false); x::toal_tick(c, 0x10e, false);
        check(c.c818 == 0 && c.c81c == 1 && c.c814 == 0, "the three counters clear outside their sections");
    }
    {   // Hugo Extra (EXTRA_VERSIONS.md §3)
        namespace x = cleria::extra;
        check(x::hugo_shield_life(false, false) == 360 && x::hugo_shield_life(true, false) == 380 &&
              x::hugo_shield_life(false, true) == 420 && x::hugo_shield_life(true, true) == 440,
              "Hugo's Force Shield: 360 / 380 ticks, Extra 420 / 440");
        check(x::hugo_flag157(0, 1) == 0 && x::hugo_flag157(1, 0) == 2 && x::hugo_flag157(2, 0) == 1 && x::hugo_flag157(3, 2) == 2,
              "Hugo Extra: flag 157 by bracelet (Ventus 0, Terra 2, Ignis 1, none unchanged)");
    }
    std::printf("[skill] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
