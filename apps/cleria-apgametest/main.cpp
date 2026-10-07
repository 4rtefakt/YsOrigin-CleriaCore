// cleria-apgametest — the Ys Origin side of the Archipelago client (src/cleria/ap/ys_game.hpp,
// build/shell_re/AP_GAME.md) on a fake game: the slot_data of a real default seed
// (res/ap/test_slot_data.json, made by tools/ap_slotdata.py from the apworld), the location registry
// and its detection, the vanilla-grant suppression, every receipt kind, the save block, the goal and
// the EXP scaling. Runs from the source dir (res/ap/...); no assets, no network.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "cleria/ap/ys_game.hpp"

using namespace cleria;
using namespace cleria::ap;
using namespace cleria::ap::ys;

static int fails = 0, checks = 0;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++fails; std::printf("FAIL: %s\n", what); }
}

static json::Value J(const std::string& s) {
    auto v = json::parse(s);
    if (!v) { std::printf("bad json in the test: %s\n", s.c_str()); std::exit(2); }
    return *v;
}
static json::Value load_json(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return J(ss.str());
}

struct FakeGame {
    std::int32_t flags[0x200] = {};
    std::int32_t item_arr[54] = {};
    std::uint8_t warp[24] = {};
    int sp = 0, tier = 0, level = 1, refreshes = 0;
    GameIo io;
    explicit FakeGame(int character = 1) {
        for (int i = 0; i < 0x80; ++i) flags[i] = -1;    // New Game: item cells -1 (never had)
        flags[150] = character;
        io.flags = flags; io.item_arr = item_arr; io.warp_on = warp; io.character = character;
        io.add_sp = [this](int n) { sp += n; };
        io.set_weapon_tier = [this](int t) { tier = t; flags[148] = t; };
        io.level = [this] { return level; };
        io.set_level = [this](int l) { level = l; };
        io.refresh = [this] { ++refreshes; };
    }
};

