// Headless tests for src/cleria/hit.hpp — the hit-resolution rules, both directions.
// Expectations are the disassembly's semantics (addresses cited in hit.hpp), including
// the cases where CleriaCore used to differ: the min-1 chip, the resist multiplier,
// i-frames frozen through the hurt clip, and the facing-driven knock direction.
#include <cmath>
#include <cstdio>

#include "cleria/hit.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[hit] %-58s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
    const auto never = [](float) { return false; };
    const auto always = [](float) { return true; };

    // --- player -> enemy (FUN_004fa550) --------------------------------------------
    AttackStats a; a.atk = 48; a.bonus = 8;      // level-1 Yunica-ish, basic swing power
    EnemyDefense v; v.def = 40;
    const PlayerDamageMods none;
    // raw = 48^0.66 * (48 - 39.2 + 10 + 8) / 22.92 = 15.05
    check(enemy_incoming_damage(a, kDamageClassMelee, v, none, never) == 15, "raw formula, round down");
    check(enemy_incoming_damage(a, kDamageClassMelee, v, none, always) == 16, "raw formula, round up");
    v.resist[3] = 0.5f;                          // melee slot = +0xc0
    check(enemy_incoming_damage(a, kDamageClassMelee, v, none, never) == 7, "resist is a (1-r) multiplier");
    v.resist[3] = -0.5f;
    check(enemy_incoming_damage(a, kDamageClassMelee, v, none, never) == 22, "negative resist = weakness");
    v.resist[3] = 0; v.resist[0] = 0.5f;         // class 4 folds onto slot 0
    check(enemy_incoming_damage(a, 4, v, none, never) == 7, "charged class 4 uses slot 0");
    v.resist[0] = 0;

    AttackStats weak; weak.atk = 10;             // outclassed attacker
    EnemyDefense tank; tank.def = 200;
    check(enemy_incoming_damage(weak, kDamageClassMelee, tank, none, never) == 1, "min-1 chip (damage_real gives 0)");
    tank.def = 999;
    check(enemy_incoming_damage(weak, kDamageClassMelee, tank, none, never) == 0, "DEF >= 999 disables min-1");
    tank.def = 200; tank.resist[3] = 1.0f;
    check(enemy_incoming_damage(weak, kDamageClassMelee, tank, none, never) == 0, "immune (resist 1) disables min-1");
    tank.resist[3] = 0; tank.no_floor = true;
    check(enemy_incoming_damage(weak, kDamageClassMelee, tank, none, never) == 0, "+0xce disables min-1");

    AttackStats floored = weak; floored.min_dmg = 3.0f;
    EnemyDefense t2; t2.def = 200;
    check(enemy_incoming_damage(floored, kDamageClassMelee, t2, none, never) == 3, "+0x100 floor applies");

    PlayerDamageMods yb; yb.yunica_bonus = true;
    check(enemy_incoming_damage(a, kDamageClassMelee, v, yb, never) == 16, "yunica x1.1 after rounding (15 -> 16.5 -> 16)");
    PlayerDamageMods dbl; dbl.double_damage = true;
    check(enemy_incoming_damage(a, kDamageClassMelee, v, dbl, never) == 30, "g_flags[210] doubles");
    PlayerDamageMods deb; deb.atk_debuff = true;
    const float raw43 = std::pow(43.0f, 0.66f) * (43 - 39.2f + 10 + 8) / 22.92f;
    check(enemy_incoming_damage(a, kDamageClassMelee, v, deb, never) == int(std::floor(raw43)), "ATK debuff -5");

    // --- enemy -> player follow-ups (FUN_004fd350 player branch) ---------------------
    check(player_damage_after_boost(9, true) == 4 && player_damage_after_boost(9, false) == 9,
          "Boost halves incoming, truncating");

    PlayerReaction r = player_reaction(50, 100, 0, false, false, 0);
    check(r.motion_state == 9 && r.iframes == 16 && near(r.knockback, 1.3f) && r.face_attacker,
          "dmg >= maxHP/2 -> knockdown, 16f, 1.3 impulse, face");
    r = player_reaction(49, 100, 0, false, false, 0);
    check(r.motion_state == 0xb && r.iframes == 12 && near(r.knockback, 0.1f), "dmg >= maxHP/5 -> stagger, 12f");
    r = player_reaction(20, 100, 0, false, false, 0);
    check(r.motion_state == 0xb, "maxHP/5 boundary is inclusive");
    r = player_reaction(19, 100, 0, false, false, 0);
    check(r.motion_state == 4 && r.iframes == 8, "small -> flinch, 8f");
    r = player_reaction(1, 100, 2, false, false, 0);
    check(r.motion_state == 9 && r.iframes == 16, "rtype 2 forces knockdown");
    r = player_reaction(1, 100, 1, false, false, 0);
    check(r.motion_state == 0xb && r.iframes == 12, "rtype 1 forces stagger");
    r = player_reaction(60, 100, 1, false, false, 0);
    check(r.motion_state == 9, "big threshold is checked before rtype 1");
    r = player_reaction(5, 100, 10, false, false, 0);
    check(r.motion_state == 4 && r.iframes == 0, "rtype 10 flinches, grants nothing");
    r = player_reaction(5, 100, 0, false, true, 0);
    check(r.motion_state == -1 && r.iframes == 16, "durable: no flinch, 16f");
    r = player_reaction(5, 100, 0, true, false, 0);
    check(r.motion_state == 5, "lethal -> death state 5");
    r = player_reaction(0, 100, 0, false, false, 0);
    check(r.motion_state == -1 && near(r.knockback, 0.1f), "0 damage still knocks back");
    r = player_reaction(80, 100, 0, false, false, 9);
    check(near(r.knockback, 1.2f), "already knocked down: base impulse 0, +1.2");
    check(near(player_reaction(7, 100, 0, false, false, 0).boost_gain, 700.0f), "Boost gauge += dmg*100");

    std::uint32_t f = 3;
    tick_iframes(f, 4);  tick_iframes(f, 9);  tick_iframes(f, 0xb);
    check(f == 3, "i-frames frozen in hurt states 4/9/0xb");
    tick_iframes(f, 0);
    check(f == 2, "i-frames count down otherwise");

    // --- enemy reaction (FUN_004fbb40) --------------------------------------------
    check(enemy_flinch_state(5, false, 0, false, 0) == 4, "enemy flinch on a real hit");
    check(enemy_flinch_state(0, false, 0, false, 0) == -1, "0 damage: no flinch (clank)");
    check(enemy_flinch_state(5, false, 3, false, 0) == -1, "rtype 3: no flinch");
    check(enemy_flinch_state(5, false, 0, true, 0) == -1, "durable: no flinch");
    check(enemy_flinch_state(5, true, 0, false, 0) == 5, "lethal: death state");

    // --- knockback ------------------------------------------------------------------
    check(near(player_knock_scale(0.056f, false, 2.2f), 0.0952f) &&
          near(player_knock_scale(0.056f, true, 2.2f), 0.20944f), "player L = walk*1.7 (*run)");
    // Facing +X (engine yaw 0); a victim off to the diagonal is still driven along +X.
    float ang = player_knock_angle(0.0f, 0, 0, 0, 1, 1, 0);
    check(std::fabs(ang) < 1e-3f, "knock follows facing, not the radial (45deg) line");
    ang = player_knock_angle(1.5707963f, 0, 0, 0, 1, 1, 0);
    check(std::fabs(ang - 1.5707963f) < 0.03f, "facing +Z drives along +Z (1/256-turn quantised)");
    Impulse imp = knock_impulse(0.0f, 0.5f);
    check(near(imp.x, 0.5f) && near(imp.z, 0.0f), "impulse = (cos, sin) * K");

    Impulse vel{1.0f, 0.0f};
    Impulse d = step_knockback(vel, true, false);
    check(near(d.x, 0.36f) && near(vel.x, 1.0f - 0.135f * 0.95f), "displacement capped 0.36, damped 0.872");
    vel = {1.0f, 0.0f};
    step_knockback(vel, false, false);
    check(near(vel.x, 1.0f - 0.10125f * 0.95f), "airborne friction x0.75");
    vel = {0.01f, 0.0f};
    step_knockback(vel, true, false);
    check(vel.x == 0.0f, "stops below |v|^2 0.000225");
    vel = {0.2f, 0.0f};
    step_knockback(vel, true, true);
    check(vel.x == 0.0f, "dead actors stop sliding");
    vel = {1.0f, 0.0f};
    step_knockback(vel, true, false, Mover::Player);
    check(near(vel.x, 0.865f), "player mover: v *= 1 - 0.135");
    vel = {1.0f, 0.0f};
    step_knockback(vel, false, false, Mover::Player);
    check(near(vel.x, 1.0f - 0.0405f), "player mover airborne: friction x0.3");

    {   // Guard (FUN_004fbb40 0x4fbf77): section 6 / +0x399, rtype <= 1, +/-75 deg.
        // Attacker faces +X (angle 0); a victim facing back at it (pi) is square on.
        const float pi = 3.14159265f;
        check(guard_blocks(22, true, false, 1, pi, 0.0f), "guard: facing the attacker blocks (rtype 1)");
        check(guard_blocks(22, true, false, 0, pi + 1.2f, 0.0f), "guard: 69 deg off still inside the 150 arc");
        check(!guard_blocks(22, true, false, 1, pi + 1.4f, 0.0f), "guard: 80 deg off is outside +/-75");
        check(!guard_blocks(22, true, false, 2, pi, 0.0f), "guard: rtype 2 is never blocked");
        check(!guard_blocks(22, false, false, 1, pi, 0.0f), "not guarding: no block");
        check(guard_blocks(0xa7, true, false, 3, 0.1f, 0.0f), "cast 0xa7: blocks any rtype from (nearly) behind (arc 360)");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
