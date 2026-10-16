// cleria-wndtest — the window text layout (src/cleria/richtext.hpp) and the event windows
// (src/cleria/wnddialog.hpp) against build/event_re/WNDDIALOG_DRAW.md.
// Usage: cleria-wndtest
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/bonusshop.hpp"
#include "cleria/stageselect.hpp"
#include "cleria/emotion.hpp"
#include "cleria/eventsprites.hpp"
#include "cleria/extfont.hpp"
#include "cleria/richtext.hpp"
#include "cleria/wnddialog.hpp"
#include "cleria/wndtext.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[wnd] %-72s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    RichMetrics m;
    m.height = 32; m.ascent = 26; m.space = 10;
    m.advance = [](unsigned char) { return 16.0f; };

    {   // §6.2-6.3: the BATTLE_0 string after the unescape.
        const RichLayout l = layout_rich(wnd_unescape("Received\\n<color:0xfff2ea8c>Crystal<color:>."), m, 12);
        check(l.lines == 2 && near(l.height, 76) && near(l.width, 128), "two lines, pitch 44, height 76, width 128");
        bool colours = l.glyphs.size() == 16 && l.glyphs[8].colour == 0xfff2ea8c &&
                       l.glyphs[14].colour == 0xfff2ea8c && l.glyphs[15].colour == 0xFFFFFFFF &&
                       l.glyphs[0].colour == 0xFFFFFFFF;
        check(colours, "per-run colours: the item name 0xfff2ea8c, <color:> back to white");
        check(near(l.glyphs[8].y, 44) && near(l.glyphs[8].x, 0), "line 2 starts at x 0, y 44");
        check(l.counted == 16, "every visible glyph counted");
    }
    {   // nocount, scale, y.
        const RichLayout l = layout_rich("<nocount><scale:.8><y:-4>Ab<count>\n<y:8><scale:1.0>c d", m, 12);
        check(l.glyphs[0].index == 0 && l.glyphs[1].index == 0 && l.glyphs[2].index == 0 && l.counted == 2,
              "uncounted glyphs share the next counted index");
        check(near(l.glyphs[0].scale, 0.8f) && near(l.glyphs[1].x, 12.8f) && near(l.glyphs[0].y, -4),
              "<scale:.8> scales the advance; <y:-4> moves the pen");
        check(near(l.glyphs[2].y, -4 + 44 + 8), "<y:> persists across the line break");
        check(near(l.glyphs[3].x, 16 + 10), "a space advances by the space width");
        const RichLayout s = layout_rich(" x", m, 12);
        check(near(s.glyphs[0].x, 10), "a space at the line start puts the pen at 10");
    }
    {   // WINDOW_FONT.md §2-§3: the walk is in raster pixels, the measure in the 1024 x 768 frame. 2560 x 1440:
        // lfHeight 60, A = 2560 / 1920.
        RichMetrics w = m;
        w.screen_w = 2560; w.screen_h = 1440; w.height = 60; w.ascent = 52; w.space = 18;
        w.advance = [](unsigned char) { return 31.0f; };
        const RichLayout l = layout_rich("ab cd\n e<x:16>f", w, 12);
        check(near(l.glyphs[1].x, 31) && near(l.glyphs[2].x, 80) && near(l.glyphs[3].x, 111), "raster: raw advances, the space 18 px");
        check(near(l.glyphs[4].y, 60 + 22) && near(l.glyphs[4].x, 10),
              "a line feed is tmHeight + 12 x 1440 / 768 (22) px; the line-start space 10 raw px");
        check(near(l.glyphs[5].x, 10 + 31 + 40), "<x:16> = 16 x 2560 >> 10 = 40 px");
        check(near(l.width, 72), "measure: 4 x trunc(31 x A) + the unscaled space = 182 px -> 182 x 1024 / 2560 = 72");
        check(near(l.height, 75), "measure: (82 + 60) x 768 / 1440 = 75");
        // FUN_00578710: the rows every 38.4 from 6.4 whatever the font's height (33 at 768 lines); the box adds it.
        RichMetrics t = m;
        t.height = 33;
        WndDialog d;
        d.menu(512, 282, "T", {"a", "b", "c"}, {"", "", ""}, t, true);
        d.arm();
        std::vector<int> snd;
        for (int i = 0; i < 200; ++i) d.update({}, snd);
        DrawList dl;
        d.draw(dl);
        std::vector<float> ys;
        for (const DrawCmd& c : dl) if (c.kind == DrawCmd::TextRich) ys.push_back(c.y);
        const float T = (768.0f - (3 * 39.4f + 12.8f)) * 0.5f;
        check(ys.size() == 4 && near(ys[0], T - 44.8f) && near(ys[1], T + 6.4f) && near(ys[2], T + 44.8f) && near(ys[3], T + 83.2f),
              "rows at 6.4 + 38.4 x (i - 1); the box is 3 x (33 + 6.4) + 12.8 high");
    }
    {   // PLAYTEST_1_STORY.md §2: the Roo alphabet (tag 15) and the extfont cells (FUN_005f03f0 ext branch).
        check(roo_glyph('A', false) == 12 && roo_glyph('z', false) == 37 && roo_glyph('*', false) == 1 &&
              roo_glyph('#', false) == 7 && roo_glyph('!', false) == -1 && roo_glyph(' ', false) == -1,
              "roo: A-Z / a-z -> cells 12..37, * @ $ ^ { } # -> 1..7, '!' stays a letter");
        check(roo_glyph('A', true) == -1 && roo_glyph('$', true) == 3, "EnglishRoonic keeps the letters, not the symbols");
        RichMetrics e = m;
        e.ext_advance = [](int i) { return float(20 + i); };
        const RichLayout l = layout_rich("<roo>Hi! <ext:9>x", e, 12);
        check(l.glyphs.size() == 5 && l.glyphs[0].ext && l.glyphs[0].c == 19 && l.glyphs[1].ext && l.glyphs[1].c == 20 &&
              !l.glyphs[2].ext && l.glyphs[2].c == '!' && l.glyphs[3].ext && l.glyphs[3].c == 9 && l.glyphs[4].ext,
              "<roo>Hi! -> cells 19, 20, a '!', then <ext:9>");
        check(near(l.glyphs[1].x, 39) && near(l.glyphs[2].x, 79) && near(l.glyphs[3].x, 105) && l.counted == 5,
              "an extfont cell advances by left + right, unscaled; counted like a letter");
        // A 2 x 104 font (two-pixel cells): cell 0 lit in column 1 only, cell 1 empty.
        std::vector<std::uint8_t> bmp(54 + 1024 + 4 * 104, 0);
        auto put32 = [&](std::size_t o, std::uint32_t v) { for (int k = 0; k < 4; ++k) bmp[o + std::size_t(k)] = std::uint8_t(v >> (8 * k)); };
        bmp[0] = 'B'; bmp[1] = 'M'; put32(10, 54 + 1024); put32(14, 40); put32(18, 2); put32(22, 104);
        bmp[26] = 1; bmp[28] = 8;
        bmp[54 + 1024 + 4 * 103 + 1] = 16;                 // the top row (stored last), column 1
        const ExtFont f = ExtFont::parse(bmp);
        check(f.ok() && f.cell == 2 && f.cov[1] == 16 && f.left[0] == 0 && f.right[0] == 2 && f.left[1] == 1 &&
              f.right[1] == 2 && near(f.advance(0), 2), "extfont: bottom-up rows, left = 1 - first lit column, right = last + 1");
    }
    {   // §6.4: the reveal.
        RichGlyph g; g.index = 3;
        check(reveal_alpha(g, 0x40000) == 1.0f && near(reveal_alpha(g, 0x38000), 0.5f) && reveal_alpha(g, 0x20000) == 0.0f,
              "reveal: before = 1, at = the fraction, after = 0");
    }
    {   // §4: the item-get box.
        WndDialog d;
        d.treasure(512, 122, 0x54, "Received\\n<color:0xfff2ea8c>Crystal<color:>.", m);
        std::vector<int> snd;
        for (int i = 0; i < 20; ++i) d.update({}, snd);
        DrawList dl;
        d.draw(dl);
        const float w = 128 + 128, L = 512 - std::floor(w / 2);
        check(dl.size() >= 10 && near(dl[0].x, L - 80) && near(dl[0].y, 122 - 64), "DIALOG0 caps outside the inner rect; y is the top");
        const DrawCmd& ic = dl[9];
        check(ic.tex.kind == TexRef::Icon && ic.tex.id == 0x54 && near(ic.x, L + 19.2f) &&
              near(ic.y, 122 + (101.6f - 51.2f) / 2), "the icon 51.2 at (L + 19.2, T + (h - 51.2) / 2)");
        const DrawCmd& tx = dl[10];
        check(tx.kind == DrawCmd::TextRich && near(tx.x, L + 89.6f) && near(tx.y, 122 + 12.8f), "the text at (L + 89.6, T + 12.8)");
        check(snd.empty(), "the item-get box types without the tick sound");
        WndInput press; press.confirm_pressed = true;
        d.update(press, snd);
        check(!d.accepted(), "not accepted before Wait_Prompt arms it");
        d.arm();
        for (int i = 0; i < 400; ++i) d.update({}, snd);
        d.update(press, snd);
        check(d.accepted() && snd == std::vector<int>{0}, "armed + revealed: Confirm accepts with SE 0");
    }
    {   // §5: the statue choice box (3 items) centred like Menu_Select.
        WndDialog d;
        d.menu(512, 282, "What will you do?", {"Save", "Accept divine blessing", "Nothing right now"},
               {"@SaveMenu", "@GrowMenu", "@SaveBack"}, m, true);
        d.set_cancel(3);
        d.arm();
        std::vector<int> snd;
        for (int i = 0; i < 200; ++i) d.update({}, snd);
        DrawList dl;
        d.draw(dl);
        check(near(dl[0].y, 320 - 64), "h = 3 x 38.4 + 12.8 = 128, so T = 320 (statue_choice.png)");
        int ticks = 0;
        for (int s : snd) ticks += s == 0x0c;
        check(ticks > 0, "the menu header types with the tick sound");
        snd.clear();
        WndInput up; up.vstep = -1;
        d.update(up, snd);
        WndInput cancel; cancel.cancel_pressed = true;
        WndDialog e = d;
        e.update(cancel, snd);
        check(e.accepted() && e.chosen() == "@SaveBack", "Cancel picks the AssignCancel record");
        WndInput ok; ok.confirm_pressed = true;
        d.update(ok, snd);
        check(d.accepted() && d.chosen() == "@SaveBack" && snd.front() == 3, "Up from row 1 wraps to the last row");
    }
    {   // §3.2 step 3: an interactive dialog's caption reveals at 0x30000 a tick from the tick after Attack is held.
        auto reveal_ticks = [&](bool arm, bool attack) {
            WndDialog d;
            d.menu(512, 282, "What will you do?", {"Save", "Nothing right now"}, {"@a", "@b"}, m, true, true);
            if (arm) d.arm();
            WndInput in; in.attack_held = attack;
            std::vector<int> snd;
            for (int i = 0; i < 200; ++i) d.update(in, snd);
            int n = 0;
            for (int s : snd) n += s == 0x0c;
            return n;
        };
        const int slow = reveal_ticks(true, false), fast = reveal_ticks(true, true), idle = reveal_ticks(false, true);
        check(fast < slow && fast <= 2 && idle == slow, "dialog caption: Attack held speeds an armed dialog up, not an unarmed one");
    }
    {   // §2.4: the message box.
        WndText t;
        t.message(0x57, 1, 2, "Cecilia", "Hello\\n world", m);
        std::vector<int> snd;
        int opened = 0;
        for (int i = 1; i <= 20 && !opened; ++i) { t.update({}, snd); if (t.box_alpha() >= 1.0f) opened = i; }
        check(opened == 14, "the box fades in over 14 frames (0.075 / frame)");
        check(t.face() == 1 && t.face_frame() == 2, "class 0x57 -> face f_210, frame = op1");
        WndInput press; press.confirm_pressed = true; press.confirm_held = true;
        for (int i = 0; i < 100; ++i) t.update({}, snd);
        t.update(press, snd);
        check(!t.accepted(), "not accepted before Wait_Prompt arms it");
        t.arm();
        t.update({}, snd);
        t.update(press, snd);
        check(t.accepted(), "armed, revealed and released: a Confirm press accepts");
        int ticks = 0;
        for (int s : snd) ticks += s == 0x0c;
        check(ticks >= 1, "the tick SE 0x0c plays while typing");
        WndText p;
        p.message(0xd, 3, 0, "", "x", m);
        check(p.face() == 0x3c + 2, "the player (class 0xd) as Toal -> f_820");
    }
    {   // The emotion bubble (CHR_OPS.md §1.7 test vectors, kind 0, L = 60).
        std::vector<Emotion> list;
        spawn_emotion(list, 13, 0, 0.0f);
        Emotion& e = list.back();
        struct Row { int age, cell; float alpha; int a8; };
        const Row rows[] = {{1, 2, 1.0f / 15, 0}, {9, 2, 0.6f, 136}, {10, 3, 10.0f / 15, 153}, {15, 4, 1.0f, 238},
                            {20, 5, 1.0f, 255}, {60, 5, 1.0f, 255}, {61, 5, 0.96f, 255}, {84, 5, 0.04f, 20}};
        bool ok = true;
        for (int t = 1; t <= 84; ++t) {
            step_emotion(e);
            for (const Row& r : rows)
                if (r.age == t && (e.cell != r.cell || std::fabs(e.alpha - r.alpha) > 1e-4f || e.a8 != r.a8)) ok = false;
        }
        check(ok, "kind 0: the cells, the alpha ramp and the previous-tick vertex alpha");
        step_emotion(e);
        check(e.dead, "kind 0 dies at age L + 25 = 85");
        std::vector<Emotion> d;
        spawn_emotion(d, 7, 0, 0.0f);
        for (int t = 0; t < 30; ++t) step_emotion(d.back());
        spawn_emotion(d, 7, 1, 0.0f);
        check(d[0].age == 60, "a new bubble on the same actor jumps the older one to its fade-out");
        step_emotion(d[0]);
        check(d[0].age == 61 && std::fabs(d[0].alpha - 0.96f) < 1e-4f, "... and it fades from the next tick");
        const EmotionQuad q = emotion_quad(e);
        check(q.x0 == 0.0f && q.x1 == 0.75f && q.y1 == 0.75f, "kind 0: the 0.75 quad offset 0.375 to the right");
    }
    {   // §8.1: FUN_005d3be0 and FUN_0058b410's V_ card paths.
        check(map_coord(0, 2, 0.0, false, 1280, 800) == 107.0 && map_coord(0, 2, 1024.0, false, 1280, 800) == 1173.0,
              "mapper mode 2 at 1280x800: the 4:3 field is 107..1173 (W43 = 1066)");
        check(map_coord(0, 3, 1024.0, false, 1280, 800) == 1280.0, "mapper mode 3 of 1024 = W");
        check(map_coord(0, 2, 37.5, false, 1024, 768) == 37.5, "4:3: the value comes back untouched");
        check(map_coord(1, 2, 0.0, true, 1280, 720) == -120.0 && map_coord(1, 2, 768.0, true, 1280, 720) == 840.0,
              "fit at 16:9: the field takes the width, y is cropped (-120..840)");
        check(map_coord(0, 2, 1024.0, true, 1280, 720) == 1280.0, "fit at 16:9: x spans the full width");
        check(card_path("V_109", 0).kind == CardPath::Stretch, "V_109 stretches (no mapping)");
        check(card_path("v_301", 0).kind == CardPath::Cover, "V_30x covers");
        check(card_path("V_160", 0).pillars == 2, "V_160 draws the HugoPillar art");
        bool black = true;
        for (const char* n : {"V_150", "V_171", "V_200", "V_213", "V_320", "V_400", "V_402", "V_451"})
            black &= card_path(n, 0).pillars == 1 && card_path(n, 0).kind == CardPath::Field;
        check(black, "V_15x/17x/2xx/32x/400-402/45x draw black pillars");
        bool none = true;
        for (const char* n : {"V_100", "V_119", "V_180", "V_310", "V_403", "V_003a", "fin_01", "t_840a"})
            none &= card_path(n, 0).pillars == 0 && card_path(n, 0).kind == CardPath::Field && card_path(n, 0).mode == 2;
        check(none, "other V_ cards, fin and t_ at 0 <= x <= 100 take field mode 2");
        check(card_path("t_250a", -180).mode == 1 && card_path("t_280a", 280).mode == 3 &&
              card_path("t_860a", 80).mode == 3 && card_path("t_880a", 80).mode == 1 &&
              card_path("t_200a", 100).mode == 2, "t_ modes: x < 0 -> 1, x > 100 -> 3, t_860 / t_880");
        Busts bs;
        bs.size_of = [](const std::string&, int* w, int* h) { *w = 1024; *h = 1024; return true; };
        bs.open("V_109", 0, 1024);
        bs.open("V_200", 0, 1024);
        for (int i = 0; i < 60; ++i) bs.step();
        bs.priority("V_109", 3);
        DrawList dl;
        bs.draw(dl, 1280, 800);
        check(dl.size() == 4 && dl[0].anchor_x == 0 && dl[1].anchor_x == 2, "the backdrop (prio 3) first, stretched; the card over it");
        check(dl.size() == 4 && dl[2].anchor_x == 0x23D && dl[2].x == 0 && dl[2].w == 107 && dl[2].diffuse == 0 &&
                  dl[2].flags == 0x20001 && dl[3].x == 1173 && dl[3].w == 107,
              "V_200's pillars: opaque black 0..107 and 1173..1280");
        dl.clear();
        bs.draw(dl, 1024, 768);
        check(dl.size() == 2, "4:3: no pillars");
    }
    {   // The Bonus Shop (BONUS_MODES.md §4): the rows FUN_005c9d20 builds, the prices, a purchase.
        using CD = records::ClearData;
        auto labels = [](const CD& d) { std::vector<std::string> v; for (const ShopRow& r : shop_rows(d)) v.push_back(r.label); return v; };
        using V = std::vector<std::string>;
        CD d;
        d.clear = {1, 1, 1};
        check(labels(d) == V{"[5,000] Arena Stage 2", "[500,000] Adol (Felghana Version)", "Cancel"},
              "shop, nothing bought: Arena Stage 2, Adol (Felghana), Cancel");
        d.shop[CD::kAdolFelghana] = 1;
        check(labels(d) == V{"[5,000] Arena Stage 2", "[Unlocked] Adol (Felghana Version)", "[750,000] Yunica (Extra Version)",
                             "[750,000] Hugo (Extra Version)", "[750,000] Toal (Extra Version)", "Cancel"},
              "Felghana bought: the three Extras appear");
        d.shop[CD::kYunicaExtra] = d.shop[CD::kHugoExtra] = d.shop[CD::kToalExtra] = 1;
        check(labels(d).size() == 7 && labels(d)[5] == "[1,000,000] Adol (Napishtim Version)", "the three Extras bought: Adol (Napishtim)");
        for (int s = CD::kArena2; s <= CD::kArena6; ++s) d.shop[std::size_t(s)] = 1;
        check(labels(d)[0] == "[Unlocked] Arena Stages" && shop_rows(d)[0].kind == ShopRow::Unlocked,
              "stages 2-6 bought, no Nightmare x 3 / Hard + Normal + Easy x 3: the slot reads [Unlocked] Arena Stages");
        d.tier[4] = {1, 1, 1};
        check(labels(d)[0] == "[2,000,000] Arena Stage 7", "Nightmare x 3: Arena Stage 7 is offered");
        d.tier[4] = {1, 1, 0}; d.tier[1] = d.tier[2] = d.tier[3] = {1, 1, 1};
        check(labels(d)[0] == "[2,000,000] Arena Stage 7", "... or Hard, Normal and Easy x 3 each");
        d.shop[CD::kArena7] = d.shop[CD::kAdolNapishtim] = 1;
        check(labels(d).size() == 7 && labels(d)[0] == "[Unlocked] Arena Stages", "all bought, 11 of 15 tiers: no School Outfit row");
        d.clear = {0xD5, 0x8E, 0x89};
        check(labels(d).size() == 8 && labels(d)[6] == "[3,000,000] Yunica School Outfit", "the three Extra clear bytes: the School Outfit");
        d.clear = {1, 1, 1}; d.tier[0] = d.tier[4] = {1, 1, 1};
        check(labels(d).size() == 8, "... or all fifteen tiers");
        static const std::uint32_t kPrice[CD::kShopCount] = {500000, 1000000, 750000, 750000, 750000, 5000, 15000, 30000, 100000,
                                                             250000, 2000000, 3000000};
        bool prices = true;
        for (int i = 0; i < CD::kShopCount; ++i) prices = prices && shop_price(CD::Shop(i)) == kPrice[i];
        check(prices, "the twelve prices [BYTES 0x5c7b82..0x5c9a72]");

        // A purchase: Arena Stage 2 with 5,000 SP exactly (the compare is `jb`), then the same row again is Stage 3
        // with 0 SP: "Insufficient SP.".
        CD c;
        c.clear = {1, 1, 1};
        std::uint32_t sp = 5000;
        BonusShop shop;
        shop.open();
        ShopEvents ev;
        auto run = [&](int n) { for (int i = 0; i < n; ++i) { ShopEvents e; shop.update({}, c, sp, m, e); ev.bought |= e.bought; ev.closed |= e.closed; } };
        auto press = [&](bool cancel) {
            WndInput in; in.confirm_pressed = !cancel; in.cancel_pressed = cancel;
            ShopEvents e; shop.update(in, c, sp, m, e); ev.bought |= e.bought; ev.closed |= e.closed;
        };
        run(120);
        check(shop.is_open() && shop.rows().size() == 3, "the shop opens on its list");
        press(false); run(60);                                // Arena Stage 2 -> the confirm window
        check(!ev.bought && sp == 5000, "the row's handler opens the confirm window: nothing bought yet");
        press(false); run(120);                               // "OK to unlock this bonus?"
        check(ev.bought && sp == 0 && c.shop[CD::kArena2] == 1 && shop.rows()[0].label == "[15,000] Arena Stage 3",
              "confirm: 5,000 SP spent, 0x74ac0c = 1, the list rebuilt on Arena Stage 3");
        ev = {};
        press(false); run(60);                                // Arena Stage 3 with 0 SP: the OK box
        press(false); run(120);
        check(!ev.bought && sp == 0 && c.shop[CD::kArena3] == 0, "Insufficient SP.: an OK box, nothing bought");
        press(true); run(10);                                 // Cancel on the list
        check(ev.closed && !shop.is_open(), "Cancel (the list's cancel slot) closes the shop");
    }
    {   // The Time Attack / Arena stage select (STAGE_SELECT.md): the tables, the time text, the window, the panel.
        check(stages::time_attack_count(0) == 14 && stages::time_attack_count(1) == 13 && stages::time_attack_count(2) == 13 &&
              stages::time_attack_count(3) == 14, "stage counts 14 / 13 / 13 / 14 [BYTES 0x68c460]");
        check(std::string(stages::time_attack_name(2, 1)) == "Epona" && std::string(stages::time_attack_name(3, 5)) == "Shion" &&
              std::string(stages::time_attack_scene(3, 5)) == "S_3080" && std::string(stages::time_attack_scene(1, 6)) == "S_4013" &&
              std::string(stages::time_attack_scene(1, 0)) == "S_1080" && std::string(stages::arena_scene(6)) == "S_9159",
              "stage names and scenes per character [BYTES 0x68c4cc, 0x68cba4, 0x68c604]");
        check(stages::time_text(0) == "-#>3-#>1:#>2-#>3-#>1:#>2-#>3-#>3-#>1" && stages::time_text(59983) == "00:59:983" &&
              stages::time_text(3723004) == "1:02:03:004", "FUN_0057d1e0: the dash form, mm:ss:mmm, h:mm:ss:mmm");
        check(text_width_middle("00:59:983") == 114.0f && text_width_middle(stages::time_text(0)) == 133.0f,
              "F_MIDDLE widths: a time 114, the dash form 133 (its #>n shifts add 19)");
        records::RecordTable ta(records::RecordTable::kTimeAttackRows), ar(records::RecordTable::kArenaRows);
        ta.submit(1, 1, 2, 3600);                                                  // Yunica, Normal, Velagunder: 01:00:000, marked
        StageModel sm;
        sm.mode = 1; sm.character = 1; sm.column = 1; sm.picking = true; sm.ta = &ta; sm.ar = &ar;
        StageSelect ss;
        ss.set_target(1, true);
        check(ss.title_tint() == 0xFFFFFFFFu, "no overlay: the title is white");
        for (int i = 0; i < 20; ++i) ss.tick_fade();
        check(ss.fade(1) == 1.0f && ss.fade(2) == 0.0f && ss.title_tint() == 0xFFAFAFAFu, "20 ticks: fade 1, the title shaded to 0xAFAFAF");
        ss.open(0);
        auto step = [&](int v, bool ok, bool esc) {
            WndInput in; in.vstep = v; in.confirm_pressed = ok; in.cancel_pressed = esc;
            StageEvents e; ss.update(in, sm, e); return e;
        };
        check(step(-1, false, false).sounds == std::vector<int>{3} && ss.cursor() == 13, "up from row 0 wraps to Cancel (row 13 for Yunica)");
        step(1, false, false); step(1, false, false); step(1, false, false);
        check(ss.cursor() == 2, "down from Cancel wraps to row 0, then 1, 2");
        DrawList dl;
        ss.draw(dl, sm);
        auto find = [&](const std::string& t) -> const DrawCmd* { for (const DrawCmd& c : dl) if (c.text == t) return &c; return nullptr; };
        const DrawCmd* vela = find("Velagunder");
        const DrawCmd* rec = find("01:00:000");
        const DrawCmd* easy = find("Easy");
        check(dl[0].hgrad && dl[0].diffuse == 0xFFFFFFFFu && dl[0].diffuse_left == 0x00FFFFFFu && dl[0].u0 == 0.07f && dl[0].anchor_x == 0,
              "the panel's background: tarec_back over the whole frame, alpha 0 on its left edge");
        check(vela && vela->x == 76.8f && vela->y == 204.8f && vela->diffuse == 0xFFFFFF00u, "row 2 at (76.8, 140.8 + 64), yellow under the cursor");
        check(rec && rec->x == 572.8f - 114.0f && rec->y == 204.8f && (rec->diffuse & 0xFF00FF00u) == 0xFF00FF00u && (rec->diffuse & 0xFF) == ((rec->diffuse >> 16) & 0xFF),
              "its Normal time right-aligned at 572.8, in the new-record pulse");
        check(easy && easy->x == 309.8f && easy->y == 108.8f && easy->diffuse == 0xFF586868u && !find("Nightmare"),
              "the column headers: Easy dimmed beside the chosen Normal; no Nightmare column without data.nya");
        check(find("Cancel") && find("Cancel")->y == 140.8f + 32.0f * 13 && dl.back().kind == DrawCmd::Quad && dl.back().tex.kind == TexRef::Icon &&
              dl.back().tex.id == 4 && dl.back().x == 44.8f && dl.back().y == 204.8f, "Cancel under the 13 rows; the cursor icon (item 4) at (44.8, row y)");
        StageEvents e = step(0, true, false);
        check(e.launch == 2 && !e.back && !ss.is_open() && e.sounds == std::vector<int>{0}, "confirm on a stage: launch row 2, sound 0");
        ss.open(13);
        e = step(0, true, false);
        check(e.back && e.launch < 0 && !ss.is_open(), "confirm on Cancel: back");
        for (int i = 0; i < 20; ++i) ss.tick_fade();
        check(ss.fade(1) == 0.0f, "back drops the overlay's target: faded out after 20 ticks");
        // The Arena twin: rows from the bought stages, its own offsets.
        sm.mode = 2; sm.arena_rows = 3;
        ss.set_target(2, true); ss.tick_fade(); ss.open(1);
        dl.clear(); ss.draw(dl, sm);
        check(find("Flooded Prison") && find("Flooded Prison")->x == 56.8f && find("Flooded Prison")->y == 172.8f + 32.0f && !find("Silent Sands") &&
              find("Easy")->y == 140.8f && dl.back().x == 24.8f && dl.back().y == 172.8f + 32.0f,
              "the Arena panel: 3 rows at (56.8, 172.8 + 32 r), headers at y 140.8, the cursor icon at x 24.8");
        e = step(0, false, true);
        check(e.back && e.sounds.empty(), "cancel: back, no sound of its own");
    }
    {   // The run's clocks on the HUD (STAGE_SELECT.md §7; FUN_0043fe00's block, FUN_0043a340).
        records::RecordTable ta(records::RecordTable::kTimeAttackRows);
        ta.submit(1, 1, 0, 45259);                                                 // the Boss Rush's best: 12:34:316
        ta.submit(1, 1, 2, 3674);                                                  // stage 2's: 01:01:233
        RunHud h;
        h.mode = 1; h.pick = 0; h.character = 1; h.stage = 2; h.d = 1; h.stage_frames = 485; h.run_frames = 515; h.table = &ta;
        DrawList dl;
        run_hud(dl, h);
        auto is = [&](std::size_t i, const char* t, float x, float y, float s) {
            return i < dl.size() && dl[i].text == t && std::abs(dl[i].x - x) < 0.01f && dl[i].y == y && dl[i].scale == s && dl[i].anchor_x == 3;
        };
        check(dl.size() == 7 && is(0, "00:08:583", 908, 738, 1) && is(1, "/", 894, 738, 1) && is(2, "00:08:083", 782, 738, 1),
              "a Boss Rush's live line: the run's clock at 874 + 34, '/' 14 left of it, the stage's clock 160 left");
        check(is(3, "12:34:316", 942.8f, 718, 0.7f) && is(4, "/", 933, 718, 0.7f) && is(5, "01:01:233", 854.6f, 718, 0.7f),
              "the bests at 0.7: the Boss Rush's at 919 + 23.8, '/' 9.8 left, the stage's 112 left");
        check(dl[0].kind == DrawCmd::TextGeo && dl[6].kind == DrawCmd::TextMiddle && dl[6].text == "[Normal]" &&
              std::abs(dl[6].x - 886.8f) < 0.01f && dl[6].y == 688, "the tag [Normal] in F_MIDDLE at X2 - 56, y 688");
        h.pick = 2; h.d = 3; h.run_frames = 216000 + 485;                          // a single stage; an hour on the (unused) run clock
        dl.clear();
        run_hud(dl, h);
        check(is(0, "--:--:---", 914, 738, 1) && is(1, "/", 900, 738, 1) && is(2, "00:08:083", 788, 738, 1) && is(3, "--:--:---", 947, 718, 0.7f) &&
              is(4, "/", 937.2f, 718, 0.7f) && is(5, "00:00:000", 858.8f, 718, 0.7f) && dl[6].text == "[Nightmare]" && std::abs(dl[6].x - 850.4f) < 0.01f,
              "a single stage: the dashes at 914 / 947 for the run's items, an empty best prints 00:00:000");
        h.mode = 2; h.stage = 0; h.stage_frames = 216000 + 60;
        records::RecordTable ar(records::RecordTable::kArenaRows);
        h.table = &ar; h.d = 0;
        dl.clear();
        run_hud(dl, h);
        check(dl.size() == 4 && is(0, "00:00:000", 908, 738, 1) && is(1, "/", 894, 738, 1) && is(2, "01:00:01:000", 748, 738, 1) &&
              dl[3].text == "[Easy]" && std::abs(dl[3].x - 880.0f) < 0.01f, "the Arena's one line: the best right, the live clock left (hh: from an hour on, no shift)");
    }
    std::printf("[wnd] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
