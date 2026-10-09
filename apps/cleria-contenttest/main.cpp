// cleria-contenttest — mod content (phase 2a, build/shell_re/CONTENT.md): the INI reader, the
// content/*.ini parsers and their load-order merge with conflict notes, the item patch, the chest
// grant filter (on the real S_1001/S_BOX01 chest script when an asset root is given), the treasure
// text re-label, text.ini on a real script's pool, the rules, and the event bus / flag diff.
// Usage: cleria-contenttest [asset_root]   (writes only under %TEMP%; the asset root is only read)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cleria/content.hpp"
#include "cleria/inidoc.hpp"
#include "cleria/items.hpp"
#include "cleria/mod_events.hpp"
#include "cleria/sidecar.hpp"
#include "cleria/vfs.hpp"
#include "cleria/xso.hpp"

using namespace cleria;
namespace fs = std::filesystem;
namespace ct = cleria::content;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[content] %-86s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool has_note(const ct::Content& c, const std::string& mod, const std::string& part) {
    for (const ct::Note& n : c.notes)
        if (n.mod == mod && n.text.find(part) != std::string::npos) return true;
    return false;
}
static bool has_error(const ct::Content& c, const std::string& part) {
    for (const ct::Note& n : c.errors)
        if (n.text.find(part) != std::string::npos) return true;
    return false;
}
static std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

// A host that records what the chest shows.
struct ChestHost : XsoHost {
    void spawn(int, int, float, float, float, float, int) override {}
    bool is_alive(int) const override { return false; }
    std::vector<int> icons, boxes;
    std::vector<std::string> texts;
    void effect_get_item(int item, int, const std::string&) override { icons.push_back(item); }
    void acquire_item(float, float, int id, const std::string& t) override { boxes.push_back(id); texts.push_back(t); }
};

