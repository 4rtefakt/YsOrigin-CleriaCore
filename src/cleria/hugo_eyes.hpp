// Hugo's Eyes of Fact as the PLAYER's familiars (cast 0xb0 = 176, CAST_M900, owner +0x854 = the player).
// Spec: build/ability_re/EYE_FAMILIARS.md (§7..§12, the 2026-10-08 trace).
//
//   FUN_0046af60 (the player tick, Hugo branch)  the spawner: keep E eyes, slots 1..k free, mines above;
//                                                and the Fire Wheel's flag-158 countdown / Fix cancel
//   FUN_00470210 (the eye's handler, owner = P)   familiar(): the chain, modes 1 (V behind Hugo) / 2 (the
//                                                wheel) / 3 (the rope), the mine fuse, the 0x89 fire effect
//   FUN_0046d8c0 (Trap Mine, item 117 / 0x75)     deploy_mines(): charged = every free eye, fuse 180, all
//                                                stacked on Hugo's spot; normal = one eye, fuse 60
//   FUN_0046ea00's 0x75 / 0x76 branches          the host (view_hugo_eyes.inl)
//   FUN_0046a3e0's Hugo branch                   the charged blast (EF2 0x93 / 0x9c) on the P+0x7d4 latch
//   FUN_0046d650                                 eye_effect_class(): an eye-owned effect / bullet's class
//
// The eye's fields live in its EnemyAi block at the engine offsets (+0x730 slot, +0x72c mode, +0x728 sub,
// +0x7c8 fuse, +0x7e4 / +0x7f0 / +0x7fc the chain points, +0x8a4 / +0x8a5 the mine bytes (one dword here),
// +0x88c / +0x890 effect handles as live flags, the host's kill tags 0 / 1).
#pragma once
#include <functional>
#include <vector>

#include "cleria/enemy_ai.hpp"

namespace cleria {

// The player side the familiars read and write (DAT_0074c09c's fields).
struct FamiliarHost {
    int player_id = 0;          // the player actor's instance id (the eyes' +0x854)
    Vec3 origin{};              // P+0x50..+0x58
    float heading = 0;          // P+0x308 (engine basis: (cos, sin))
    float facing = 0;           // P+0x314
    float hp = 1;               // P+0x98
    int level = 1;              // P+0xc8
    bool boosting = false;      // DAT_0076c198 & 0x20
    bool extra = false;         // DAT_0074c0d8, Hugo's Extra skin [not modelled: always 0]
    bool culled = false;        // FUN_0045d3c0 for this eye (set per eye by the host)
    Vec3 mine_point{};          // P+0x7c8..+0x7d0: where a charged Trap Mine was laid
    int latch_7d4 = 0;          // P+0x7d4: 1 = a charged mine expired -> the player's blast
    bool fx_7d8 = false;        // P+0x7d8: the 0x94 effect laid with a charged mine is live
};

namespace hugo {

// The eye count (FUN_0046af60 / FUN_00470210 / FUN_0046ea00): flag 211 -> 0, flag 210 -> 6, Boost -> 4, else 2.
int eye_count(int flag211, int flag210, bool boosting);
// FUN_0046d650: an eye-owned effect / bullet's damage class (BULLETS.md §1.3), -1 for none.
int eye_effect_class(int effect);

// +0x8a4 (deployed as a mine) / +0x8a5 (a charged mine): two bytes, one dword in EnemyAi.
bool b8a4(const EnemyAi& a);
bool b8a5(const EnemyAi& a);
void set_b8a4(EnemyAi& a, bool v);
void set_b8a5(EnemyAi& a, bool v);

// An eye of this owner: cast 0xb0 and +0x854 == the player.
bool owned_eye(const Actor& a, int player_id);

// FUN_00470210 for a player-owned eye, one tick (the m900 brain calls it from the handler's branch 3c).
// Reads the host through env.familiar; writes the eye's position / alpha / fields, and returns the section,
// effect (0x89 the wheel's fire, kill tags 0 = +0x88c, 1 = +0x890) and HP requests.
AiOut familiar(EnemyAi& a, const EnemyAiParams& p, const AiEnv& env);

// FUN_0046d8c0(charged, P): deploy the free eyes as mines. Returns the eyes deployed (0 = none: the
// caller then detonates or refuses). The host applies `events`: the effects 0x94 (charged, at the
// mine point; its handle is P+0x7d8) and 0x92 (at each eye's old position), and section 0x10f on each
// deployed eye (FUN_00446550 on the eye: priority-gated).
struct DeployEvent {
    enum Kind { kEffect, kSection } kind = kEffect;
    int id = 0;                 // the effect id (0x92 / 0x94) or the section (0x10f)
    Vec3 pos{};                 // the effect's position
    int eye = 0;                // the eye's instance id (kSection)
};
// section_of(eye) = its live ASO section (+0x994).
int deploy_mines(bool charged, FamiliarHost& h, AiWorld& w, const std::function<int(const Actor&)>& section_of,
                 std::vector<DeployEvent>& events);

}  // namespace hugo
}  // namespace cleria
