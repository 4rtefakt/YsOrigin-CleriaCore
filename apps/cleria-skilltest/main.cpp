// cleria-skilltest — the MP gauge, the skill charge/release machine and the Boost rules
// (src/cleria/skillmp) against the numbers in build/ability_re/ABILITIES_VERIFIED.md §1-§2.
#include <cmath>
#include <cstdio>
#include <string>

#include "cleria/extra_versions.hpp"
#include "cleria/hit_feedback.hpp"
#include "cleria/skillmp.hpp"
#include "cleria/water_rings.hpp"

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
    {   // Felghana Adol's skill armour (ADOL.md §2.4)
        Armor a = armor(0, 0x10c, 0, 200.0f, 0);
        check(a.active && a.guard && a.spend == 200.0f, "Adol (Felghana) Terra with 200 MP: the guard, 200 spent");
        a = armor(0, 0x10f, 0, 199.0f, 0);
        check(a.active && !a.guard && a.scale == 0.25f, "... under 200 MP: x 0.25");
        check(armor(0, 0x10e, 0, 3000.0f, 0).scale == 0.5f && !armor(0, 0x111, 0, 3000.0f, 0).guard &&
              armor(0, 0x10d, 0, 3000.0f, 0).scale == 0.75f && armor(0, 0x110, 0, 0.0f, 0).active,
              "Ignis x 0.5, Ventus x 0.75, no guard");
        check(!armor(0, 0x100, 0, 3000.0f, 0).active && !armor(0, 0x10c, 0, 3000.0f, 1).active && !armor(0, 0x10e, 0, 3000.0f, 1).active,
              "none outside the skills, none for Napishtim");
        check(armor(1, 0x10f, 0, 499.0f).scale == 0.75f && armor(3, 0x111, 0, 100.0f).scale == 0.75f, "Yunica / Toal keep x 0.75");
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
    {   // Napishtim Adol (ADOL.md §4): the costs, the Terra press counter, the Ignis release
        namespace x = cleria::extra;
        CostIn a; a.chr = 0; a.adol_alt = true;
        int cs[3];
        for (int k = 0; k < 3; ++k) { a.slot = k; cs[k] = cost(a); }
        a.difficulty = 2; a.slot = 1;
        CostIn f; f.chr = 0; f.slot = 0;
        check(cs[0] == 1500 && cs[1] == 1800 && cs[2] == 1200 && cost(a) == 1800 && cost(f) == 1000,
              "Napishtim Adol's skills cost 1500 / 1800 / 1200 (no Nightmare add); Felghana 1000");
        x::Napishtim n;
        bool early = false;
        for (int i = 0; i < 10; ++i) { early = early || n.press(1); n.frame(1, 0x121); }
        const x::Napishtim::Tint t10 = n.frame(1, 0x121);
        const bool fire = n.press(1);
        check(!early && fire && n.count == 0 && n.lock == 60.0f && t10.op == 2 && t10.colour == 0xc0c000u,
              "Terra: the 11th quick press plays 0x124 (lockout 60); at 10 presses the tint is 192 red + green");
        int locked = 0;
        for (int i = 0; i < 60; ++i) { if (n.press(1)) ++locked; n.frame(1, 0x124); }
        check(locked == 0 && n.count == 0 && n.lock == 0.0f, "...no count during the 60-tick lockout");
        x::Napishtim d;
        for (int i = 0; i < 6; ++i) d.press(1);
        std::uint32_t c12 = 0, col6 = 0;
        int op_low = -1;
        for (int i = 0; i < 20; ++i) {
            const x::Napishtim::Tint t = d.frame(1, 0);
            if (i == 0) col6 = t.colour;
            if (i == 11) c12 = d.count;
            op_low = t.op;
        }
        check(col6 == 0x262600u && c12 == 6 && d.count == 0 && op_low == 1 && d.frame(1, 0x124).op == 0,
              "Terra: 6 presses tint 38; idle past 12 ticks the count falls one a tick; the tint is off below 6 (0x124 keeps its flash)");
        x::Napishtim o;
        for (int i = 0; i < 30; ++i) { o.press(0); o.press(2); o.frame(0, 0); }
        o.press(1); o.on_switch();
        check(o.count == 0 && o.since == 0.0f, "the counter only steps with Terra; a bracelet switch clears it");
        check(x::Napishtim::ignis_release(2, false, 0x11b) == 0x11c && x::Napishtim::ignis_release(2, false, 0x11d) == 0x11e &&
              x::Napishtim::ignis_release(2, false, 0x11f) == 0x120 && x::Napishtim::ignis_release(2, true, 0x11b) == -1 &&
              x::Napishtim::ignis_release(1, false, 0x11b) == -1 && x::Napishtim::ignis_release(2, false, 0x11c) == -1,
              "Ignis: the button up in a wind-up (0x11b / 0x11d / 0x11f) starts its swing");
    }
    {   // The kind-0x18 chain lightning (GOXX_17_19.md §3)
        namespace x = cleria::extra;
        const std::int32_t sp[8] = {1, 0x41700000, 0x3f800000, 0x3fe66666, 20, 624, 0x3eb851ec, 6};
        const x::Runner r = x::special_runner(sp, {0, 0.75f, 0}, 0.0f, 10.0f);
        check(r.power == 15.0f && r.scale == 1.8f && r.step == 0.36f && r.effect == 624 && r.p == 6 && r.life == 121,
              "anime_Special(1, ..): the charged Ignis runner (power 15, scale 1.8, step 0.36, effect 624, C 20, P 6)");
        const cleria::Vec3 c0 = x::Chain::centre({0, 0, 0}, 0.0f);
        check(c0.x == 2.0f && c0.z == 2.0f && x::Chain::in_range(c0, {13.9f, 0, 2.0f}) && !x::Chain::in_range(c0, {14.1f, 0, 2.0f}),
              "the chain's scan: 12.0 around the point (2 cos, 2 cos) from the caster");
        // Three monsters on the x axis; 5 hops: nearest first, then least visited / nearest to the last one.
        const std::vector<x::Chain::Target> found = {{7, {6, 0, 0}}, {8, {2, 0, 0}}, {9, {4, 0, 0}}};
        x::Chain ch = x::Chain::make({0, 0, 0}, 0.0f, -15.0f, 5, 25, found, 10.0f, 0x1000001u);
        std::string order;
        for (const x::Chain::Hop& h : ch.hops) order += std::to_string(h.id);
        check(!ch.ray && ch.n == 5 && order == "89789" && ch.first_heading() == 0.0f && ch.power == -15.0f,
              "5 hops over 3 monsters: 8, 9, 7 by distance, then 8, 9 (every key ties: the CRT qsort's order)");
        {
            std::vector<int> q = {5, 3, 9, 1, 7, 3, 8, 2, 6, 4, 0, 9, 5, 1, 7, 2, 8, 6, 4, 0};
            x::crt_qsort(q, [](int a, int b) { return a - b; });
            bool sorted = true;
            for (std::size_t i = 1; i < q.size(); ++i) sorted = sorted && q[i - 1] <= q[i];
            std::vector<std::pair<int, int>> e = {{0, 0}, {0, 1}, {0, 2}};
            x::crt_qsort(e, [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first - b.first; });
            check(sorted && e[0].second == 1 && e[1].second == 2 && e[2].second == 0,
                  "the CRT qsort: sorts 20; three equal keys come out 1, 2, 0 (the short sort's swaps)");
        }
        std::string hits;
        int ticks = 0, first = 0;
        std::uint32_t w = 0;
        auto live = [&](int id, cleria::Vec3* p) { *p = {id == 7 ? 6.0f : id == 8 ? 2.0f : 4.0f, 0, 0}; return true; };
        while (!ch.dead && ticks < 200) {
            ++ticks;
            ch.tick(live, [&] { return w += 0x40000000u; }, [&](int id) { hits += std::to_string(id); if (!first) first = ticks; });
        }
        check(hits == "89789" && first == 28 && ch.dead, "the head waits 25 ticks, flies 0.85 a tick, hits once per arrival, then dies");
        x::Chain one = x::Chain::make({0, 0, 0}, 0.0f, -15.0f, 30, 0, {{3, {1, 0, 0}}}, 10.0f, 1u);
        int n1 = 0;
        for (int t = 0; t < 400 && !one.dead; ++t)
            one.tick([](int, cleria::Vec3* p) { *p = {1, 0, 0}; return true; }, [&] { return w += 0x40000000u; }, [&](int) { ++n1; });
        check(one.n == 20 && n1 == 20, "the hop count clamps to 20; a lone monster is hit on every hop");
        x::Chain gone = x::Chain::make({0, 0, 0}, 0.0f, -15.0f, 5, 0, {{3, {3, 0, 0}}}, 10.0f, 1u);
        int n2 = 0, t2 = 0;
        for (; t2 < 400 && !gone.dead; ++t2)
            gone.tick([&](int, cleria::Vec3* p) { *p = {3, 0, 0}; return n2 == 0; }, [&] { return w += 0x40000000u; }, [&](int) { ++n2; });
        check(n2 == 1 && gone.dead && t2 < 20, "the chain ends once every target is gone");
        x::Chain ray = x::Chain::make({0, 0, 0}, 0.0f, -15.0f, 5, 0, {}, 10.0f, 0x1000002u);
        int scans = 0;
        for (int t = 0; t < 40; ++t)
            if (ray.tick(live, [&] { return 0u; }, [](int) {})) ++scans;
        check(ray.ray && ray.power == -2.0f && scans == 19 && ray.dead && ray.token == 0x1000002u,
              "no monster: a ray, power -2.0, 19 scans");
        x::Chain rr = x::Chain::make({0, 0, 0}, 0.0f, -15.0f, 5, 0, {}, 10.0f, 1u);
        check(rr.ray_reaches({4.0f, 0, 0.5f}, 0.5f) && !rr.ray_reaches({6.0f, 0, 0}, 0.5f) && !rr.ray_reaches({3.0f, 0, 1.0f}, 0.5f) &&
              !rr.ray_reaches({-2.0f, 0, 0}, 0.5f) && rr.ray_reaches({-0.5f, 0, 0}, 0.5f),
              "the ray reaches 5.0 along the heading, a body's radius + 0.4 wide; not behind unless overlapping");
    }
    {   // The type-7 jagged line (GOXX_17_19.md §3.5)
        namespace x = cleria::extra;
        std::uint32_t w = 0;
        int draws = 0;
        auto rnd = [&] { ++draws; return w += 0x9e3779b9u; };
        x::Bolt::Desc d;
        d.a = {0, 0, 0}; d.b = {6, 0, 0}; d.w0 = 0.25f; d.w1 = 0.05f; d.segs = 12; d.life = 30; d.forks = 1; d.reach = 0.75f; d.fork_segs = 5;
        x::Bolt b = x::Bolt::make(d, rnd);
        float off = 0;
        for (const x::Bolt::Node& q : b.nodes) off = std::max(off, std::fabs(q.pos.y) + std::fabs(q.pos.z));
        check(b.nodes.size() == 12 && draws == 2 + 4 * 11 && b.nodes[0].pos.x == 0.0f && b.nodes[0].w == 0.25f &&
              std::fabs(b.nodes[11].w - 0.05f) < 1e-5f && off > 0.05f && off < 2.0f && !b.nodes[5].hidden && b.cd == 3,
              "a free line: 12 nodes, 2 + 4 x 11 draws, widths 0.25 -> 0.05, the nodes off the straight line");
        int forks = 0, life = 0, fork_nodes = 0;
        x::Bolt f;
        while (!b.dead && life < 100) { ++life; if (b.tick(rnd, &f)) { ++forks; fork_nodes = int(f.nodes.size()); } }
        check(life == 31 && forks == 1 && fork_nodes == 5 && f.life == 20 && f.nodes[0].w == 0.125f && f.forks == 0,
              "it lives 30 updates and throws its one side line on the 4th: 5 nodes, 20 updates, 0.125 wide");
        x::Bolt fade; fade.life = 6;
        const int a6 = fade.alpha(); fade.life = 12;
        check(std::fabs(b.scroll + 0.015f * 30) < 1e-5f && a6 == 0x7e && fade.alpha() == 0xff, "the texture scrolls 0.015 an update; alpha 0x15 x life under 12");
        d.driven = true; d.segs = 25; d.forks = 0;
        x::Bolt c = x::Bolt::make(d, rnd);
        int shown = 0;
        c.quads(false, {0, 0, 1}, 1.0f, [&](const cleria::Vec3*, const float*, const float*) { ++shown; });
        const float w3 = c.nodes[3].w;
        c.feed({1, 2, 3}, false, rnd);
        c.feed({2, 2, 3}, true, rnd);
        int dry = 0, wet = 0;
        c.quads(false, {0, 0, 1}, 1.0f, [&](const cleria::Vec3*, const float*, const float*) { ++dry; });
        c.quads(true, {0, 0, 1}, 1.0f, [&](const cleria::Vec3*, const float*, const float*) { ++wet; });
        for (int t = 0; t < 50; ++t) c.tick(rnd, &f);
        check(shown == 0 && dry == 1 && wet == 1 && c.nodes[3].w == w3 && std::fabs(c.nodes[1].pos.x - 1.0f) <= 0.76f && !c.dead,
              "a driven line starts hidden, shows what it is fed (a quad a shown node, by its water byte), keeps its widths, does not age");
        check(cleria::hitfx::charged_glow(0, 1, 0, 0x40) == 0x008050u && cleria::hitfx::charged_glow(0, 0, 0, 0x40) == 0x700010u &&
              cleria::hitfx::charged_glow(0xf0f0f0, 2, 3, 0x40) == 0xffffffu && cleria::hitfx::charged_glow(0x101010, 1, 1, 0xc0) == 0x101010u,
              "the charged glow: row x (0.5 sin + 0.5), the others' Ventus (0, 128, 80), Adol's (112, 0, 16), clamped, nothing at the trough");
    }
    {   // the water rings and wakes (water_rings.hpp, WATER_RINGS.md)
        namespace w = cleria::water;
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
        w::Rings rs;
        rs.spawn_ring({1, 2, 3}, 0, 2.5f);
        rs.step(); rs.blend(1.0f);
        int n = 0;
        cleria::Vec3 q[4]; float qu[4] = {}, qv[4] = {}; int qa = 0;
        rs.quads(0, [&](const cleria::Vec3* p, const float* u, const float* v, std::uint8_t a) {
            ++n; for (int k = 0; k < 4; ++k) { q[k] = p[k]; qu[k] = u[k]; qv[k] = v[k]; } qa = a; });
        int other = 0;
        rs.quads(1, [&](const cleria::Vec3*, const float*, const float*, std::uint8_t) { ++other; });
        check(n == 1 && other == 0 && near(rs.live()[0].scale, 1.25f) && near(q[0].x, 1.0f - 0.65f) && near(q[0].z, 3.0f - 0.65f) &&
              near(q[3].x, 1.65f) && near(q[3].z, 3.65f) && q[1].y == 2.0f && qu[2] == 1.0f && qv[1] == 0.5f && qv[3] == 0.5f && qa == 0xff,
              "a ring 2.5: half-size (1 x 0.02 + 0.5) x 1.25 after its first step, the texture's top half, opaque, pass 0 only");
        int life = 1;
        while (rs.size() && life < 100) { rs.step(); ++life; }
        check(life == 50 && w::Rings::alpha(34.0f) == 0xff && w::Rings::alpha(35.0f) == 0xff && w::Rings::alpha(40.0f) == 170 &&
              w::Rings::alpha(49.0f) == 17, "it is gone on its 50th step; opaque to 35, then (50 - age) x 17");
        w::Rings full;
        int made = 0;
        for (int i = 0; i < 300; ++i) made += full.spawn_ring({}, 0, 1.0f);
        check(made == 256 && !full.spawn_wake({}, 1, 1.0f, 0.0f), "256 nodes, then nothing");
        w::Rings wk;
        wk.spawn_wake({0, 0, 0}, 1, 2.0f, 0.0f);                    // heading 0 (along +x): i = 64, (T[64], T[0]) = (-0, 1)
        wk.step(); wk.blend(1.0f);
        n = 0;
        wk.quads(1, [&](const cleria::Vec3* p, const float*, const float* v, std::uint8_t) { ++n; for (int k = 0; k < 4; ++k) { q[k] = p[k]; qv[k] = v[k]; } });
        const float s = 0.52f;
        check(n == 1 && near(q[0].x, s) && near(q[0].z, -s) && near(q[1].x, -s) && near(q[1].z, -s) && near(q[3].x, -s) && near(q[3].z, s) &&
              qv[0] == 0.5f && qv[1] == 1.0f, "a wake along +x: the square turned a quarter, the texture's bottom half, pass = its kind");
        std::uint32_t rw = 0;
        int draws = 0;
        auto rnd = [&] { ++draws; return rw += 0x40000000u; };   // 0x40000000 (r = lo), then 0x80000000 (index 0x80)
        float dx = 0, dz = 0;
        w::ring_offset(rnd, 0.25f, 0.5f, &dx, &dz);
        check(draws == 2 && near(dx, -0.25f) && std::fabs(dz) < 1e-6f, "the ring offset: radius from the first word, the angle from the second's top byte");
        w::Follower f;
        w::Rings fr;
        draws = 0;
        std::string at;
        int steps = 0;
        auto deep = [](const cleria::Vec3& p, float* y) { *y = p.y + 2.0f; return true; };
        while (!f.dead && steps < 200) { ++steps; if (f.tick({0, 1, 0}, true, deep, rnd, fr)) at += std::to_string(steps) + " "; }
        check(steps == 112 && draws == 14 * 3 && fr.size() > 0 &&
              at == "7 15 23 31 39 47 55 63 71 79 87 95 103 111 ",
              "kind 0x19: 111 steps; a ring and a wake on step 7, then every 8th; three draws each");
        w::Follower g, h;
        draws = 0;
        auto shallow = [](const cleria::Vec3& p, float* y) { *y = p.y + 1.0f; return true; };
        for (int t = 0; t < 120; ++t) { g.tick({0, 1, 0}, false, deep, rnd, fr); h.tick({0, 1, 0}, true, shallow, rnd, fr); }
        check(draws == 0, "kind 0x19: nothing (and no draw) out of water or with the surface 1.0 or less over him");
        w::Wade pw, ew;
        w::Rings wr;
        draws = 0;
        w::WadeIn in;
        in.player = true; in.wading = true; in.surface = 5.0f;
        int pfire = 0, efire = 0;
        for (int t = 1; t <= 32; ++t) {
            in.prev = in.pos; in.pos.x += 0.1f;
            pfire += w::wade_step(pw, in, rnd, wr).fired;
        }
        w::WadeIn ein = in; ein.player = false;
        for (int t = 1; t <= 32; ++t) { ein.prev = ein.pos; efire += w::wade_step(ew, ein, rnd, wr).fired; }
        check(pfire == 8 && efire == 4 && wr.size() == 8 * 2 + 4 && draws == 12 * 2,
              "wading: the moving player every 4th step (ring + wake), another actor still every 8th (a ring)");
        w::Wade sw;
        w::Rings sr;
        w::WadeIn sin = in; sin.sand2 = true; sin.player = false;
        int sfire = 0;
        for (int t = 1; t <= 8; ++t) sfire += w::wade_step(sw, sin, rnd, sr).fired;
        w::Wade lw;
        w::WadeIn lin = in; lin.lava = true; lin.prev = lin.pos;
        for (int t = 1; t <= 8; ++t) w::wade_step(lw, lin, rnd, sr);
        check(sfire == 1 && sr.size() == 1 && sr.live()[0].kind == 1 && near(sr.live()[0].pos.y, 5.15f),
              "on sand2 the offset is drawn but nothing spawns; on lava the ring is kind 1, 0.15 over the surface");
        namespace x = cleria::extra;
        x::Runner r = x::Runner::make({0, 0.75f, 0}, 0.0f, 0.3f, 0.0f, 1.0f, 4, 3, 623, 0.0f);   // life 13
        auto floor0 = [](const cleria::Vec3& o, const cleria::Vec3& dd, cleria::Vec3* hh) {
            if (dd.y >= 0.0f || o.y < 0.0f || o.y + dd.y > 0.0f) return false;
            *hh = {o.x, 0.0f, o.z}; return true; };
        auto wet = [](const cleria::Vec3& o, const cleria::Vec3&, cleria::Vec3* hh) { *hh = {o.x, 2.0f, o.z}; return true; };
        int rt = 0, rings = 0;
        cleria::Vec3 rp{}, rr{};
        while (!r.dead && rt < 40) { ++rt; if (r.tick(floor0, wet, &rp, &rr) && r.ring_due) ++rings; }
        check(rt == 5 && rings == 1 && near(rr.y, 2.15f) && r.sub, "the runner in water: 3 life a tick, a ring at the surface + 0.15 with its pulse");
    }
    std::printf("[skill] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