int main(int argc, char** argv) {
    // ---- the INI reader ----------------------------------------------------------------------------
    {
        const IniDoc d = IniDoc::parse(
            "\xEF\xBB\xBF; head\r\nroot=1\r\n[A]\r\nIcon=X   ; a note\r\nDo=give 0x59; text \"a;b\"\r\nDo=flag 1 = 2\r\n"
            "\"Original text\"=\"New \\\"one\\\"\"\r\n3=\"keep\\nescape\"\r\nbad line\r\n[B\r\n");
        bool ok = d.sections.size() == 2 && d.sections[0].pairs.size() == 1 && d.sections[1].name == "A";
        const IniDoc::Section& a = d.sections[1];
        ok = ok && a.find("icon") && a.find("icon")->value == "X" && a.pairs.size() == 5;
        ok = ok && a.pairs[1].value == "give 0x59; text \"a;b\"" && a.find("DO")->value == "flag 1 = 2";
        ok = ok && a.pairs[3].quoted_key && a.pairs[3].key == "Original text" && a.pairs[3].value == "New \"one\"";
        ok = ok && a.pairs[4].value == "keep\\nescape" && d.errors.size() == 2;
        check(ok, "IniDoc: BOM, comments, ' ;' trailing comment vs glued ';', quoted keys, repeats, errors");
    }

    // ---- parsers + merge ---------------------------------------------------------------------------
    ct::Content c;
    ct::merge_file(c, "a.mod", "items.ini",
                   "[item.0x59]\r\nName=\"Greater Panacea\"\r\nDescription=\"Line 1\\nLine 2\"\r\nMagnitude=12\r\n"
                   "[item.0x100]\r\nName=\"Lantern\"\r\nIcon=AP_LANTERN\r\nKind=flag\r\nSetsFlag=lantern.lit\r\n"
                   "[item.0x101]\r\nKind=heal\r\nAmount=50\r\n"
                   "[item.0x90]\r\nName=x\r\n[item.0x59x]\r\n[thing]\r\n[item.0x5]\r\nKind=key\r\n");
    ct::merge_file(c, "a.mod", "chests.ini",
                   "[S_1001/S_BOX01]\r\nItem=0x100\r\n[s_1001\\s_box03/0x36]\r\nItem=0x57\r\nCount=2\r\n"
                   "[S_1001/S_BOX02]\r\nItem=none\r\n[S_1003/S_BOX01]\r\nCount=3\r\n[nochest]\r\nItem=1\r\n");
    ct::merge_file(c, "a.mod", "pickups.ini",
                   "[pickup.p1]\r\nRoom=s_1009\r\nPos=1.5, 2, -3\r\nItem=0x57\r\n[pickup.p2]\r\nRoom=S_1009\r\nItem=1\r\n");
    ct::merge_file(c, "a.mod", "rules.ini",
                   "[rule.boss]\r\nOn=flag 0x1A0 >= 1\r\nDo=give 0x59; text \"The tower trembles.\"; flag 0x1F1 = 1\r\nOnce=1\r\n"
                   "[rule.lit]\r\nOn=item 0x100\r\nDo=cflag lantern.uses += 1\r\n"
                   "[rule.bad]\r\nOn=moon phase\r\nDo=give 0x59\r\n");
    ct::merge_file(c, "a.mod", "text.ini",
                   "[MAP/S_10/S_1009/SAVE.XSO]\r\n3=\"<color:0xffacdae8>Modded.\"\r\n\"eft_clear\"=\"eft_clear\"\r\n"
                   "[*]\r\n\"Original\"=\"Replaced\"\r\n");
    ct::merge_file(c, "a.mod", "weird.ini", "[x]\r\n");
    {
        const auto i59 = c.items.find(0x59), i100 = c.items.find(0x100), i101 = c.items.find(0x101);
        bool ok = i59 != c.items.end() && i59->second.name == "Greater Panacea" && i59->second.magnitude == 12u;
        ok = ok && i100 != c.items.end() && i100->second.kind == "flag" && i100->second.icon == "ap_lantern" &&
             i100->second.sets_flag == "lantern.lit";
        ok = ok && i101 != c.items.end() && i101->second.name == "Mod item 0x101" && i101->second.amount == 50;
        ok = ok && !c.items.count(0x90) && has_error(c, "0x100..0x1FF") && has_error(c, "unknown section [thing]") &&
             has_error(c, "Kind applies to new items only");
        check(ok, "items.ini: retail patch, new items (Kind / Amount / SetsFlag), id range + key errors");
    }
    {
        const ct::ChestDef* b1 = ct::find_chest(c, "s_1001", "s_box01", 0x59);
        const ct::ChestDef* b3a = ct::find_chest(c, "S_1001", "S_BOX03", 0x36);
        const ct::ChestDef* b3b = ct::find_chest(c, "S_1001", "S_BOX03", 0x3C);
        const ct::ChestDef* b2 = ct::find_chest(c, "S_1001", "@s_box02", 0x63);
        bool ok = b1 && b1->item == 0x100 && b1->count == 1 && b3a && b3a->item == 0x57 && b3a->count == 2 && !b3b;
        ok = ok && b2 && b2->nothing && !ct::find_chest(c, "S_1003", "S_BOX01", 0x57) && has_error(c, "has no Item") &&
             has_error(c, "[<room>/<script>]");
        check(ok, "chests.ini: room/script keys (case, '\\', '@'), item-qualified keys, Item=none, errors");
    }
    {
        const auto p1 = c.pickups.find("p1");
        bool ok = p1 != c.pickups.end() && p1->second.room == "S_1009" && p1->second.pos[0] == 1.5f &&
                  p1->second.pos[2] == -3.0f && p1->second.item == 0x57 && p1->second.flag == "pickup.p1" &&
                  !c.pickups.count("p2") && has_error(c, "needs Room, Pos and Item");
        check(ok, "pickups.ini: room / pos / item / default collected-flag; incomplete entries refused");
    }
    {
        const auto r = c.rules.find("boss");
        bool ok = r != c.rules.end() && r->second.once && r->second.on.kind == events::Kind::FlagChanged &&
                  r->second.on.index == 0x1A0 && r->second.act.size() == 3 &&
                  r->second.act[0].kind == ct::Action::Kind::Give && r->second.act[0].item == 0x59 &&
                  r->second.act[1].text == "The tower trembles." && r->second.act[2].index == 0x1F1;
        ok = ok && !c.rules.count("bad") && has_error(c, "unknown condition");
        std::string e;
        ok = ok && ct::parse_cond("room_enter s_1100", &e)->arg == "S_1100" &&
             ct::parse_cond("chest S_1001/S_BOX01", &e)->kind == events::Kind::ChestOpened &&
             ct::parse_cond("death", &e) && ct::parse_cond("new_game", &e) && !ct::parse_cond("flag 9999 >= 1", &e) &&
             !ct::parse_cond("flag 3 ~ 1", &e);
        e.clear();
        ct::parse_actions("give 0x90", &e);
        ok = ok && !e.empty();
        e.clear();
        ct::parse_actions("text \"unterminated", &e);
        ok = ok && !e.empty();
        check(ok, "rules.ini: On / Do / Once, conditions + actions, errors (bad item, bad op, quotes)");
    }
    {
        const auto t = c.text.find(vfs::key("MAP/S_10/S_1009/SAVE.XSO"));
        bool ok = t != c.text.end() && t->second.by_index.at(3) == "<color:0xffacdae8>Modded." &&
                  t->second.by_text.count("eft_clear") && c.text.count("*") && has_error(c, "unknown content file");
        check(ok, "text.ini: by pool index, by original text, the [*] section; unknown files reported");
    }
    {   // a second mod later in the load order wins and both get a note
        ct::Content m = c;
        ct::merge_file(m, "b.mod", "items.ini", "[item.0x59]\r\nName=\"Other Panacea\"\r\n");
        ct::merge_file(m, "b.mod", "chests.ini", "[S_1001/S_BOX01]\r\nItem=0x63\r\n");
        ct::merge_file(m, "b.mod", "rules.ini", "[rule.boss]\r\nOn=death\r\nDo=text \"x\"\r\n");
        ct::merge_file(m, "b.mod", "text.ini", "[MAP/S_10/S_1009/SAVE.XSO]\r\n3=\"B\"\r\n");
        ct::merge_file(m, "b.mod", "pickups.ini", "[pickup.p1]\r\nRoom=S_1000\r\nPos=0,0,0\r\nItem=1\r\n");
        bool ok = m.items[0x59].name == "Other Panacea" && m.items[0x59].description && m.items[0x59].magnitude == 12u;
        ok = ok && ct::find_chest(m, "S_1001", "S_BOX01", 0x59)->item == 0x63 &&
             m.rules["boss"].on.kind == events::Kind::PlayerDeath && m.pickups["p1"].room == "S_1000" &&
             m.text[vfs::key("MAP/S_10/S_1009/SAVE.XSO")].by_index[3] == "B";
        ok = ok && has_note(m, "a.mod", "item 0x59 Name replaced by b.mod") &&
             has_note(m, "b.mod", "replaces item 0x59 Name of a.mod") && has_note(m, "a.mod", "chest S_1001/S_BOX01") &&
             has_note(m, "b.mod", "rule boss") && has_note(m, "b.mod", "pickup p1") && has_note(m, "b.mod", "#3") &&
             m.mods.size() == 2 && m.mods[1] == "b.mod";
        // the same mod re-setting its own entry notes nothing
        ok = ok && !has_note(c, "a.mod", "replaced by a.mod");
        check(ok, "merge: the later mod wins per item field / chest / rule / pickup / text key, notes on both");
    }
    {   // merge_dir: files by name, only *.ini
        const fs::path tmp = fs::temp_directory_path() / "cleria_contenttest";
        std::error_code ec;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp / "content", ec);
        { std::ofstream(tmp / "content" / "chests_floor2.ini", std::ios::binary) << "[S_1002/S_BOX01]\r\nItem=0x10\r\n"; }
        { std::ofstream(tmp / "content" / "readme.txt", std::ios::binary) << "x"; }
        ct::Content d;
        const int n = ct::merge_dir(d, "dir.mod", tmp / "content");
        check(n == 1 && ct::find_chest(d, "S_1002", "S_BOX01", 1) && d.errors.empty() &&
                  ct::merge_dir(d, "x", tmp / "nothing") == 0,
              "merge_dir: content/*.ini picked by prefix (chests_floor2.ini), other files ignored");
        fs::remove_all(tmp, ec);
    }

    // ---- the item patch ----------------------------------------------------------------------------
    {
        ItemTable t;
        t.items.resize(0x80);
        t.items[0x59].name = "Celcetan Panacea";
        t.items[0x59].description = "old";
        ct::patch_items(c, t);
        const Item* n = t.get(0x100);
        bool ok = t.items.size() == 0x102 && t.name(0x59) == "Greater Panacea" &&
                  t.items[0x59].description == "Line 1\r\nLine 2" && t.items[0x59].magnitude == 12u;
        ok = ok && n && n->name == "Lantern" && n->icon == "ap_lantern" && t.get(0xA0) && t.get(0xA0)->name.empty();
        ok = ok && t.icon_path("R", 0x100) == fs::path("R") / "MENU" / "ICON" / "AP_LANTERN.DDS";
        // the process-wide hook every ItemTable::load runs
        set_item_patch(nullptr);
        check(ok, "patch_items: retail fields (CR/LF description), the table grows to new ids, icon path");
    }

    // ---- the treasure text ---------------------------------------------------------------------------
    {
        const std::string o1 = "Acquired\\n<color:0xfff2ea8c>Celcetan Panacea<color:> x 1.";
        const std::string o2 = "Acquired\\n<color:0xfff2ea8c>Bronze Key<color:>.";
        bool ok = ct::treasure_text(o1, "Lantern", 1) == "Acquired\\n<color:0xfff2ea8c>Lantern<color:> x 1.";
        ok = ok && ct::treasure_text(o1, "Roda Fruit", 3) == "Acquired\\n<color:0xfff2ea8c>Roda Fruit<color:> x 3.";
        ok = ok && ct::treasure_text(o2, "Lantern", 1) == "Acquired\\n<color:0xfff2ea8c>Lantern<color:>.";
        ok = ok && ct::treasure_text(o2, "Roda Fruit", 2) == "Acquired\\n<color:0xfff2ea8c>Roda Fruit<color:> x 2.";
        ok = ok && ct::treasure_text("", "Lantern", 1) == "Acquired\\n<color:0xfff2ea8c>Lantern<color:>.";
        check(ok, "treasure_text: the coloured name and ' x N' re-labelled; a bare text rebuilt (retail shape)");
    }

    // ---- the grant filter: decide() on queries -------------------------------------------------------
    {
        std::int32_t flags[0x200] = {};
        for (int i = 0; i < 0x80; ++i) flags[i] = -1;
        flags[0x57] = 4;
        GrantQuery q;
        q.flags = flags;
        q.script = "s_box03";
        q.kind = GrantQuery::Kind::Store;
        q.index = 0x36; q.old_value = -1; q.value = 1;
        std::vector<ct::Obtained> got;
        GrantDecision d = ct::decide(c, q, "S_1001", nullptr, &got);
        bool ok = d.action == GrantDecision::Action::Replace && d.index == 0x57 && d.value == 6 && got.size() == 1 &&
                  got[0].item == 0x57 && got[0].count == 2 && !got[0].apply;
        q.index = 0x3C;                                   // Toal's grant of the same chest: not redirected
        ok = ok && ct::decide(c, q, "S_1001", nullptr, nullptr).action == GrantDecision::Action::Pass;
        q.script = "s_box01"; q.index = 0x59; q.old_value = 2; q.value = 3; got.clear();
        d = ct::decide(c, q, "S_1001", nullptr, &got);
        ok = ok && d.action == GrantDecision::Action::Replace && d.index == -1 && got.size() == 1 && got[0].apply &&
             got[0].item == 0x100;
        q.index = 302; q.old_value = 0; q.value = 1;      // the box flag: never a grant
        ok = ok && ct::decide(c, q, "S_1001", nullptr, nullptr).action == GrantDecision::Action::Pass;
        q.index = 0x59; q.old_value = 3; q.value = 2;     // a count going down: not a grant
        ok = ok && ct::decide(c, q, "S_1001", nullptr, nullptr).action == GrantDecision::Action::Pass;
        q.script = "s_box02"; q.index = 0x63; q.old_value = -1; q.value = 1;
        ok = ok && ct::decide(c, q, "S_1001", nullptr, nullptr).action == GrantDecision::Action::Suppress;
        q.kind = GrantQuery::Kind::Icon;
        ok = ok && ct::decide(c, q, "S_1001", nullptr, nullptr).action == GrantDecision::Action::Suppress;
        q.kind = GrantQuery::Kind::Window; q.text = "Acquired\\n<color:0xfff2ea8c>Bronze Key<color:>.";
        d = ct::decide(c, q, "S_1001", nullptr, nullptr);
        ok = ok && d.action == GrantDecision::Action::Replace && d.index == ct::kNoIcon && d.text == "The chest was empty.";
        q.script = "s_box01"; q.index = 0x59;             // another room's S_BOX01 is not this chest
        ok = ok && ct::decide(c, q, "S_1002", nullptr, nullptr).action == GrantDecision::Action::Pass;
        ct::Content none;
        ok = ok && ct::decide(none, q, "S_1001", nullptr, nullptr).action == GrantDecision::Action::Pass;
        check(ok, "decide: store redirect (inventory rule), new item -> caller, Item=none, icon/window, pass-through");
    }

    // ---- the event bus ---------------------------------------------------------------------------------
    {
        events::Bus b;
        std::vector<std::string> log;
        const int all = b.subscribe([&](const events::Event& e) { log.push_back(std::string("all:") + events::kind_name(e.kind)); });
        b.subscribe(events::Kind::ItemObtained, [&](const events::Event& e) { log.push_back("item:" + std::to_string(e.item)); });
        events::Event e;
        e.kind = events::Kind::ItemObtained; e.item = 0x59;
        b.emit(e);
        e.kind = events::Kind::Save;
        b.emit(e);
        b.unsubscribe(all);
        e.kind = events::Kind::ItemObtained; e.item = 1;
        b.emit(e);
        bool ok = log == std::vector<std::string>{"all:item", "item:89", "all:save", "item:1"} && b.size() == 1;
        // a handler that re-emits is bounded
        events::Bus r;
        int n = 0;
        r.subscribe([&](const events::Event& ev) { ++n; r.emit(ev); });
        r.emit(e);
        ok = ok && n == events::Bus::kMaxDepth && r.dropped == 1;
        // the flag diff
        std::int32_t f[0x200] = {};
        events::FlagWatch w;
        std::vector<events::Event> seen;
        events::Bus fb;
        fb.subscribe(events::Kind::FlagChanged, [&](const events::Event& ev) { seen.push_back(ev); });
        f[5] = 7;
        ok = ok && w.diff(f, fb, "S_1000") == 0 && seen.empty();       // the first call primes
        f[5] = 8; f[0x1A0] = 1;
        ok = ok && w.diff(f, fb, "S_1000") == 2 && seen.size() == 2 && seen[0].index == 5 && seen[0].old_value == 7 &&
             seen[0].new_value == 8 && seen[1].index == 0x1A0 && seen[1].room == "S_1000";
        ok = ok && w.diff(f, fb, "S_1000") == 0;
        f[9] = 3;
        w.prime(f);                                                       // a load: no report
        ok = ok && w.diff(f, fb, "S_1000") == 0;
        check(ok, "event bus: all / per-kind subscribers, unsubscribe, bounded re-emit; the flag diff + prime");
    }

    // ---- rules ------------------------------------------------------------------------------------------
    {
        const ct::RuleDef& boss = c.rules["boss"];
        Sidecar sc;
        events::Event e;
        e.kind = events::Kind::FlagChanged; e.index = 0x1A0; e.old_value = 0; e.new_value = 1;
        bool ok = ct::rule_fires(boss, e, sc);
        e.old_value = 1; e.new_value = 2;                                // already true before: no edge
        ok = ok && !ct::rule_fires(boss, e, sc);
        e.old_value = 0; e.new_value = 1;
        std::int32_t flags[0x200] = {};
        std::vector<int> given;
        std::vector<std::string> msgs;
        ct::RuleEnv env;
        env.flags = flags; env.sc = &sc;
        env.give = [&](int item, int n) { given.push_back(item); given.push_back(n); };
        env.message = [&](const std::string& m) { msgs.push_back(m); };
        ct::run_rule(boss, env);
        ok = ok && given == std::vector<int>{0x59, 1} && msgs.size() == 1 && flags[0x1F1] == 1 && sc.flag("rule.boss") == 1;
        ok = ok && !ct::rule_fires(boss, e, sc);                         // Once: spent for this save
        const ct::RuleDef& lit = c.rules["lit"];
        events::Event it;
        it.kind = events::Kind::ItemObtained; it.item = 0x100;
        ok = ok && ct::rule_fires(lit, it, sc);
        ct::run_rule(lit, env);
        ct::run_rule(lit, env);
        ok = ok && sc.flag("lantern.uses") == 2 && ct::rule_fires(lit, it, sc);
        ct::RuleDef ch;
        ch.on = *ct::parse_cond("chest S_1001/S_BOX01", nullptr);
        events::Event ce;
        ce.kind = events::Kind::ChestOpened; ce.room = "s_1001"; ce.script = "s_box01";
        ok = ok && ct::rule_fires(ch, ce, sc);
        ce.room = "S_1002";
        ok = ok && !ct::rule_fires(ch, ce, sc);
        check(ok, "rules: flag edges, Once kept in the sidecar, give / text / flag / cflag += actions, chest keys");
    }

    // ---- real scripts (asset root) ----------------------------------------------------------------------
    if (argc >= 2 && fs::exists(fs::path(argv[1]) / "MAP" / "S_10" / "S_1001" / "S_BOX01.XSO")) {
        const fs::path root = argv[1];
        const Xso box = Xso::parse(slurp(root / "MAP" / "S_10" / "S_1001" / "S_BOX01.XSO"));
        ItemTable names;
        names.items.resize(0x80);
        names.items[0x59].name = "Celcetan Panacea";
        ct::patch_items(c, names);
        auto run_box = [&](bool filtered, ChestHost& host, std::vector<ct::Obtained>& got, XsoVm& vm) {
            vm.new_game(1);
            vm.g_flags[301] = 1;   // S_1001's barrier cleared (BARRIER_OBJECTS.md): the chest's guard
            vm.host = &host;
            vm.script_name = "s_box01";
            vm.vars.s[0] = "box_01";
            if (filtered)
                vm.grant_filter = [&](const GrantQuery& q) { return ct::decide(c, q, "S_1001", &names, &got); };
            vm.run(box);
        };
        ChestHost h0, h1;
        std::vector<ct::Obtained> g0, g1;
        XsoVm v0, v1;
        run_box(false, h0, g0, v0);
        run_box(true, h1, g1, v1);
        // The vanilla run: Panacea (0x59) 1, box flag 302 set, icon + box show 0x59.
        bool van = v0.g_flags[0x59] == 1 && v0.g_flags[302] == 1 && h0.icons == std::vector<int>{0x59} &&
                   h0.boxes == std::vector<int>{0x59} &&
                   h0.texts[0] == "Acquired\\n<color:0xfff2ea8c>Celcetan Panacea<color:> x 1.";
        check(van, "real S_1001/S_BOX01 unfiltered: Panacea 0x59 = 1, box flag 302, icon + 'Acquired' box");
        bool red = v1.g_flags[0x59] == -1 && v1.g_flags[302] == 1 && g1.size() == 1 && g1[0].item == 0x100 && g1[0].apply &&
                   h1.icons == std::vector<int>{0x100} && h1.boxes == std::vector<int>{0x100} &&
                   h1.texts[0] == "Acquired\\n<color:0xfff2ea8c>Lantern<color:> x 1.";
        bool no59 = true;
        for (const Grant& gr : v1.grants) no59 = no59 && gr.index != 0x59;
        check(red && no59, "real S_1001/S_BOX01 redirected to new item 0x100: no 0x59 grant, box flag kept, icon + box re-labelled");
        // Every other flag write is identical between the two runs (only the item cell differs).
        bool same = true;
        for (int i = 0; i < 0x200; ++i)
            if (i != 0x59) same = same && v0.g_flags[i] == v1.g_flags[i];
        check(same, "real S_1001/S_BOX01: every flag but the redirected item cell matches the vanilla run");
        // A filter that passes everything is the vanilla run, grant for grant.
        XsoVm v2;
        ChestHost h2;
        v2.new_game(1);
        v2.g_flags[301] = 1;
        v2.host = &h2;
        v2.script_name = "s_box01";
        v2.grant_filter = [](const GrantQuery&) { return GrantDecision{}; };
        v2.run(box);
        bool pass = v2.grants.size() == v0.grants.size() && h2.texts == h0.texts && h2.icons == h0.icons;
        for (std::size_t i = 0; pass && i < v2.grants.size(); ++i)
            pass = v2.grants[i].index == v0.grants[i].index && v2.grants[i].value == v0.grants[i].value;
        for (int i = 0; i < 0x200; ++i) pass = pass && v2.g_flags[i] == v0.g_flags[i];
        check(pass, "real S_1001/S_BOX01: a pass-through filter = the unfiltered run (grants, flags, box)");
        // The blessing shop's filter (xso.hpp ShopQuery) on the real @Grow03 / @GrowAr / @GrowMenu scripts.
        if (fs::exists(root / "MAP" / "S_COMMON" / "GROW03.XSO")) {
            struct ShopHost : ChestHost {
                int sp = 0;
                std::vector<std::string> rows;
                int gold() const override { return sp; }
                void set_gold(int v) override { sp = v; }
                void menu_add(const std::string& t, const std::string&) override { rows.push_back(t); }
            };
            const Xso grow03 = Xso::parse(slurp(root / "MAP" / "S_COMMON" / "GROW03.XSO"));
            const Xso growar = Xso::parse(slurp(root / "MAP" / "S_COMMON" / "GROWAR.XSO"));
            const Xso menu = Xso::parse(slurp(root / "MAP" / "S_COMMON" / "GROWMENU.XSO"));
            auto buy = [&](const Xso& x, const char* name, int sp, std::function<GrantDecision(const ShopQuery&)> f,
                           ShopHost& sh, XsoVm& vm) {
                vm.new_game(1);
                sh.sp = sp;
                vm.host = &sh;
                vm.script_name = name;
                vm.shop_filter = std::move(f);
                vm.run(x);
            };
            check(blessing_index("@Grow03") == 3 && blessing_index("@grow25") == 25 && blessing_index("@GrowAr") == 7 &&
                  blessing_index("growle") == 8 && blessing_index("@GrowMenu") == -1 && blessing_index("@Grow_YesNo") == -1 &&
                  blessing_index("s_box01") == -1, "shop: blessing_index of the @Grow* script names");
            // Vanilla: 24 000 SP for bit 3; with 23 999 the script says "not enough" and nothing changes.
            ShopHost a0, a1;
            XsoVm s0, s1;
            buy(grow03, "@grow03", 30000, nullptr, a0, s0);
            buy(grow03, "@grow03", 23999, nullptr, a1, s1);
            check(a0.sp == 6000 && (s0.g_flags[217] & 8) != 0 && a1.sp == 23999 && s1.g_flags[217] == 0,
                  "real @Grow03 unfiltered: 24000 SP for blessing bit 3; one SP short buys nothing");
            // A filter prices it at 500, hears the purchase with the SP paid, and suppresses the bit.
            std::vector<std::string> seen;
            auto f = [&](const ShopQuery& q) {
                GrantDecision d;
                static const char* kK[] = {"price", "row", "owned", "buy"};
                seen.push_back(std::string(kK[int(q.kind)]) + " " + std::to_string(q.sub) + " " + std::to_string(q.index) + " " +
                               std::to_string(q.value));
                if (q.kind == ShopQuery::Kind::Price) { d.action = GrantDecision::Action::Replace; d.value = 500; }
                if (q.kind == ShopQuery::Kind::Buy) d.action = GrantDecision::Action::Suppress;
                return d;
            };
            ShopHost a2;
            XsoVm s2;
            buy(grow03, "@grow03", 600, f, a2, s2);
            check(a2.sp == 100 && s2.g_flags[217] == 0 &&
                  seen == std::vector<std::string>{"owned 154 3 0", "price 97 3 24000", "price 105 3 24000", "buy 175 3 500"},
                  "real @Grow03 filtered: owned?, the 0x61 and 0x69 prices answered 500, the purchase heard, the bit suppressed");
            // An "owned" answer of 1 ends the script at "already applied": no SP moves.
            ShopHost a3;
            XsoVm s3;
            buy(grow03, "@grow03", 30000, [](const ShopQuery& q) {
                GrantDecision d;
                if (q.kind == ShopQuery::Kind::Owned) { d.action = GrantDecision::Action::Replace; d.value = 1; }
                return d; }, a3, s3);
            check(a3.sp == 30000 && s3.g_flags[217] == 0, "real @Grow03: owned answered 1 -> 'already applied', nothing bought");
            // @GrowAr: the Raval cost of flag 218 (0xB0) is the price both its compare and its deduction read.
            ShopHost a4;
            XsoVm s4;
            s4.new_game(1);
            int paid = -1;
            a4.sp = 1000;
            s4.host = &a4;
            s4.script_name = "@growar";
            s4.g_flags[152] = 6;                                   // the first armor: cost 100
            s4.shop_filter = [&](const ShopQuery& q) {
                GrantDecision d;
                if (q.kind == ShopQuery::Kind::Price && q.sub == 0xb0 && q.index == 7 && q.value == 100) {
                    d.action = GrantDecision::Action::Replace; d.value = 250;
                }
                if (q.kind == ShopQuery::Kind::Buy) paid = q.value;
                return d;
            };
            s4.run(growar);
            check(a4.sp == 750 && paid == 250 && s4.item_arr[6] == 1, "real @GrowAr filtered: the 0xB0 cost 100 -> 250, paid, the armor's Raval set");
            // @GrowMenu: every row of a blessing passes by (price, then the row's text); other rows do not. (A bare
            // run re-enters the menu until its step cap, so the same 15 rows repeat.)
            ShopHost a5;
            XsoVm s5;
            int rows = 0, prices = 0;
            buy(menu, "@growmenu", 0, [&](const ShopQuery& q) {
                GrantDecision d;
                if (q.kind == ShopQuery::Kind::Price) { ++prices; if (q.index == 3) { d.action = GrantDecision::Action::Replace; d.value = 42; } }
                if (q.kind == ShopQuery::Kind::Row) {
                    ++rows;
                    if (q.index == 3) { d.action = GrantDecision::Action::Replace; d.text = "AP location - [SP:]" + std::to_string(q.value); }
                }
                return d; }, a5, s5);
            bool relabel = false, vanilla = false, nothing = false, old = false, plain = false;
            for (const std::string& t : a5.rows) {
                relabel = relabel || t == "AP location - [SP:]42";
                vanilla = vanilla || t == "Increase SP gain - [SP:]16000";
                nothing = nothing || t.find("Nothing") != std::string::npos;
                old = old || t == "Increase item drop rate - [SP:]24000";
            }
            ShopHost a6;
            XsoVm s6;
            buy(menu, "@growmenu", 0, nullptr, a6, s6);
            for (const std::string& t : a6.rows) plain = plain || t == "Increase item drop rate - [SP:]24000";
            check(relabel && vanilla && nothing && !old && plain && rows == prices && rows > 0 && a5.rows.size() == a6.rows.size() &&
                  a5.rows.size() >= 15 && a5.rows[0] == "Strengthen current armor - [SP:]100" && a5.rows[14] == "Nothing right now",
                  "real @GrowMenu filtered: a blessing row re-priced and re-labelled, the others and the row count unchanged");
        }
        // A retail redirect through the ScriptManager (the live path: the instance name is the key).
        {
            names.items[0x57].name = "Roda Fruit";
            ct::Content r;
            ct::merge_file(r, "r", "chests.ini", "[S_1001/S_BOX01]\r\nItem=0x57\r\nCount=2\r\n");
            ScriptManager mgr;
            ChestHost h;
            mgr.vm.new_game(1);
            mgr.vm.g_flags[301] = 1;
            mgr.vm.host = &h;
            std::vector<ct::Obtained> got;
            mgr.vm.grant_filter = [&](const GrantQuery& q) { return ct::decide(r, q, "S_1001", &names, &got); };
            mgr.start_root(std::make_shared<Xso>(box), "s_box01");
            for (int t = 0; t < 400 && mgr.any_live(); ++t) mgr.tick_all();
            const bool ok = mgr.vm.g_flags[0x57] == 2 && mgr.vm.g_flags[0x59] == -1 && mgr.vm.g_flags[302] == 1 &&
                            got.size() == 1 && !got[0].apply && h.boxes == std::vector<int>{0x57} &&
                            h.texts[0].find("x 2.") != std::string::npos;
            check(ok, "ScriptManager S_BOX01 -> Roda Fruit x2: the VM writes cell 0x57 = 2 (inventory rule), box x 2");
        }
        // text.ini on the real SAVE.XSO pool
        {
            Xso sv = Xso::parse(slurp(root / "MAP" / "S_10" / "S_1009" / "SAVE.XSO"));
            const std::string s2 = sv.string(2);
            const int n = ct::patch_script(c, "MAP/S_10/S_1009/SAVE.XSO", sv);
            Xso other = Xso::parse(slurp(root / "MAP" / "S_10" / "S_1001" / "S_BOX01.XSO"));
            const int n2 = ct::patch_script(c, "MAP/S_10/S_1001/S_BOX01.XSO", other);
            check(n == 1 && sv.string(3) == "<color:0xffacdae8>Modded." && sv.string(2) == s2 && n2 == 0 &&
                      other.string(2) == box.string(2),
                  "text.ini: SAVE.XSO pool #3 replaced (an identical by-text entry counts nothing), others untouched");
        }
        // The item patch hook on the real INVINFO
        {
            ct::Content keep = c;
            static ct::Content* g = nullptr;
            g = &keep;
            set_item_patch([](ItemTable& t) { ct::patch_items(*g, t); });
            const ItemTable t = ItemTable::load(root / "MISC" / "INVINFO.DAT");
            set_item_patch(nullptr);
            const ItemTable u = ItemTable::load(root / "MISC" / "INVINFO.DAT");
            check(t.items.size() == 0x102 && t.name(0x59) == "Greater Panacea" && t.name(0x100) == "Lantern" &&
                      u.items.size() == 0x80 && u.name(0x59) == "Celcetan Panacea" && u.name(0x58) == t.name(0x58),
                  "set_item_patch: every ItemTable::load sees the patch; unset = INVINFO as shipped");
        }
    } else {
        std::printf("[content] (no asset root given: the real-script checks are skipped)\n");
    }

    std::printf("[content] %s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
