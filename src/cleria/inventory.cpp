// Inventory, equipment, item use, pickups and the player stat block:
// build/progress_re/INVENTORY.md. See inventory.hpp.
#include "cleria/inventory.hpp"

#include <algorithm>
#include <cmath>

#include "cleria/gameinfo.hpp"
#include "cleria/items.hpp"

namespace cleria {

namespace {

constexpr int kCountCap = 99;   // capTable 0x69a3d8 = {99, 99, 99}

// Equip grids (0x68ed70 [char], 22 records, +4 itemId) — INVENTORY.md §3.2.
const std::array<std::array<std::int32_t, 22>, 4> kEquipGrid = {{
    {3, 24, 25, 26, 27, 28, 29, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 116, 117, 118},   // Adol
    {0, 6, 7, 8, 9, 10, 11, 30, 31, 32, 33, 34, 35, 54, 55, 58, 56, 57, 62, 116, 117, 118},      // Yunica
    {1, 12, 13, 14, 15, 16, 17, 36, 37, 38, 39, 40, 41, 54, 55, 58, 56, 57, 63, 116, 117, 118},  // Hugo
    {2, 18, 19, 20, 21, 22, 23, 42, 43, 44, 45, 46, 47, 60, 61, 58, 56, 57, 59, 116, 117, 118},  // Toal
}};

// Item page layouts (0x68ed58 [char], 30 records, +4 id; 0xff = empty) — §7.
constexpr std::array<std::int32_t, 30> kItemYunica = {
    84, 85, 89, 97, 95, 98,  78, 79, 80, 81, 82, 83,  99, 100, 101, 102, 103, 110,
    107, 108, 109, 90, 91, 94,  87, 88, 111, 92, 106, 105};
constexpr std::array<std::int32_t, 30> kItemHugo = {
    84, 85, 89, 97, 96, 98,  78, 79, 80, 81, 82, 83,  99, 100, 101, 102, 103, 110,
    107, 108, 109, 90, 91, 94,  87, 88, 111, 92, 106, -1};
constexpr std::array<std::int32_t, 30> kItemToal = {
    86, 85, 89, 97, 95, 98,  78, 79, 80, 81, 82, 83,  99, 100, 101, 102, 103, 110,
    107, 108, 109, -1, 91, 94,  87, 88, 111, 92, -1, -1};

}  // namespace

const std::array<std::int32_t, 22>& equip_grid(int character) {
    return kEquipGrid[std::size_t(std::clamp(character, 0, 3))];
}

const std::array<std::int32_t, 30>& item_grid(int character) {
    if (character == 2) return kItemHugo;
    if (character == 3) return kItemToal;
    return kItemYunica;                                  // Yunica and Adol share 0x702310
}

void inv_new_game(InvState& inv) {
    inv = InvState{};                                    // slots -1, tiers / raval 0, buffs 0
}

void inv_sanitize(std::int32_t* flags, InvState& inv) {
    for (int i = 0; i < 128; ++i) flags[i] = std::min(flags[i], kCountCap);
    for (int& t : inv.tier) t = std::min(t, 10);
    for (int& r : inv.raval) r = std::min(r, 1);
}

PlayerStats compute_stats(const std::int32_t* flags, InvState& inv, const GameInfo& gi,
                          const ItemTable& items, int character, int level) {
    const int ch = std::clamp(character, 0, 3);
    PlayerStats s;
    // §4.2 base curves: floorf(lerp) + bonus / buff, then the per-character offset.
    s.max_hp = std::floor(gi.curves[kPlayerMaxHp].at(level)) + inv.hp_bonus;
    s.atk = std::floor(gi.curves[kPlayerAtk].at(level)) + inv.atk_buff;
    s.def = std::floor(gi.curves[kPlayerDef].at(level)) + inv.def_buff;
    s.luck = inv.luck_base * 3.0f + 0.0f;
    static constexpr float kChAtk[4] = {5, 0, -1, 2}, kChDef[4] = {5, 0, -2, 1};
    s.atk += kChAtk[ch];
    s.def += kChDef[ch];
    // §4.3 equipment (FUN_004208f0).
    float mod_atk = 0, mod_def = 0;
    std::uint32_t fl = 0;
    const std::int32_t weapon = flags[151], armor = flags[152], foot = flags[153];
    auto mag = [&](int id) {
        const Item* it = items.get(id);
        return it ? float(std::int32_t(it->magnitude)) : 0.0f;
    };
    auto rv = [&](int id) { return (id >= 0 && id < 54) ? float(inv.raval[id]) : 0.0f; };
    if (weapon != -1) mod_atk = 2.0f * rv(weapon) + gi.weapon_atk[ch][std::clamp(inv.tier[ch], 0, 10)];
    if (armor != -1) mod_def = rv(armor) + mag(armor);
    if (foot != -1) {
        mod_def += rv(foot) + mag(foot);
        switch (foot) {
            case 0x1f: case 0x25: case 0x2b: case 0x31: fl = 0x1000000; break;
            case 0x20: case 0x26: case 0x2c: case 0x32: fl = 0x2000; break;
            case 0x21: case 0x27: case 0x2d: case 0x33: fl = 0x2000000; break;
            case 0x22: case 0x28: case 0x2e: case 0x34: fl = 0x4000000; break;
            case 0x23: case 0x29: case 0x2f: case 0x35: fl = 0x200; break;
            default: break;
        }
    }
    switch (inv.accessory) {                              // acc_count is always 1
        case 0x36: fl |= 0x10000; break;                  // Mask of Eyes
        case 0x37: fl |= 0x20000; break;                  // Blue Necklace
        case 0x38: fl |= 0x8000; break;                   // Silver Chimes
        case 0x39: fl |= 0x4000; break;                   // Earth Dragon's Claws
        case 0x3a: fl |= 0x400; break;                    // Spirit Cape
        case 0x3b: fl |= 0x248400; mod_atk += 5; mod_def += 5; break;   // Goddess Ring
        case 0x3c: mod_atk += 3; mod_def -= 3; break;     // Cleria Ring
        case 0x3d: fl |= 0x200000; break;                 // Ring of Ease
        case 0x3e: case 0x3f: fl |= 0x1000; break;        // Necklace / Talisman (auto-revive)
        default: break;
    }
    inv.equip_flags = fl;
    s.max_hp = std::min(999.0f, s.max_hp * 1.0f);
    s.atk += mod_atk;
    s.def += mod_def;
    return s;
}

bool inv_is_equipped(const std::int32_t* flags, const InvState& inv, int id) {
    if (id >= 0 && id <= 5) return flags[151] == id;
    if (id >= 6 && id <= 29) return flags[152] == id;
    if (id >= 30 && id <= 53) return flags[153] == id;
    if (unsigned(id - 0x36) < 6u) return inv.accessory == id;   // 54..59 only (60..65: false)
    if (id >= 66 && id <= 115) return inv.held == id;
    return false;
}

bool equip_cell(std::int32_t* flags, InvState& inv, int character, int cell) {
    if (cell < 0 || cell >= 22) return false;
    const std::int32_t id = equip_grid(character)[std::size_t(cell)];
    if (flags[id] < 1) return false;                     // not owned / used up: buzzer
    if (id == inv.accessory) { inv.accessory = 0xff; return true; }   // re-select = unequip
    if (cell == 0) flags[151] = id;
    else if (cell <= 6) flags[152] = id;
    else if (cell <= 12) flags[153] = id;
    else if (cell <= 18) inv.accessory = id;
    else inv.magic = id;
    return true;
}

int apply_item_effect(InvState& inv, const ItemTable& items, EffectEnv& env, int id) {
    const std::uint32_t bless = env.flags ? std::uint32_t(env.flags[217]) : 0u;
    if (id >= 0x42 && id <= 0x47) {                      // Recovery Drop 1..6
        const Item* it = items.get(id);
        const float m = it ? float(it->magnitude) : 0.0f;
        int amt = int(m);
        if (bless & 0x1000) amt = int(std::floor(m * 1.25f));
        if (bless & 0x20000) amt = int(std::floor(m * 1.5f));
        if (env.mod) {                                   // CleriaCore mods
            PickupFx fx{id, 1, float(amt), 0};
            if (!env.mod(fx)) return 0;
            amt = int(fx.value);
        }
        if (env.hp && env.player_alive && *env.hp >= 1.0f)
            *env.hp = std::clamp(*env.hp + float(amt), 0.0f, env.max_hp);
        return amt;
    }
    if (id == 0x59) {                                    // Celcetan Panacea
        const int f143 = env.flags ? env.flags[143] : 0, f144 = env.flags ? env.flags[144] : 0;
        static constexpr int kPan[3] = {3, 2, 1};
        inv.hp_bonus += f143 == 0 ? float(kPan[std::clamp(f144, 0, 2)]) : 10.0f;
        return 0;
    }
    if (id == 0x72) {                                    // Vitality Seed
        PickupFx fx{id, 5, 30000.0f, 0};
        if (env.mod && !env.mod(fx)) return 0;           // CleriaCore mods
        if (env.flags && env.flags[187] != 0 && env.boost) *env.boost = fx.value;
        return 0;
    }
    if (id == 0x73) {                                    // Air Seed
        PickupFx fx{id, 6, 6000.0f, 0};
        if (env.mod && !env.mod(fx)) return 0;           // CleriaCore mods
        if (env.oxygen) *env.oxygen = fx.value;
        return 0;
    }
    return 0;                                            // "now making": no effect
}

UseResult use_item(InvState& inv, const ItemTable& items, EffectEnv& env, int id, std::string* label) {
    if (!env.flags || id < 0 || id >= 128 || env.flags[id] < 1) return UseResult::NotUsable;
    inv.held = id;                                       // selecting an owned item holds it
    const char* script = nullptr;
    switch (id) {
        case 0x54: case 0x56: script = "@CheckWing"; break;
        case 0x55: script = "@Riranokaigara"; break;
        case 0x70: script = "@DarkRiranokaigara"; break;
        case 0x5f: script = "@UseHarmonica"; break;
        case 0x60: script = "@UseHammer"; break;
        case 0x69: script = "@UseSekizou"; break;
        case 0x59:
            apply_item_effect(inv, items, env, id);
            env.flags[id] -= 1;
            return UseResult::Consumed;
        default: return UseResult::NotUsable;
    }
    if (label) *label = script;
    return UseResult::RunScript;
}

PickupResult pickup(InvState& inv, const ItemTable& items, EffectEnv& env, int id, int* popup) {
    if (popup) *popup = 0;
    if (id >= 0x78 && id <= 0x7f) {                      // gold -> the SP wallet
        const Item* it = items.get(id);
        int n = std::max(1, int(std::floor(float(it ? it->magnitude : 0) * env.sp_rate * env.gold_scale)));
        if (inv.sp >= 999999) return PickupResult::LeftInWorld;
        if (env.mod) {                                   // CleriaCore mods
            PickupFx fx{id, 0, float(n), 0};
            if (!env.mod(fx)) return PickupResult::Taken;
            n = int(fx.value);
        }
        inv.sp = std::min(999999, inv.sp + n);
        if (popup) *popup = n;
        return PickupResult::Taken;
    }
    if ((id >= 0x42 && id <= 0x47) || id == 0x72 || id == 0x73) {
        const int amt = apply_item_effect(inv, items, env, id);
        if (popup) *popup = amt;
        return PickupResult::Taken;
    }
    if (id >= 0x48 && id <= 0x4d) {                      // Strength / Defense / MP Drop (§4.5)
        const float lv = ((id - 0x48) & 1) ? 2.0f : 1.0f;
        const std::uint32_t bl = env.flags ? std::uint32_t(env.flags[217]) : 0u;
        const int timer = (bl & 0x800000) ? 2200 : (bl & 0x10) ? 2000 : 1800;
        PickupFx fx{id, id <= 0x49 ? 2 : id <= 0x4b ? 3 : 4, (id <= 0x49 ? 0.5f : id <= 0x4b ? 0.3f : 0.2f) * lv, timer};
        if (env.mod && !env.mod(fx)) return PickupResult::Taken;   // CleriaCore mods
        if (id <= 0x49) {
            if (inv.atk_buff < 5.0f) inv.atk_buff = std::min(5.0f, inv.atk_buff + fx.value);
            inv.timer_atk = fx.timer;
        } else if (id <= 0x4b) {
            if (inv.def_buff < 3.0f) inv.def_buff = std::min(3.0f, inv.def_buff + fx.value);
            inv.timer_def = fx.timer;
        } else {                                         // FUN_00463f40 bit 0x10 (PICKUPS.md §7.3)
            if (inv.mp_buff == 0.0f) inv.mp_buff = 1.0f + fx.value;
            else if (inv.mp_buff < 3.0f) inv.mp_buff += fx.value;
            inv.mp_buff = std::min(3.0f, inv.mp_buff);
            inv.timer_mp = fx.timer;
        }
        return PickupResult::Taken;
    }
    if (!env.flags || id < 0 || id >= 128) return PickupResult::LeftInWorld;
    const std::int32_t c = env.flags[id];
    if (c >= kCountCap) return PickupResult::LeftInWorld;
    if (env.mod) {                                       // CleriaCore mods
        PickupFx fx{id, 7, 1.0f, 0};
        if (!env.mod(fx)) return PickupResult::Taken;
    }
    env.flags[id] = (c == -1) ? 1 : c + 1;
    return PickupResult::Taken;
}

}  // namespace cleria
