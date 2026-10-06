// Inventory, equipment, item use, pickups and the player stat block, ported from
// build/progress_re/INVENTORY.md (FUN_00420c40 / FUN_004208f0 recompute, FUN_005a8030 equip,
// FUN_005abd10 / FUN_00443390 use, FUN_00585ff0 pickup).
//
// Ownership lives in the game flags: g_flags[0..127] = count[id] (-1 never had, 0 used up, n
// held), g_flags[151..153] = weapon / armor / foot, g_flags[217] = the blessing bits. The rest of
// the save-block fields are in InvState. Functions take the flag array so there is one copy.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace cleria {

struct GameInfo;
struct ItemTable;

struct InvState {
    std::int32_t accessory = -1;          // 0x76a628 (one slot; 0xff also means empty)
    std::int32_t held = -1;               // 0x76a62c: the Item page's selected tool
    std::int32_t magic = -1;              // 0x76a630: the bracelet 116..118
    std::int32_t tier[4] = {0, 0, 0, 0};  // 0x76a634 + 8ch: weapon tier 0..10 per character
    std::int32_t raval[54] = {};          // 0x76a654: per-gear "strengthened" (0/1)
    float hp_bonus = 0;                   // 0x76b8fc: Panacea max-HP bonus
    float atk_buff = 0, def_buff = 0;     // 0x76b904 / 0x76b908: the Strength / Defense Drops
    float luck_base = 0;                  // 0x76b90c (no writer in code)
    std::int32_t timer_atk = 0, timer_def = 0;   // 0x76c19c / 0x76c1a0
    float mp_buff = 0;                    // 0x76b900: the MP Drop buff (0 off, else 1.0..3.0)
    std::int32_t timer_mp = 0;            // 0x76c1a4
    std::int32_t sp = 0;                  // 0x76a75c: the SP wallet (gold adds here), 0..999999
    std::uint32_t equip_flags = 0;        // 0x76a624: derived ability bits
};

struct PlayerStats {
    float max_hp = 0, atk = 0, def = 0, luck = 0;
    float g2_max = 3000;                  // 0x76a734
};

// The 22-cell Equip grid per character (table 0x68ed70): cell -> item id. Cells 0 weapon,
// 1..6 armor, 7..12 foot, 13..18 accessory, 19..21 magic.
const std::array<std::int32_t, 22>& equip_grid(int character);
// The 30-cell Item page layout per character (table 0x68ed58): cell -> item id, -1 empty.
const std::array<std::int32_t, 30>& item_grid(int character);

// The non-flag new-game writes of FUN_0041fd10 (the flag writes are XsoVm::new_game).
void inv_new_game(InvState& inv);
// FUN_00431840 after a load: counts <= 99, tiers <= 10, raval <= 1.
void inv_sanitize(std::int32_t* flags, InvState& inv);

// FUN_00420c40 bits 0x04 + 0x20: max HP / ATK / DEF from the level curves, the per-character
// offset, the buffs, the equipment and the two rings; writes inv.equip_flags.
PlayerStats compute_stats(const std::int32_t* flags, InvState& inv, const GameInfo& gi,
                          const ItemTable& items, int character, int level);

// FUN_00443ce0 (the 60..65 quirk kept: those ids always answer false).
bool inv_is_equipped(const std::int32_t* flags, const InvState& inv, int id);

// FUN_005a8030 confirm on an Equip cell. False = refused (not owned): the buzzer.
bool equip_cell(std::int32_t* flags, InvState& inv, int character, int cell);

// FUN_00443390: the effect of a heal drop / Panacea / seed on the player. `hp` / `max_hp` are
// the live HP; returns the popup value (heal amount), or 0.
struct EffectEnv {
    std::int32_t* flags = nullptr;
    float* hp = nullptr;
    float max_hp = 0;
    bool player_alive = true;
    float* boost = nullptr, * oxygen = nullptr;
    float sp_rate = 1.0f;   // the SP rate 0x76a5f8 (1.0 in retail; CleriaCore's SP cheat, access.hpp)
};
int apply_item_effect(InvState& inv, const ItemTable& items, EffectEnv& env, int id);

// FUN_005abd10 Use on the Item page: Panacea is consumed here; key items run a script.
enum class UseResult { Consumed, RunScript, NotUsable };
UseResult use_item(InvState& inv, const ItemTable& items, EffectEnv& env, int id, std::string* label);

// FUN_00585ff0 touch: gold -> SP, heal drops / seeds -> the effect, buff drops -> the buff,
// anything else -> count + 1 (capped at 99). LeftInWorld = refused (the pickup stays).
enum class PickupResult { Taken, LeftInWorld };
PickupResult pickup(InvState& inv, const ItemTable& items, EffectEnv& env, int id, int* popup);

}  // namespace cleria
