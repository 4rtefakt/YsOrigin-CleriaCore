// The revive handler family (build/ai_spec/REVIVE.md): M430 Zeruena FUN_004b3970, M560 Karon
// FUN_004a12a0, M570 Zarue FUN_004f4690, M670 Bazeraena FUN_004f24c0, M780 Zelkaron FUN_004ab280.
// All five are the M100 handler shape with a replaced death path: without Silver Chimes
// (DAT_0076a624 & 0x8000) at the first dead tick the corpse never fades or despawns, and on the
// 600th dead tick the actor is rebuilt in place (phase 4, section 10). Pure: the host (the view)
// supplies the world facts and does the section requests, the VM step, the sound and the new id.
#pragma once
#include "cleria/enemy_ai.hpp"

namespace cleria {
struct Actor;

namespace revive {

struct Row {
    int ai_index;
    int cast;
    bool skeleton;    // M560 / M780: the sand clamp, the veto, latch +0x7a4, the +0x8a5 gate
    int latch;        // the "stays dead" latch: +0x7a4 (skeletons) / +0x7c4 (summoners)
    bool m560, m670, m780;
};
// The family member for an AI index (REVIVE.md §1), or nullptr.
const Row* row(int ai_index);

// REVIVE.md §5.3 (M560 0x4a12c0, M780 0x4ab2a0): the sand clamp on the ORIGIN y, every gated tick
// before the HP test. Reads +0x4ba (surf_ray), +0x320 (water_y), +0x7a0, +0x7a4, HP, +0x388.
void sand_clamp(const Row& r, EnemyAi& a, float& origin_y, float hp, bool grounded, bool in_4005);

struct World {
    bool chimes = false;           // DAT_0076a624 & 0x8000 (Silver Chimes; Goddess Ring too)
    bool in_4005 = false;          // FUN_004358b0("s_40\s_4005\") is the current map (M560's rules)
    int flag179 = 0;               // DAT_0076bbe8 = g_flags[179]
    int* flag180 = nullptr;        // DAT_0076bbec = g_flags[180] (M560 counts its deaths there)
    int* flag265 = nullptr;        // DAT_0076bd40 = g_flags[265] (M780's Chimes deaths)
};

// The dead path up to the VM step (REVIVE.md §3.1 + FUN_004780e0 §3.2). `first`: dead tick 1 (the
// host plays SE 92 unless +0x130 & 0x1000, requests the death section 5; the death-effect block and
// the drop loop are gated by +0x130 & 8 / & 1). `remove`: the destroy request (a latched corpse at
// +0x3fc >= 0x100). The draw alpha lands in a.alpha.
struct DeadPre { bool first = false, remove = false; };
DeadPre dead_pre(const Row& r, Actor& A, const World& w);
// After the host's VM step (FUN_004472e0 on the corpse): the no-latch block and, on the 600th dead
// tick, the REVIVE (§3.5): HP full, phase 4, unhittable; the host then makes the GATED request of
// section 10 and gives the actor a new instance id (FUN_00434db0). `corpse_mover`: M560 in
// s_40\s_4005\ runs FUN_00478080 on the corpse.
struct DeadPost { bool revived = false, corpse_mover = false; };
DeadPost dead_post(const Row& r, Actor& A, const float ei_raw[16]);

// §5.1, every gated tick (alive, frozen or dead): without the Chimes the EXP base +0xb0 is zeroed;
// with them the stats are re-made at the ENEMYINFO default level, keeping the HP (here: the spawn's
// EXP is restored — the spawn already used that level).
void exp_block(const Row& r, Actor& A, bool chimes);

// FUN_00434db0(cast): the new instance id over the live actor ids (REVIVE.md §3.7): the cast id if
// no actor holds it, else the lowest free id >= 0xba.
template <class Taken> int new_instance_id(int cast, Taken taken) {
    if (cast != 0 && !taken(cast)) return cast;
    for (int i = 0xba;; ++i) if (!taken(i)) return i;
}

}  // namespace revive
}  // namespace cleria
