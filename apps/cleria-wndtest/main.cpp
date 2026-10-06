// cleria-wndtest — the window text layout (src/cleria/richtext.hpp) and the event windows
// (src/cleria/wnddialog.hpp) against build/event_re/WNDDIALOG_DRAW.md.
// Usage: cleria-wndtest
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/emotion.hpp"
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
    std::printf("[wnd] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
