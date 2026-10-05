// cleria-invtest — the inventory / equipment / stat model (src/cleria/inventory.hpp) against
// build/progress_re/INVENTORY.md: the stat formula reproduced on three real saves (§4.4), the
// equip rules (§3.3), the pickup branches (§6.3) and item use (§5).
// Usage: cleria-invtest <assets dir>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "cleria/gameinfo.hpp"
#include "cleria/inventory.hpp"
#include "cleria/items.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[inv] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

// The tier whose weapon ATK is `atk` for this character (the saves' tier is not in the
// spec's table, only its value).
static int tier_of(const GameInfo& gi, int ch, float atk) {
    for (int t = 0; t <= 10; ++t) if (gi.weapon_atk[ch][t] == atk) return t;
    return -1;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-invtest <assets>\n"); return 2; }
    const std::filesystem::path root = argv[1];
    const GameInfo gi = GameInfo::load(root / "MISC" / "GAMEINFO.DAT");
    const ItemTable items = ItemTable::load(root / "MISC" / "INVINFO.DAT");
    if (!gi.ok() || items.items.size() < 128) { std::fprintf(stderr, "tables missing\n"); return 2; }

    {   // yso_40: Yunica L1, Battle Axe tier 0, Leather Armor 6 -> ATK 46, DEF 38, HP 60.
        std::int32_t f[0x200]; for (int& v : f) v = -1;
        f[151] = 0; f[152] = 6; f[153] = -1; f[0] = 1; f[6] = 1;
        InvState inv;
        const PlayerStats s = compute_stats(f, inv, gi, items, 1, 1);
        std::printf("[inv]   Yunica L1: HP %.1f ATK %.1f DEF %.1f\n", s.max_hp, s.atk, s.def);
        check(s.max_hp == 60.0f && s.atk == 46.0f && s.def == 38.0f, "yso_40 Yunica L1: HP 60, ATK 46, DEF 38");
    }
    {   // yso_02: Hugo L6, armor 12 strengthened, hpBonus 2, weapon ATK 29 -> ATK 69, DEF 52, HP 90.
        std::int32_t f[0x200]; for (int& v : f) v = -1;
        f[151] = 1; f[152] = 12; f[153] = -1;
        InvState inv; inv.raval[12] = 1; inv.hp_bonus = 2; inv.tier[2] = tier_of(gi, 2, 29.0f);
        const PlayerStats s = compute_stats(f, inv, gi, items, 2, 6);
        std::printf("[inv]   Hugo L6 (tier %d): HP %.1f ATK %.1f DEF %.1f\n", inv.tier[2], s.max_hp, s.atk, s.def);
        check(inv.tier[2] >= 0 && s.max_hp == 90.0f && s.atk == 69.0f && s.def == 52.0f,
              "yso_02 Hugo L6: HP 90, ATK 69, DEF 52 (raval +1, char offset -1/-2)");
    }
    {   // yso_19: Toal L11, Cleria Ring, foot 42, buffs 5.0 / 0.6, hpBonus 3 -> ATK 94, DEF 72.6, HP 120.
        std::int32_t f[0x200]; for (int& v : f) v = -1;
        f[151] = 2; f[152] = 18; f[153] = 42;
        InvState inv; inv.accessory = 60; inv.atk_buff = 5.0f; inv.def_buff = 0.6f; inv.hp_bonus = 3;
        inv.tier[3] = tier_of(gi, 3, 27.0f);
        const PlayerStats s = compute_stats(f, inv, gi, items, 3, 11);
        std::printf("[inv]   Toal L11 (tier %d): HP %.1f ATK %.1f DEF %.2f\n", inv.tier[3], s.max_hp, s.atk, s.def);
        check(inv.tier[3] >= 0 && s.max_hp == 120.0f && s.atk == 94.0f && std::fabs(s.def - 72.6f) < 1e-4f,
              "yso_19 Toal L11: HP 120, ATK 94, DEF 72.6 (Cleria Ring, buffs, foot)");
    }
    {   // Equip: only owned cells; re-selecting the worn accessory unequips it.
        std::int32_t f[0x200]; for (int& v : f) v = -1;
        f[151] = 0; f[152] = 6; f[7] = 1; f[54] = 1;
        InvState inv;
        const bool refused = !equip_cell(f, inv, 1, 2);          // armor 7 owned? cell 2 = 7: yes
        const bool armor_ok = f[152] == 7;
        const bool unowned = !equip_cell(f, inv, 1, 3);          // armor 8 not owned
        equip_cell(f, inv, 1, 13);                               // accessory 54
        const bool acc_on = inv.accessory == 54;
        equip_cell(f, inv, 1, 13);
        check(!refused && armor_ok && unowned && acc_on && inv.accessory == 0xff,
              "equip: owned cells only; the worn accessory toggles off (0xff)");
        check(!inv_is_equipped(f, inv, 60) && inv_is_equipped(f, inv, 7), "IsEquipped: armor 7 yes; 60..65 always false");
    }
    {   // Pickups: gold -> SP; a counted item -1 -> 1 -> 2; the cap leaves it in the world;
        // a Recovery Drop heals by its magnitude.
        std::int32_t f[0x200]; for (int& v : f) v = -1; f[217] = 0;
        InvState inv;
        float hp = 10, boost = 0, o2 = 0;
        EffectEnv env{f, &hp, 100.0f, true, &boost, &o2};
        int pop = 0;
        pickup(inv, items, env, 0x7a, &pop);
        const int gold = pop;
        pickup(inv, items, env, 87, &pop); pickup(inv, items, env, 87, &pop);
        f[89] = 99;
        const PickupResult capped = pickup(inv, items, env, 89, &pop);
        pickup(inv, items, env, 0x43, &pop);
        std::printf("[inv]   gold %d -> SP %d; Roda %d; heal %d -> HP %.0f\n", gold, inv.sp, f[87], pop, hp);
        check(inv.sp == gold && gold > 0 && f[87] == 2 && capped == PickupResult::LeftInWorld &&
              hp == 10.0f + float(pop) && pop == int(items.get(0x43)->magnitude),
              "pickup: gold -> SP; count -1 -> 1 -> 2; cap 99 stays; Recovery Drop heals by +0x24");
    }
    {   // Use: Panacea consumed (+3 max HP on difficulty 0); the Hammer runs @UseHammer.
        std::int32_t f[0x200]; for (int& v : f) v = -1; f[143] = 0; f[144] = 0; f[217] = 0;
        f[0x59] = 2; f[0x60] = 1;
        InvState inv;
        float hp = 50;
        EffectEnv env{f, &hp, 60.0f, true, nullptr, nullptr};
        std::string label;
        const UseResult a = use_item(inv, items, env, 0x59, &label);
        const UseResult b = use_item(inv, items, env, 0x60, &label);
        check(a == UseResult::Consumed && f[0x59] == 1 && inv.hp_bonus == 3.0f && b == UseResult::RunScript &&
              label == "@UseHammer", "use: Panacea consumed (+3 max HP), the Hammer runs @UseHammer");
    }
    std::printf("[inv] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
