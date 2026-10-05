// cleria-booktest — the pause book (src/cleria/pausebook.hpp) against
// build/menu_re/PAUSEBOOK_DRAW.md: the tweener (§0.3), the tab slide-in and page wrap (§2),
// the Equip neighbour graph and slot writes (§3), the Item grid and the Use popup (§6),
// and the draw order (§2.3).
// Usage: cleria-booktest <assets dir>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "cleria/cast.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/inventory.hpp"
#include "cleria/items.hpp"
#include "cleria/pausebook.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[book] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-booktest <assets>\n"); return 2; }
    const std::filesystem::path root = argv[1];
    const GameInfo gi = GameInfo::load(root / "MISC" / "GAMEINFO.DAT");
    const ItemTable items = ItemTable::load(root / "MISC" / "INVINFO.DAT");

    {   // §0.3: position steps by speed along the line, snaps when speed^2 >= d^2.
        MenuSprite s;
        s.set_rect(0, -51.2f, 10, 10);
        s.set_target(0, 0, 16);
        int n = 0;
        while (tween(s) && n < 100) ++n;
        check(s.y == 0.0f && n == 4, "slide-in from -51.2 at speed 16 lands in 4 steps");
        s.set_colour(0x00FFFFFF, 0);
        s.fade_to(0xFFFFFFFF, 0, 16);
        tween(s);
        check(s.diffuse == 0xFFFFFFFF, "a speed-16 fade from alpha 0 snaps (16^2 >= 255)");
        s.set_colour(0x00FFFFFF, 0);
        s.fade_to(0xFFFFFFFF, 0, 8);
        tween(s);
        check((s.diffuse >> 24) == 127, "a speed-8 fade steps 255*8/sqrt(255) = 127");
    }

    std::int32_t f[0x260]; for (int& v : f) v = -1;
    InvState inv; inv_new_game(inv);
    f[150] = 1;                                   // Yunica
    f[151] = 0; f[152] = -1; f[153] = -1;
    f[0] = 1; f[6] = 1; f[7] = 0;                 // the axe, Leather Armor, a used-up armour
    f[228] = 0;
    const auto& eg = equip_grid(1);
    const auto& ig = item_grid(1);
    for (int i = 0; i < 30; ++i) if (ig[std::size_t(i)] == 0x60) f[0x60] = 1;   // own the hammer
    BookModel m;
    m.flags = f; m.inv = &inv; m.items = &items; m.gi = &gi; m.character = 1;
    m.level = 1; m.hp = 60; m.max_hp = 60; m.atk = 46; m.def = 38;

    PauseBook b;
    b.open(m);
    BookEvents ev;
    auto step = [&](BookInput in) { ev = {}; b.update(in, m, ev); };
    for (int i = 0; i < 8; ++i) step({});
    {
        DrawList dl; b.draw(dl, m);
        check(dl.size() > 1 && dl[0].kind == DrawCmd::Pillars && dl[1].tex.kind == TexRef::Menu &&
              dl[1].tex.id == 0 && dl[1].flags == 0x20001,
              "the pillars, then the back plate, opaque (0x20001)");
        int down = 0, parked = 0;
        for (const DrawCmd& c : dl)
            if (c.kind == DrawCmd::Quad && c.h == 51.2f) { if (c.y == 0.0f) ++down; else if (c.y == -51.2f) ++parked; }
        check(down == 6 && parked == 4, "5 tabs + the cursor slid to y = 0; plaques 5..8 stay parked above");
    }
    step({-1, 0, false, false});
    check(b.page() == 4 && !ev.sounds.empty() && ev.sounds[0] == 3, "L from Equip wraps to Topic (pages 5..8 skipped)");
    step({+1, 0, false, false});
    check(b.page() == 0, "R from Topic wraps back to Equip");
    step({0, 0, true, false});
    check(b.sub_id() == 1 && ev.sounds == std::vector<int>{0}, "confirm opens Equip (subId 1)");
    for (int i = 0; i < 40; ++i) step({});
    // From the weapon cell 0: down -> 1 (the first armour).
    step({0, +1, false, false});
    step({0, 0, true, false});
    for (int i = 0; i < 80; ++i) step({});
    check(f[152] == eg[1], "Equip: confirm on armour cell 1 writes slot 152");
    step({+1, 0, false, false});              // -> cell 2 (count -1 or 0): buzzer
    step({0, 0, true, false});
    check(ev.sounds == std::vector<int>{2} || f[eg[2]] > 0, "Equip: an unowned cell buzzes");
    step({0, 0, false, true});
    check(b.sub_id() == 0, "cancel returns Equip to the tabs");

    // Item page: R then confirm.
    for (int i = 0; i < 40; ++i) step({});
    step({+1, 0, false, false});
    check(b.page() == 1, "R moves to the Item tab");
    step({0, 0, true, false});
    check(b.sub_id() == 2, "confirm opens Item (subId 2)");
    for (int i = 0; i < 40; ++i) step({});
    step({-1, 0, false, false}); step({});
    check(f[228] == 5, "Item: L from cell 0 wraps to cell 5 (toroidal)");
    step({0, -1, false, false}); step({});
    check(f[228] == 29, "Item: up from row 0 wraps to row 4");
    // Walk to the hammer and use it -> the popup -> Yes -> @UseHammer.
    int hammer = -1;
    for (int i = 0; i < 30; ++i) if (ig[std::size_t(i)] == 0x60) hammer = i;
    if (hammer >= 0) {
        while (f[228] % 6 != hammer % 6) { step({+1, 0, false, false}); step({}); }
        while (f[228] / 6 != hammer / 6) { step({0, +1, false, false}); step({}); }
        step({0, 0, true, false});
        check(b.sub_id() == 3 && inv.held == 0x60, "Item: the hammer is held and opens the Use popup");
        step({});
        step({0, 0, true, false});
        check(ev.script == "@UseHammer" && ev.closed, "Use popup: Yes starts @UseHammer and closes the book");
    }
    {   // The Load list (PAUSEBOOK_DRAW_SVLD_OPT.md §1.4, §2): slot 0 has a card, the rest are empty.
        std::vector<int> scanned;
        int sfn = 0;
        m.scan = [&](int slot) { scanned.push_back(slot); SlotCard c; c.status = slot == 0 ? 1 : 0; c.level = 9; return c; };
        m.save_file_no = &sfn;
        PauseBook L;
        L.open(m);
        auto lstep = [&](BookInput in) { ev = {}; L.update(in, m, ev); };
        for (int i = 0; i < 12; ++i) lstep({});
        lstep({+1, 0, false, false}); lstep({+1, 0, false, false});
        lstep({0, 0, true, false});
        check(L.page() == 2 && L.sub_id() == 6, "Load: the third tab opens the Load list (subId 6)");
        for (int i = 0; i < 30; ++i) lstep({});
        lstep({0, +1, false, false});
        lstep({0, 0, true, false});
        check(ev.sounds == std::vector<int>{2} && L.sub_id() == 6, "Load: an empty slot buzzes (sound 2)");
        lstep({0, -1, false, false});
        lstep({0, 0, true, false});
        check(L.sub_id() == 7, "Load: a card opens the Load dialog (subId 7)");
        for (int i = 0; i < 20; ++i) lstep({});
        lstep({0, 0, true, false});                              // the cursor starts on No
        check(L.sub_id() == 6 && ev.load_slot < 0, "Load dialog: Confirm on the initial No returns to the list");
        for (int i = 0; i < 40; ++i) lstep({});
        scanned.clear();
        lstep({+1, 0, false, false});
        check(scanned.size() == 8 && scanned[0] == 8, "Load: Right flips to file tab 2 (slots 8..15 rescanned)");
        lstep({-1, 0, false, false});
        lstep({0, 0, true, false});
        for (int i = 0; i < 20; ++i) lstep({});
        lstep({-1, 0, false, false});                            // No -> Yes
        lstep({0, 0, true, false});
        check(ev.load_slot == 0 && ev.closed && sfn == 0, "Load dialog: Yes loads the slot and closes the book");
    }
    {   // The Save list from the statue (ctx 1): an empty slot is written at once with sound 9.
        int sfn = 3;
        m.scan = [&](int) { return SlotCard{}; };
        m.save_file_no = &sfn;
        PauseBook S;
        S.open(m, 1);
        auto sstep = [&](BookInput in) { ev = {}; S.update(in, m, ev); };
        for (int i = 0; i < 4; ++i) sstep({});
        check(S.sub_id() == 4 && S.page() == 5, "Save: ctx 1 opens the Save list (subId 4, page 5)");
        sstep({0, 0, true, false});
        check(ev.save_slot == 3 && ev.sounds == std::vector<int>{9}, "Save: an empty slot (SaveFileNo 3) saves at once, sound 9");
        for (int i = 0; i < 40; ++i) sstep({});
        sstep({0, 0, false, true});
        check(ev.closed, "Save: Cancel closes the book");
    }
    {   // The Option page (§3.3).
        int sliders[3] = {512, 512, 512};
        bool checks[4] = {false, true, false, false};
        m.sliders = sliders; m.checks = checks;
        PauseBook O;
        O.open(m);
        auto ostep = [&](BookInput in) { ev = {}; O.update(in, m, ev); };
        for (int i = 0; i < 12; ++i) ostep({});
        for (int i = 0; i < 3; ++i) ostep({+1, 0, false, false});
        ostep({0, 0, true, false});
        check(O.sub_id() == 0x0b, "Option: the fourth tab opens the Option page (subId 0xb)");
        for (int i = 0; i < 30; ++i) ostep({});
        ostep({0, 0, true, false});
        check(O.sub_id() == 0x0c, "Option: Confirm on BGM enters its slider (subId 0xc)");
        for (int i = 0; i < 30; ++i) ostep({});
        BookInput hold; hold.held_right = true;
        for (int i = 0; i < 10; ++i) ostep(hold);
        check(sliders[0] == 592, "Option: a held Right steps the slider +8 per frame");
        ostep({0, 0, true, false});
        check(O.sub_id() == 0x0c, "Option: Confirm does not leave a slider");
        ostep({0, 0, false, true});
        check(O.sub_id() == 0x0b, "Option: Cancel leaves the slider");
        for (int i = 0; i < 30; ++i) ostep({});
        for (int i = 0; i < 3; ++i) ostep({0, +1, false, false});
        ostep({0, 0, true, false});
        check(checks[0] && ev.options_changed, "Option: Confirm on row 3 toggles the analog switch");
        ostep({0, -1, false, false}); ostep({0, -1, false, false}); ostep({0, -1, false, false}); ostep({0, -1, false, false});
        ostep({0, 0, true, false});                              // row 9 = Exit
        check(O.sub_id() == 0x10, "Option: Exit opens its message box (subId 0x10)");
        for (int i = 0; i < 20; ++i) ostep({});
        ostep({0, 0, true, false});                              // the cursor starts on No
        check(O.sub_id() == 0x0b && ev.option_quit < 0, "Option: No in the box returns to the page");
    }
    // The earlier blocks' locals are gone: drop the model's pointers to them.
    m.scan = nullptr; m.save_file_no = nullptr; m.sliders = nullptr; m.checks = nullptr;
    {   // The Topic page (PAUSEBOOK_DRAW_TOPIC_MAP.md §1-§4).
        std::uint8_t seen[256] = {};
        seen[116] = 1;                                           // Yunica, the first Character entry
        std::uint8_t manual[13] = {};
        const CastTable casts = CastTable::load(root / "MISC" / "CASTINFO.DAT");
        m.casts = &casts;
        m.topic_seen = seen;
        m.manual_on = manual;
        PauseBook T;
        T.open(m);
        check(manual[0] && manual[1] && manual[2] && !manual[3], "Manual: the builder enables pages 0..2");
        auto tstep = [&](BookInput in) { ev = {}; T.update(in, m, ev); };
        for (int i = 0; i < 12; ++i) tstep({});
        tstep({-1, 0, false, false});
        tstep({0, 0, true, false});
        check(T.page() == 4 && T.sub_id() == 0x12, "Topic: the fifth tab opens the category menu (0x12)");
        for (int i = 0; i < 30; ++i) tstep({});
        tstep({0, 0, true, false});
        check(T.sub_id() == 0x13, "Topic: Character opens the index sheet's bar (0x13)");
        for (int i = 0; i < 30; ++i) tstep({});
        tstep({+1, 0, false, false});
        tstep({0, 0, true, false});
        check(T.sub_id() == 0x14, "Topic: Select focuses the grid (0x14)");
        for (int i = 0; i < 40; ++i) tstep({});
        tstep({-1, 0, false, false});                            // 0 - 8 + 40 = 32, stepped back under 28 -> 24
        tstep({+1, 0, false, false});                            // 24 + 8 >= 28 -> the row's first column, 0
        tstep({0, 0, true, false});
        check(T.sub_id() == 0x15, "Topic: Confirm opens the datacard viewer (0x15)");
        for (int i = 0; i < 40; ++i) tstep({});
        DrawList dl;
        T.draw(dl, m);
        int texts = 0;
        for (const DrawCmd& c : dl) texts += c.kind == DrawCmd::TextGdi && c.diffuse == 0xFFC0C0C0;
        check(texts >= 1, "Topic: a seen entry draws its description (GDI, 0xffc0c0c0)");
        tstep({0, 0, false, true});
        check(T.sub_id() == 0x14, "Topic: Cancel returns from the viewer to the grid");
        for (int i = 0; i < 40; ++i) tstep({});
        tstep({0, 0, false, true});
        check(T.sub_id() == 0x13, "Topic: Cancel returns from the grid to the bar");
        for (int i = 0; i < 40; ++i) tstep({});
        tstep({0, 0, false, true});
        for (int i = 0; i < 40; ++i) tstep({});
        tstep({0, +1, false, false}); tstep({0, +1, false, false});
        tstep({0, 0, true, false});
        check(T.sub_id() == 0x16, "Topic: Manual opens the manual (0x16)");
        m.topic_seen = nullptr; m.manual_on = nullptr; m.casts = nullptr;
    }
    {   // The warp map (§6): the Crystal's Yes opens it; an unlocked point warps, a locked one buzzes.
        std::uint8_t warp[24] = {};
        warp[0] = 1;
        std::vector<std::string> started;
        f[0x54] = 1; inv.held = 0x54;
        m.warp_on = warp;
        m.start_script = [&](const std::string& n) { started.push_back(n); };
        int crystal = -1;
        for (int i = 0; i < 30; ++i) if (ig[std::size_t(i)] == 0x54) crystal = i;
        if (crystal >= 0) {
            f[228] = crystal;
            PauseBook W;
            W.open(m);
            auto wstep = [&](BookInput in) { ev = {}; W.update(in, m, ev); };
            for (int i = 0; i < 12; ++i) wstep({});
            wstep({+1, 0, false, false});
            wstep({0, 0, true, false});
            for (int i = 0; i < 30; ++i) wstep({});
            wstep({0, 0, true, false});                              // held + usable -> the popup
            for (int i = 0; i < 20; ++i) wstep({});
            wstep({0, 0, true, false});                              // Yes
            check(started == std::vector<std::string>{"@CheckWing"} && W.sub_id() == 3,
                  "Map: the Crystal's Yes runs @CheckWing and opens the map (subId stays 3)");
            for (int i = 0; i < 30; ++i) wstep({});
            wstep({0, -1, false, false});                            // rec 0 up -> 1 (locked)
            wstep({0, 0, true, false});
            check(ev.sounds == std::vector<int>{2}, "Map: a locked point buzzes (sound 2)");
            wstep({0, +1, false, false});                            // back to 0
            wstep({0, 0, true, false});
            check(f[137] == 0 && started.back() == "@UseWing", "Map: an unlocked point sets flag 137 and runs @UseWing");
            wstep({});
            check(ev.closed, "Map: the book closes the frame after");
        }
        m.warp_on = nullptr; m.start_script = nullptr;
    }
    {   // TITLE_DRAW.md §4.2: the title's Load opens page 6 with the gold Load plaque sliding in.
        PauseBook L;
        L.open(m, 2);
        BookEvents e2;
        for (int i = 0; i < 6; ++i) { e2 = {}; L.update({}, m, e2); }
        DrawList dl;
        L.draw(dl, m);
        bool gold = false;
        for (const DrawCmd& c : dl)
            if (c.kind == DrawCmd::Quad && c.tex.kind == TexRef::Menu && c.tex.id == 3 && c.y == 0.0f && c.v0 > 0.39f) gold = true;
        check(L.page() == 6 && L.sub_id() == 8 && gold, "ctx 2: page 6, the gold Load plaque at (128, 0)");
    }
    {   // §5: Character Select (ctx 4). Nothing cleared: Toal is unreachable; Yunica with no Extra
        // goes straight to Difficulty; Normal starts the game.
        PauseBook C;
        C.open(m, 4);
        auto cstep = [&](BookInput in) { ev = {}; C.update(in, m, ev); };
        for (int i = 0; i < 10; ++i) cstep({});
        check(C.page() == 8 && C.sub_id() == 0x17, "ctx 4: Character Select is page 8, subId 0x17");
        cstep({0, +1, false, false});
        cstep({0, +1, false, false});
        cstep({0, 0, true, false});
        check(C.sub_id() == 0x18, "down twice wraps past locked Toal back to Yunica; Confirm opens Difficulty");
        for (int i = 0; i < 60; ++i) cstep({});
        cstep({0, 0, true, false});                              // the default row: Normal
        bool started = false;
        NewGameInfo info;
        for (int i = 0; i < 30 && !started; ++i) { cstep({}); if (ev.new_game) { started = true; info = ev.new_game_info; } }
        check(started && info.character == 1 && info.difficulty == 0 && !info.easy && ev.closed,
              "Difficulty Normal: a new game as Yunica (0x76bb74 = 1), Normal; the book closes");
    }
    std::printf("[book] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