static const Table* g_table = nullptr;
static std::int64_t item_id(const char* name) {
    for (const Item& i : g_table->items)
        if (i.name == name) return i.id;
    std::printf("no item %s\n", name);
    return -1;
}
static bool has(const std::vector<std::int64_t>& v, std::int64_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

int main() {
    std::string err;
    auto table = ys::load("res/ap/ys_origin.json", &err);
    if (!table) { std::printf("table: %s\n", err.c_str()); return 2; }
    g_table = &*table;
    const json::Value sd = load_json("res/ap/test_slot_data.json");
    auto loc = [&](const char* name) { return sd["location_signals"][name].as_int(); };

    // ---- slot_data ----
    const SlotOptions o = parse_slot_options(sd);
    check(o.character == 0 && o.goal == 0, "character / goal");
    check(o.suppress_items.count(0x59) && !o.suppress_items.count(302), "suppress_items: the Panacea, not a box flag");
    check(o.suppress_give_ids.count(0x82) && !o.suppress_items.count(0x82), "0x82 only at the give-item op");
    check(o.start_items.size() == 2 && o.start_items[0] == 0x54 && o.start_items[1] == 0x56, "start items: the Crystals");
    check(o.progressive_skills.size() == 3 && o.progressive_skills.at("Progressive Wind Skill").level_cell == 0xB6,
          "progressive skills");
    check(o.progressive_gear.at("Progressive Armor").front() == 7, "Yunica's armour ladder");
    check(o.exp_base_mult == 20 && o.level_scaling == 2, "EXP scaling defaults");
    check(o.scene_floors.at(1001) == 2, "scene_floors");
    check(o.unsupported.empty(), "the default options are all supported");
    check(!parse_slot_options(J(R"({"random_start":true,"blessing_items":true})")).unsupported.empty(),
          "unsupported options are listed");

    // ---- detect conversion (the retail forms of location_detect) ----
    {
        Reg r;
        check(convert_detect(J(R"({"method":"flag","offset":"0x36BDD4"})"), r) && r.detect == Detect::Flag && r.flag == 302,
              "flag offset -> g_flags[302]");
        r = {};
        check(convert_detect(J(R"({"method":"flag","offset":"0x36A67C"})"), r) && r.detect == Detect::ItemArr && r.index == 10,
              "flag offset in the Raval cells -> item_arr[10]");
        r = {};
        check(convert_detect(J(R"({"method":"scene","scene":"S_1014/S_BOX01"})"), r) && r.scene == 1014, "scene tag");
        r = {};
        check(convert_detect(J(R"({"method":"bit","offset":"0x36BC80","bit":9})"), r) && r.flag == 217 && r.bit == 9, "bit");
        r = {};
        check(convert_detect(J(R"({"method":"floor","offset":"0x36BC58","floor":4})"), r) && r.floor == 4, "floor");
        r = {};
        check(!convert_detect(J(R"({"method":"flag","offset":"0x123"})"), r), "an unknown offset is refused");
        check(scene_number("S_7002") == 7002 && scene_number("x") == 0, "scene_number");
    }

    // ---- the registry ----
    Logic L;
    L.configure(sd, *table);
    check(L.regs().size() == sd["location_signals"].size(), "every active location is registered");
    const std::int64_t chest = loc("Wailing Blue: 2F Path 1");
    check(L.reg(chest) && L.reg(chest)->detect == Detect::Flag && L.reg(chest)->flag == 302, "S_1001/S_BOX01 by box flag 302");

    // ---- suppression ----
    check(L.suppress_store(0x59, -1, 1), "the Panacea grant (-1 -> 1) is swallowed");
    check(L.suppress_store(0x59, 1, 2), "a Panacea add (1 -> 2) is swallowed");
    check(!L.suppress_store(0x59, 2, 1), "using one (2 -> 1) passes");
    check(!L.suppress_store(302, 0, 1), "the box flag passes: it IS the check");
    check(L.suppress_give(0x82) && !L.suppress_give(0x59), "give-item ids");

    // ---- detection ----
    {
        FakeGame G;
        auto f = L.on_store(302, 0, 1);
        check(f.size() == 1 && f[0] == chest, "the chest's box-flag store fires its location");
        check(L.on_store(302, 1, 1).empty(), "once");
        check(L.on_store(314, 1, 0).empty(), "a store of 0 is not a check");
        G.flags[302] = 1;
        check(L.sweep(G.io).empty(), "the sweep does not re-fire a sent check");
        G.flags[314] = 1;
        auto s = L.sweep(G.io);
        check(s.size() == 1 && s[0] == loc("Wailing Blue: 2F Path 1 #2"), "the sweep catches a flag set behind the VM");
        G.item_arr[10] = 1;
        check(has(L.sweep(G.io), loc("Divine Blessing: Strengthen Battle Armor")), "item_arr location");
        G.flags[217] |= 1 << 9;
        check(has(L.sweep(G.io), loc("Divine Blessing: Bolster bad status resists LV1")), "blessing bit location");
        auto sc = L.on_scene(1099);
        check(has(sc, loc("Boss: 5F Velagunder (S_1099)")) && has(sc, loc("Reach 5F")), "scene + floor locations on entry");
        check(L.on_scene(1099).empty(), "a scene fires once");
        check(L.st.saw_gameplay, "gameplay seen");
    }

    // ---- receiving ----
    {
        Logic R;
        R.configure(sd, *table);
        FakeGame G(1);   // Yunica
        auto g = R.grant(item_id("Bronze Key"), kProgression, G.io);
        check(g.known && G.flags[0x63] == 1, "Bronze Key -> g_flags[0x63] = 1");
        R.grant(item_id("Bronze Key"), kProgression, G.io);
        check(G.flags[0x63] == 1, "a key item is set, never 2 (the altar == test)");
        R.grant(item_id("Celcetan Panacea"), 0, G.io);
        R.grant(item_id("Celcetan Panacea"), 0, G.io);
        check(G.flags[0x59] == 2, "Panacea stacks");
        R.grant(item_id("SP: 50"), 0, G.io);
        check(G.sp == 50, "SP into the wallet");
        R.grant(item_id("Cleria Ore"), 0, G.io);
        R.grant(item_id("Cleria Ore"), 0, G.io);
        check(G.tier == 2 && G.flags[0x58] == -1 && R.st.ore == 2, "Cleria Ore x2 -> weapon tier 2, not the item cell");
        R.grant(item_id("Progressive Armor"), kUseful, G.io);
        R.grant(item_id("Progressive Armor"), kUseful, G.io);
        check(G.flags[7] == 1 && G.flags[8] == 1 && G.flags[9] == -1, "Progressive Armor: Yunica's first two tiers");
        const std::int64_t wind = item_id("Progressive Wind Skill");
        R.grant(wind, 0, G.io);
        check(G.flags[107] == 1 && G.flags[116] == 1 && G.flags[0xB6] == 0, "first Wind: artifact + power");
        R.grant(wind, 0, G.io);
        R.grant(wind, 0, G.io);
        R.grant(wind, 0, G.io);
        R.grant(wind, 0, G.io);
        check(G.flags[0xB6] == 3, "later Winds raise the level, capped at 3");
        R.grant(item_id("Emerald"), kUseful, G.io);
        check(G.flags[0xB6] == 3, "a gem never passes 3");
        R.grant(item_id("Progressive Reduce MP usage"), 0, G.io);
        R.grant(item_id("Progressive Reduce MP usage"), 0, G.io);
        check((G.flags[217] & (1 << 6)) && (G.flags[217] & (1 << 18)) && !(G.flags[217] & (1 << 19)), "progressive blessing bits in order");
        auto bless = R.sweep(G.io);
        check(!has(bless, loc("Divine Blessing: Reduce MP usage LV1")), "a granted blessing bit is not a check (claimed)");
        R.grant(item_id("Blessing: Increase SP gain"), kUseful, G.io);
        check(G.flags[217] & (1 << 8), "blessing bit");
        g = R.grant(item_id("Chaos Warp"), kTrap, G.io);
        check(g.trap == "Chaos Warp", "a trap is handed to the view");
        R.grant(item_id("Cerulean Flabellum"), kProgression, G.io);
        check(G.flags[107] == 1 && G.flags[116] == 1, "an artifact also grants its power");
        R.grant(item_id("4F Warp"), kUseful, G.io);
        check(G.warp[1] == 1 && G.flags[311] == 1, "a statue unlock: warp registry 1 + purify flag 311");
        check(!has(R.sweep(G.io), loc("Statue: 4F Save (Forward) (S_1009)")), "our statue write is claimed, not a check");
        check(has(R.on_store(311, 0, 1), loc("Statue: 4F Save (Forward) (S_1009)")), "the player's own purify store still checks");
        check(!R.grant(123, 0, G.io).known, "an unknown id is skipped");
        check(G.refreshes > 5, "stat refresh after grants");
        FakeGame H(2);   // Hugo: the ladder of the character played, from the table without slot_data
        Logic R2;
        R2.configure(J("{}"), *table);
        R2.grant(item_id("Progressive Boots"), kUseful, H.io);
        check(H.flags[36] == 1, "Progressive Boots: Hugo's ladder (table fallback)");
    }

    // ---- the New Game loadout and the per-tick enforcement ----
    {
        Logic R;
        R.configure(sd, *table);
        FakeGame G;
        R.start_loadout(G.io);
        check(G.flags[0x54] == 1 && G.flags[0x56] == 1 && R.st.started, "start items");
        G.flags[0x54] = 0;
        R.start_loadout(G.io);
        check(G.flags[0x54] == 0, "the loadout is given once per save");
        R.st.ore = 3;
        G.flags[148] = 0;
        R.enforce(G.io);
        check(G.tier == 4, "the ore tier is re-enforced (3 ore -> 4)");
        // warp locks
        json::Value locks = J(R"({"statue_warp_locks":true,"start_statue_scene":1000,
            "statue_unlocks":{"1F Warp":{"scene":1000,"flag":"0x36BDCC"},"4F Warp":{"scene":1009,"flag":"0x36BDF8"}}})");
        Logic W;
        W.configure(locks, *table);
        FakeGame K;
        K.warp[0] = K.warp[1] = 1;
        check(W.suppress_store(311, 0, 1) && !W.suppress_store(300, 0, 1), "a locked statue's purify is swallowed, the start one passes");
        W.enforce(K.io);
        check(K.warp[0] == 1 && K.warp[1] == 0, "a locked statue leaves the warp registry");
        W.grant(item_id("4F Warp"), kUseful, K.io);
        W.enforce(K.io);
        check(K.warp[1] == 1 && !W.suppress_store(311, 0, 1), "unlocked by its item");
    }

    // ---- the save block ----
    {
        SaveState s;
        s.seed = "S1"; s.slot = 2; s.slot_name = "Yunica"; s.applied = 7;
        s.checks = {5857605, 5857606, 5857607, 5857700};
        s.claimed = {5857800};
        s.goal_sent = true; s.started = true; s.ore = 3; s.prog["Progressive Wind Skill"] = 2; s.statues = {1009};
        const auto kv = s.to_kv();
        check(kv.at("checks") == "5857605-5857607,5857700", "id ranges");
        const SaveState b = SaveState::from_kv(kv);
        check(b.seed == "S1" && b.slot == 2 && b.slot_name == "Yunica" && b.applied == 7 && b.checks == s.checks &&
              b.claimed == s.claimed && b.goal_sent && b.started && b.ore == 3 && b.prog.at("Progressive Wind Skill") == 2 &&
              b.statues.count(1009), "round trip");
        check(SaveState{}.to_kv().empty(), "no seed: nothing to save");
        check(ids_from_text("1-3,7").size() == 4, "ids_from_text");
    }

    // ---- goal ----
    {
        Logic R;
        R.configure(sd, *table);
        std::int32_t f[0x200] = {};
        f[150] = 1;
        check(!R.goal_reached(f, 1000), "no goal at the start");
        f[226] = 1;
        check(R.goal_reached(f, 1000), "Yunica: Dalles beaten");
        f[226] = 0; f[150] = 3; f[227] = 1;
        check(!R.goal_reached(f, 1000), "Toal: Darm at 1 is not the end");
        f[227] = 2;
        check(R.goal_reached(f, 1000), "Toal: BATTLE_END ran");
        std::int32_t g[0x200] = {};
        check(!R.goal_reached(g, 7002), "scene 7002 before any gameplay (the intro) is not the goal");
        R.on_scene(1000);
        check(R.goal_reached(g, 7002), "scene 7002 after gameplay");
        Logic A;
        A.configure(J(R"({"goal":1})"), *table);
        f[150] = 1; f[226] = 1;
        check(!A.goal_reached(f, 1000), "all bosses: the floor bosses too");
        for (int i = 220; i <= 225; ++i) f[i] = 1;
        check(A.goal_reached(f, 1000), "all bosses beaten");
    }

    // ---- EXP scaling ----
    {
        Logic R;
        R.configure(J(R"({"level_scaling":2,"exp_base_mult":3,"exp_catchup_mult":8,"exp_catchup_margin":2,
                          "scene_levels":{"1000":1,"2000":15}})"), *table);
        check(R.exp_factor(5) == 3.0f, "base multiplier before any expected level");
        R.on_scene(2000);
        check(R.exp_factor(10) == 8.0f && R.exp_factor(17) == 8.0f && R.exp_factor(18) == 3.0f, "catch-up under expected + margin");
        R.on_scene(1000);
        check(R.exp_factor(10) == 8.0f, "keyed to the deepest floor seen");
        check(R.level_floor(1) == 0, "no level floor in EXP mode");
        Logic F;
        F.configure(J(R"({"level_scaling":1,"level_margin":3,"scene_levels":{"2000":15}})"), *table);
        F.on_scene(2000);
        check(F.level_floor(5) == 12 && F.level_floor(12) == 0 && F.exp_factor(5) == 1.0f, "level floor mode");
        Logic N;
        N.configure(J("{}"), *table);
        check(N.exp_factor(1) == 1.0f, "off");
    }

    std::printf("cleria-apgametest: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
