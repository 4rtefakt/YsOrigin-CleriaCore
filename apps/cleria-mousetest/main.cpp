// cleria-mousetest — the retail mouse (src/cleria/mouse.hpp) against build/shell_re/MOUSE.md: the
// cursor transform and the warp, the cursor manager's state machine and fades, the sprite quad,
// click-to-move, the skill switch (the wheel), the spawn warp and the menu hit tests.
// Usage: cleria-mousetest [ignored]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

#include "cleria/input_map.hpp"
#include "cleria/mouse.hpp"
#include "cleria/pausebook.hpp"
#include "cleria/player_actions.hpp"
#include "cleria/wnddialog.hpp"

using namespace cleria;
using namespace cleria::mouse;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[mouse] %-86s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static Display disp(int w, int h, bool windowed = false, int ox = 0, int oy = 0) {
    Display d;
    d.bb_w = w; d.bb_h = h; d.windowed = windowed; d.origin_x = ox; d.origin_y = oy;
    return d;
}

int main() {
    // ---- §1.1 the cursor transform [BYTES 0x5d2be8..0x5d2c55] ----
    {
        const Display d = disp(1024, 768);
        const Point a = cursor_from_client(0, 0, d), b = cursor_from_client(1024, 768, d);
        const Point c = cursor_from_client(512, 384, d), e = cursor_from_client(1023, 767, d);
        check(a.x == -48 && a.y == 0 && b.x == 1072 && b.y == 864, "1024x768: the client edges map to -48..1072 x 0..864");
        check(c.x == 512 && c.y == 432 && e.x == 1070 && e.y == 862, "centre (512, 384) -> (512, 432); (1023, 767) -> (1070, 862)");
        const Point w0 = cursor_from_client(0, 0, disp(1920, 1080)), w1 = cursor_from_client(1920, 1080, disp(1920, 1080));
        check(w0.x == -48 && w1.x == 1072 && w0.y == 0 && w1.y == 864, "1920x1080: the same over-scanned box (gains 1.09375 / 1.125)");
        const Point v0 = cursor_from_client(0, 0, disp(1280, 800)), v1 = cursor_from_client(1280, 800, disp(1280, 800));
        check(v0.x == -48 && v1.x == 1072 && v1.y == 864, "1280x800 (16:10): the same box");
        // Windowed: cx already holds the origin, so x reads 0.09375 * origin_x to the left; y has no term.
        const Point q = cursor_from_client(512, 384, disp(1024, 768, true, 64, 32));
        check(q.x == 506 && q.y == 432, "windowed at (64, 32): the centre reads 6 units left (the x quirk), y exact");
        const Point n = cursor_from_client(-100, -100, d);
        check(n.x < -48 && n.y < 0, "outside the client: past the box (GetCursorPos is global)");
    }
    // ---- §1.3 the warp is the inverse: round trips ----
    {
        int worst_c = 0, worst_u = 0;
        const Display ds[] = {disp(1024, 768), disp(1920, 1080), disp(1280, 800), disp(2560, 1440),
                              disp(1280, 720, true, 64, 48), disp(1600, 900, true, 300, 10)};
        for (const Display& d : ds) {
            for (int y = 0; y < d.bb_h; y += 37)
                for (int x = 0; x < d.bb_w; x += 41) {
                    const Point r = cursor_from_client(x, y, d);
                    const Point back = client_from_cursor(r.x, r.y, 0, d);
                    worst_c = std::max(worst_c, std::max(std::abs(back.x - x), std::abs(back.y - y)));
                }
            for (int Y = 0; Y < 864; Y += 29)
                for (int X = -48; X < 1072; X += 31) {
                    const Point c = client_from_cursor(X, Y, 0, d);
                    const Point r = cursor_from_client(c.x, c.y, d);
                    worst_u = std::max(worst_u, std::max(std::abs(r.x - X), std::abs(r.y - Y)));
                }
        }
        std::printf("        round trip: client -> record -> client worst %d px; record -> client -> record worst %d units\n",
                    worst_c, worst_u);
        // Each side truncates twice; at 1440 lines one unit is 1.875 px, so 3 is the floor of the error.
        check(worst_c <= 3 && worst_u <= 3, "warp(map(p)) ~ p and map(warp(X)) ~ X within the truncations (4:3, 16:9, 16:10, windowed)");
        // Mode 2 at A != 1 lands on the pillarboxed point (X in the 4:3 field).
        const Display d = disp(1920, 1080);
        const Point c = client_from_cursor(0, 384, 2, d);
        const Point r = cursor_from_client(c.x, c.y, d);
        check(std::abs(pillar_inv(r.x, d) - 0) <= 2, "mode 2 at 16:9: the warp goes to the 4:3 field (pillar_inv undoes it)");
        check(client_from_cursor(100, 100, 2, disp(1024, 768)).x == client_from_cursor(100, 100, 0, disp(1024, 768)).x,
              "mode 2 at 4:3 = mode 0");
    }
    // ---- the pillarbox helpers (FUN_005d3be0 / FUN_0057b3c0) ----
    {
        const Display d = disp(1920, 1080);
        check(near(float(aspect(d)), 1.3333334f) && aspect(disp(1024, 768)) == 1.0 && near(float(aspect(disp(1280, 800))), 1.2007504f),
              "A = W / ((4H)/3): 16:9 1.333, 4:3 1.0, 16:10 1280/1066");
        check(pillar_x(0.0, d) == 240.0 && pillar_x(1024.0, d) == 1680.0 && pillar_x(352.0, d) == 735.0,
              "X(v) at 1920x1080: 240 + v * 1440/1024 (X(352) = 735, the §5.1 band)");
        check(pillar_x(352.0, disp(1024, 768)) == 352.0, "X(v) at A = 1: v");
        // pillar_inv works on the record's full-width 1024 units: the 4:3 field spans 128..896 at 16:9.
        check(pillar_inv(128, d) == 0 && pillar_inv(896, d) == 1024 && pillar_inv(512, d) == 512 && pillar_inv(300, disp(1024, 768)) == 300,
              "pillar_inv at 16:9: 128 -> 0, 512 -> 512, 896 -> 1024; identity at 4:3");
    }

    // ---- §3 the cursor manager: menus ----
    auto rec_at = [](int x, int y, std::uint32_t held = 0, std::uint32_t dir = 0) {
        input::Frame f;
        f.x = x; f.y = y; f.held = held; f.dir = dir; f.magnitude = dir ? 1.0f : 0.0f;
        return f;
    };
    {
        CursorState s;
        Context c;
        c.menu = true;
        input::Frame f = rec_at(0, 0);
        s.step(f, c);
        check(!s.visible && s.alpha == 0.0f && s.colour == 0 && !s.queued(), "no motion: hidden, nothing queued");
        f = rec_at(300, 200);
        s.step(f, c);
        check(s.visible && s.mouse_mode && s.moved && near(s.alpha, 0.1f) && s.colour == 0x19FFFFFFu,
              "menu: any motion shows it (mouse mode, moved), alpha 0.1 = colour 0x19FFFFFF");
        int t = 1;
        while (s.alpha < 1.0f && t < 30) { f = rec_at(300, 200); s.step(f, c); ++t; }
        check(t == 10 && s.colour == 0xFFFFFFFFu && !s.moved, "fade-in: 10 ticks to alpha 1.0 (colour white); a still cursor is not 'moved'");
        f = rec_at(301, 201);
        s.step(f, c);
        check(!s.moved, "a 1-unit jitter is not motion (|d| > 1 needed)");
        f = rec_at(303, 201);
        s.step(f, c);
        check(s.moved && near(s.X, 303) && s.Xi == 303.0f && s.Yi == 201.0f, "2 units is motion; the draw anchor is floor(X)");
        f = rec_at(2000, 2000);
        s.step(f, c);
        check(s.X == 1072.0f && s.Y == 816.0f, "the drawn copy is clamped to [-48, 1072] x [-48, 816]");
    }
    // ---- §3 the field: MOUSE_DIR drags, the idle hide, events ----
    {
        CursorState s;
        Context c;                                   // the field
        input::Frame f = rec_at(100, 100);
        s.step(f, c);
        f = rec_at(400, 300);
        s.step(f, c);
        check(!s.visible && !s.mouse_mode && s.moved, "field: plain motion does nothing");
        f = rec_at(450, 300, input::kMouseDir | input::kClick);
        s.step(f, c);
        check(s.visible && s.mouse_mode && near(s.alpha, 0.1f), "field: motion with MOUSE_DIR held enters mouse mode and shows it");
        for (int i = 0; i < 100; ++i) { f = rec_at(450, 300); s.step(f, c); }
        check(s.visible && s.mouse_mode && s.alpha == 1.0f, "mouse mode never hides (100 still ticks)");
        f = rec_at(450, 300, 0, input::kUp);
        s.step(f, c);
        check(!s.mouse_mode && s.visible, "a key direction with no motion ends mouse mode");
        int hid = -1;
        for (int i = 1; i <= 80 && hid < 0; ++i) { f = rec_at(450, 300, 0, input::kUp); s.step(f, c); if (!s.visible) hid = i; }
        std::printf("        hidden after %d more ticks of keyboard play\n", hid);
        check(hid > 0 && hid <= 62, "then the idle timer hides it within ~62 ticks");
        int faded = 0;
        while (s.alpha > 0.0f && faded < 30) { f = rec_at(450, 300); s.step(f, c); ++faded; }
        // The alpha was already falling from the hide tick: count that tick too.
        check(faded + 1 == 13 && s.colour == 0x00FFFFFFu, "fade-out: 13 ticks from 1.0; the A = 0 tick still draws (0x00FFFFFF)");
        f = rec_at(450, 300);
        s.step(f, c);
        check(s.colour == 0 && !s.queued(), "the next tick: colour 0, not queued");
        // Events: a fix with no menu fades it out even in mouse mode.
        CursorState e;
        Context ce;
        ce.menu = true;
        f = rec_at(10, 10); e.step(f, ce);
        f = rec_at(100, 100); e.step(f, ce);
        for (int i = 0; i < 10; ++i) { f = rec_at(100, 100); e.step(f, ce); }
        ce.menu = false; ce.event = true;
        f = rec_at(100, 100); e.step(f, ce);
        check(e.visible && e.colour != 0xFFFFFFFFu && e.alpha < 1.0f, "an event (no menu): visible but fading out");
        ce.menu = true;
        f = rec_at(100, 100); e.step(f, ce);
        check(e.alpha > 0.9f, "an event under a menu window: shown");
        Context cd;
        cd.menu = true; cd.demo_hide = true;
        f = rec_at(500, 500); e.step(f, cd);
        check(!e.visible, "the attract demo hides it");
        // No logic step: nothing changes.
        CursorState z = e;
        Context cl = cd; cl.logic = false;
        f = rec_at(900, 700); z.step(f, cl);
        check(z.X == e.X && z.alpha == e.alpha, "no logic step: only the draw queue");
    }
    // ---- §2.3 the colour trick ----
    check(fade_colour(0.0f) == 0x00FFFFFFu && fade_colour(1.0f) == 0xFFFFFFFFu && fade_colour(0.5f) == 0x80FFFFFFu &&
          (fade_colour(0.92f) >> 24) == 235u, "colour = floor(alpha * 256) << 24 | white, 255 at 1.0");

    // ---- §4.1 click-to-move ----
    {
        // A cursor at (700, 360) (1024 units) and the player's anchor at back-buffer (512, 400), 1024x768.
        CursorState s;
        Context c;
        c.player = c.controlled = true;
        c.anchor_x = 512; c.anchor_y = 400; c.anchor_ok = true;
        input::Frame f = rec_at(600, 360);
        s.step(f, c);
        f = rec_at(700, 360, input::kMouseDir);
        bool wrote = s.step(f, c);
        const float want = std::atan2(400.0f - 360.0f, 700.0f - 512.0f);
        check(wrote && f.dir == input::kRight && f.magnitude == 1.0f && near(f.angle, want) && near(s.move_angle, want),
              "held MOUSE_DIR: dir bits (from the centre), magnitude 1 (run), angle player -> cursor (y-up)");
        f = rec_at(700, 360);
        check(s.step(f, c) && s.hold == 2, "release: still steering (hold 3 -> 2)");
        f = rec_at(700, 360);
        check(s.step(f, c) && s.hold == 1, "2nd tick after release: steering (hold 1)");
        f = rec_at(700, 360);
        check(!s.step(f, c) && f.dir == 0, "3rd tick: stopped (movement continues exactly 2 ticks after release)");
        // The stop radius: the anchor within 0.5 px of the cursor.
        c.anchor_x = 700; c.anchor_y = 360;
        f = rec_at(700, 360, input::kMouseDir);
        check(!s.step(f, c) && s.hold == 0, "within 0.5 px of the player: no write, hold cleared");
        c.anchor_x = 700.4f;
        f = rec_at(700, 360, input::kMouseDir);
        check(!s.step(f, c), "0.4 px: still inside the 0.25 squared radius");
        c.anchor_x = 701.0f;
        f = rec_at(700, 360, input::kMouseDir);
        check(s.step(f, c), "1 px away: steers again");
        // No controlled player / NaN projection: nothing.
        Context cn = c; cn.anchor_ok = false;
        f = rec_at(100, 100, input::kMouseDir);
        check(!s.step(f, cn) && f.dir == 0, "a NaN projection: no steer");
        // The direction bits: 8-way around the screen centre (y weighted 1.36), 4-way in book mode.
        check(steer_bits(1000, 360, true) == input::kRight && steer_bits(0, 360, true) == input::kLeft &&
              steer_bits(512, 0, true) == input::kUp && steer_bits(512, 760, true) == input::kDown,
              "8-way bits: right / left / up / down from (512, 360)");
        check(steer_bits(812, 140, true) == (input::kUp | input::kRight) && steer_bits(212, 580, true) == (input::kDown | input::kLeft),
              "8-way diagonals (y counts 1.36x)");
        check(steer_bits(800, 200, false) == input::kRight && steer_bits(600, 100, false) == input::kUp, "4-way");
        // The anchor heights.
        check(near(steer_anchor_y(1.075f, 1.075f, 1.075f), 0.15f) && near(steer_anchor_y(9.0f, 1.075f, 1.075f), 2.0f),
              "the anchor y: grounded = floor + 2h - 2 (feet + 0.15); 5.0 above the look point it follows");
        check(near(shadow_y(5.0f, 4.0f), 4.0f) && near(shadow_y(5.0f, 6.0f), 5.0f) && near(shadow_y(10.0f, 0.0f), 7.25f),
              "the arrow's anchor y: the floor clamped into [feet - 2.75, feet]");
    }
    // ---- §2.4 the field arrow's angle ----
    {
        check(near(draw_angle(612, 384, 512, 384, 9.0f), 0.0f) && near(draw_angle(512, 284, 512, 384, 9.0f), 1.5707964f) &&
              draw_angle(512, 384, 512, 384, 9.0f) == 9.0f, "draw angle: right 0, up +pi/2, unchanged at the player");
        const float a = draw_angle(612, 284, 512, 384, 0.0f);
        check(near(a, std::atan2(100.0f * 2.7210884f, 200.0f), 1e-4f), "the retail weights: dx x2, dy x2.721 (y stretched 1.36)");
    }
    // ---- §2.2 the sprite quad ----
    {
        CursorState s;
        s.Xi = 100; s.Yi = 200; s.colour = 0xFFFFFFFFu;
        const Quad q = cursor_quad(s, false, disp(1024, 768));
        check(q.x[0] == 85 && q.y[0] == 185 && q.x[1] == 85 && q.y[1] == 208 && q.x[2] == 108 && q.y[2] == 185 &&
              q.x[3] == 108 && q.y[3] == 208, "upright: (Xi-15, Yi-15) .. (Xi+8, Yi+8), 23 x 23 units");
        check(q.u[1] == 0.5625f && q.v[1] == 0 && q.u[2] == 0 && q.v[2] == 0.5625f && q.u[3] == 0.5625f,
              "UVs transposed (v1 below v0 has u = 0.5625), the 36-texel crop");
        const Quad w = cursor_quad(s, false, disp(2560, 1440));
        const float qw = w.x[2] - w.x[0], qh = w.y[1] - w.y[0];
        std::printf("        2560x1440: %g x %g px at x %g (the reference grab's template fits 43 x 43)\n", qw, qh, w.x[0]);
        check(qw == qh && (qh == 43 || qh == 44) && near(w.x[3] - w.x[1], qh), "widescreen: squared about its centre (43-44 px at 1440p)");
        const Quad n = cursor_quad(s, false, disp(1920, 1080));
        check(n.x[2] - n.x[0] == n.y[1] - n.y[0], "1920x1080: square");
        // The field look at drawAngle = 3pi/4 is the upright sprite.
        s.draw_angle = 2.3561945f;
        const Quad f = cursor_quad(s, true, disp(1024, 768));
        bool same = true;
        for (int i = 0; i < 4; ++i) same &= std::fabs(f.x[i] - q.x[i]) <= 1 && std::fabs(f.y[i] - q.y[i]) <= 1;
        check(same, "field look at drawAngle 3pi/4 = the upright quad");
        s.draw_angle = 0.0f;
        const Quad r = cursor_quad(s, true, disp(1024, 768));
        check(r.x[0] > 120 && std::fabs(r.y[0] - 200) <= 1, "drawAngle 0: the tip (v0) points right of the anchor");
        s.draw_angle = 1.5707964f;
        const Quad u = cursor_quad(s, true, disp(1024, 768));
        check(u.y[0] < 180 && std::fabs(u.x[0] - 100) <= 1, "drawAngle pi/2: the tip points up");
    }
    // ---- §4.3 the spawn warp ----
    {
        const Point a = spawn_cursor(0.0f), b = spawn_cursor(1.5707964f);
        check(a.x == 608 && a.y == 360 && b.x == 512 && b.y == 264, "spawn: 96 units from (512, 360) along the facing");
    }
    // ---- §4.3 the skill switch (FUN_0046cb00) ----
    {
        std::set<int> own = {0x74, 0x75, 0x76};
        auto owned = [&](int id) { return own.count(id) > 0; };
        SkillSwitch w;
        check(w.tick(120, 0, false, 0x74, owned) == 0x75 && w.cooldown == 15 && w.dir == 1,
              "wheel away (+120) = Previous Skill (+1): 0x74 -> 0x75, 15-tick cooldown");
        int fired = 0;
        for (int i = 0; i < 15; ++i) fired += w.tick(120, 0, false, 0x75, owned) != SkillSwitch::kNoRequest;
        check(fired == 0 && w.cooldown == 0, "the next 15 ticks' wheel is dropped");
        check(w.tick(-120, 0, false, 0x75, owned) == 0x74, "wheel toward (-120) = Next Skill (-1)");
        SkillSwitch v;
        check(v.tick(-240, 0, false, 0x74, owned) == 0x76, "-1 from 0x74 wraps to 0x76 (a two-notch delta is still one step)");
        SkillSwitch x;
        check(x.tick(60, 0, false, 0x74, owned) == SkillSwitch::kNoRequest && x.cooldown == 0, "|wheel| < 120 does nothing");
        check(x.tick(120, 0, true, 0x74, owned) == SkillSwitch::kNoRequest, "an attack press that started an action skips the switch");
        own = {0x74, 0x76};
        SkillSwitch k;
        check(k.tick(0, input::kPrevSkill, false, 0x74, owned) == 0x76, "the S key skips an unowned bracelet (0x75)");
        check(k.tick(120, 0, false, 0x74, owned) == SkillSwitch::kNoRequest,
              "the wheel cannot skip it: it falls to Skill 1 (0x74 = current: no request) [BYTES quirk]");
        check(k.tick(0, input::kSkill3, false, 0x74, owned) == 0x76 && k.tick(0, input::kSkill2, false, 0x74, owned) == -1,
              "Skill 3 -> 0x76; Skill 2 unowned -> -1 (unequip request)");
        check(k.tick(0, 0, false, -1, owned) == SkillSwitch::kNoRequest && k.request(0, 0, -1, owned) == 0x74,
              "none equipped (-1): Skill 1 when owned");
        SkillSwitch z;
        check(z.tick(-120, 0, false, 0xff, owned) == 0x76 && z.tick(120, 0, false, 0xff, owned) == SkillSwitch::kNoRequest,
              "0xff + wheel: 0x76 toward you; the cooldown then holds");
    }
    // ---- §5 the menu hit tests ----
    {
        const Display d = disp(1024, 768);
        // A choice box at cx 512, top 282, two items (n = 3 with the header), h = 2 * 38.4 + 12.8.
        const float h = 2 * 38.4f + 12.8f;
        check(wnddialog_row(512 + 16, 282 + 16, 512, 282, 200, h, 3, true, d) == 1, "row 1 at the box's top (tip = cursor - 16)");
        check(wnddialog_row(528, 282 + 16 + 31, 512, 282, 200, h, 3, true, d) == 1 &&
              wnddialog_row(528, 282 + 16 + 32, 512, 282, 200, h, 3, true, d) == 2, "rows every 38.4 from -6.4: 0..31 row 1, 32.. row 2");
        check(wnddialog_row(528, 282 + 15, 512, 282, 200, h, 3, true, d) == -1 &&
              wnddialog_row(528, 282 + 16 + int(h), 512, 282, 200, h, 3, true, d) == -1, "above / below the box: none");
        check(wnddialog_row(512 + 16 + 99, 300, 512, 282, 200, h, 3, true, d) == 1 &&
              wnddialog_row(512 + 16 + 100, 300, 512, 282, 200, h, 3, true, d) == -1 &&
              wnddialog_row(512 + 16 - 99, 300, 512, 282, 200, h, 3, true, d) == 1, "the band: 2|dx| < w about the centre");
        check(wnddialog_row(528, 282 + 16 + 72, 512, 282, 200, 400, 3, true, d) == -1, "row >= n: none");
        // 16:9: X(w) is back-buffer px (the §5.1 quirk): a 352 band reads 735 wide; the left gate mx >= 224.
        const Display w = disp(1920, 1080);
        check(wnddialog_row(512 + 16 + 300, 300, 512, 282, 352, h, 3, true, w) == 1 &&
              wnddialog_row(512 + 16 + 300, 300, 512, 282, 352, h, 3, true, d) == -1, "16:9: the band widens to X(w) (retail quirk)");
        check(wnddialog_row(223, 300, 512, 282, 1024, h, 3, true, w) == -1 && wnddialog_row(224, 300, 512, 282, 1024, h, 3, true, w) == 1,
              "16:9 path B: the left gate mx + 16 >= X(0) = 240");
        check(wnddialog_row(-1, 300, 0, 282, 1024, h, 3, false, d) == -1 && wnddialog_row(0, 300, 0, 282, 1024, h, 3, false, d) == 1,
              "path A: mx >= 0");
        // The csprite test: half-open, x-anchor 2 unprojects.
        check(csprite_hit(152.4f, 130, 731, 57, 2, 152, 130, d) == false && csprite_hit(152.4f, 130, 731, 57, 2, 153, 130, d) &&
              csprite_hit(152.4f, 130, 731, 57, 2, 883, 186, d) && !csprite_hit(152.4f, 130, 731, 57, 2, 884, 186, d) &&
              !csprite_hit(152.4f, 130, 731, 57, 2, 500, 187, d), "csprite: x <= p < x + w, y <= p < y + h");
        check(csprite_hit(152.4f, 130, 731, 57, 2, 278, 150, w) && !csprite_hit(152.4f, 130, 731, 57, 2, 160, 150, w) &&
              csprite_hit(152.4f, 130, 731, 57, 0, 160, 150, w), "x-anchor 2 at 16:9: the full-width point is unprojected first");
        const Point t = tip(400, 300);
        check(t.x == 384 && t.y == 284, "the hit point is the arrow tip (x - 16, y - 16)");
    }
    // ---- GoWndDialog (FUN_005789a0): hover on motion, click inside the hovered row ----
    {
        RichMetrics m;
        m.height = 32; m.ascent = 26; m.space = 10;
        m.advance = [](unsigned char) { return 16.0f; };
        auto statue = [&] {
            WndDialog d;
            d.menu(512, 282, "What will you do?", {"Save", "Accept divine blessing", "Nothing right now"},
                   {"@SaveMenu", "@GrowMenu", "@SaveBack"}, m, true);   // centred: top 320, h 128
            d.set_cancel(3);
            d.arm();
            return d;
        };
        std::vector<int> snd;
        WndDialog d = statue();
        for (int i = 0; i < 200; ++i) d.update({}, snd);
        snd.clear();
        WndInput click; click.click_pressed = true; click.mx = 528; click.my = 320 + 16 + 40;
        WndDialog e = d;
        e.update(click, snd);
        check(!e.accepted(), "a click with no hover does not confirm (retail needs the hover flag)");
        WndInput mv; mv.mouse_moved = true; mv.mx = 528; mv.my = 320 + 16 + 40;    // row 2
        d.update(mv, snd);
        check(d.cursor() == 2 && d.hover() && snd == std::vector<int>{3}, "motion over row 2: the cursor follows (SE 3), hover set");
        WndInput still = click;
        d.update(still, snd);
        check(d.accepted() && d.chosen() == "@GrowMenu", "click over the hovered row: confirms that row");
        WndDialog f = statue();
        for (int i = 0; i < 200; ++i) f.update({}, snd);
        WndInput off; off.mouse_moved = true; off.mx = 528; off.my = 100;
        f.update(off, snd);
        WndInput c2 = click; c2.my = 100;
        f.update(c2, snd);
        check(!f.hover() && !f.accepted() && f.cursor() == 1, "motion off the box clears hover; a click there does nothing");
        // Path A: interactive for the first time with mouse mode on: snaps to the row under the pointer.
        WndDialog g;
        g.menu(512, 282, "What will you do?", {"Save", "Accept divine blessing", "Nothing right now"},
               {"@SaveMenu", "@GrowMenu", "@SaveBack"}, m, true);
        WndInput mm; mm.mouse_mode = true; mm.mx = 528; mm.my = 320 + 16 + 80;     // row 3
        g.update(mm, snd);
        check(g.cursor() == 1, "path A waits for the window to become interactive (Wait_Prompt)");
        g.arm();
        g.update(mm, snd);
        check(g.cursor() == 3 && g.hover(), "path A: the first interactive tick in mouse mode snaps to row 3");
        // The item-get box: a click anywhere takes it.
        WndDialog t;
        t.treasure(512, 122, 0x54, "Received the Crystal.", m);
        t.arm();
        for (int i = 0; i < 400; ++i) t.update({}, snd);
        WndInput anywhere = click; anywhere.mx = 5; anywhere.my = 5;
        t.update(anywhere, snd);
        check(t.accepted(), "the item-get box: a click anywhere confirms (hover forced)");
    }
    // ---- §4.2 the double-click lunge (FUN_00465170 mouse mode + FUN_0046ea00) ----
    {
        PlayerActions pa;
        PlayerActions::In in;
        in.mouse_mode = true; in.actable = true; in.grounded = true; in.section = 2; in.facing = 0.0f;
        in.mouse_angle = 2.0f;
        in.double_click = true;
        pa.tick(in);
        check(pa.tap_state == 4 && pa.tap_window == 1 && near(pa.tap_angle, 2.0f), "a double click arms state 4 toward the cursor");
        in.double_click = false;
        int open = 1;
        for (int i = 0; i < 20; ++i) { pa.tick(in); if (pa.tap_window) ++open; }
        check(open == 14 && pa.tap_state == 0, "the window: the click tick + 13 (+0x718 past 12 ends it)");
        PlayerActions pb;
        in.double_click = true;
        pb.tick(in);
        in.double_click = false;
        in.jump_pressed = true;
        PlayerActions::Out o = pb.tick(in);
        check(!o.jump, "jump is suppressed inside the window");
        in.jump_pressed = false;
        in.attack_pressed = true;
        o = pb.tick(in);
        check(o.section == 0x112 && o.face && near(o.face_angle, 2.0f) && o.impulse && near(o.impulse_x, std::cos(2.0f) * 0.1f),
              "attack in the window: section 0x112 facing the cursor, the +0.1 impulse");
        PlayerActions pc;
        in.attack_pressed = false; in.mouse_mode = false;
        pc.tap_state = 4; pc.tap_window = 1;
        pc.tick(in);
        check(pc.tap_window == 0, "out of mouse mode a left-over state 4 opens nothing");
    }
    // ---- the pause book (§5.3): the statue's Save list ----
    {
        std::int32_t fl[0x260];
        for (std::int32_t& v : fl) v = -1;
        fl[150] = 1; fl[228] = 0;
        InvState inv;
        inv_new_game(inv);
        BookModel m;
        m.flags = fl; m.inv = &inv; m.character = 1;
        int sfn = 0;
        std::vector<int> scanned;
        m.scan = [&](int slot) { scanned.push_back(slot); return SlotCard{}; };
        m.save_file_no = &sfn;
        PauseBook S;
        S.open(m, 1);
        BookEvents ev;
        auto step = [&](const BookInput& in) { ev = {}; S.update(in, m, ev); };
        for (int i = 0; i < 40; ++i) step({});
        // Plate s = (152.4, 130 + 72 s, 731, 57), x-anchor 2; at 4:3 the tip is the cursor - 16.
        BookInput ck; ck.click = true; ck.mx = 500 + 16; ck.my = 130 + 72 * 5 + 10 + 16;
        step(ck);
        check(ev.save_slot < 0 && ev.sounds.empty(), "Save list: a click on plate 5 with row 0 selected does nothing");
        BookInput wh; wh.wheel = -120;
        step(wh);
        check(ev.warp && ev.warp_x == 883 && ev.warp_y == 187 + 72 && ev.warp_mode == 2 && ev.sounds == std::vector<int>{3},
              "wheel toward you: row 0 -> 1 (SE 3) and the warp to (883, 259), mode 2");
        step(wh);
        check(!ev.warp, "the next wheel tick is debounced (0x7d9be0)");
        for (int i = 0; i < 3; ++i) step({});
        scanned.clear();
        BookInput tab; tab.click = true; tab.mx = 210 + 80 * 2 + 40 + 16; tab.my = 64 + 20 + 16;
        step(tab);
        check(scanned.size() == 8 && scanned[0] == 16, "a click on file tab 3 flips the page (slots 16..23 rescanned)");
        BookInput mv; mv.moved = true; mv.mx = 500 + 16; mv.my = 130 + 72 * 3 + 10 + 16;
        step(mv);
        check(ev.sounds == std::vector<int>{3} && ev.save_slot < 0, "motion over plate 3: it takes the cursor (SE 3)");
        BookInput ck3 = mv; ck3.moved = false; ck3.click = true;
        step(ck3);
        check(ev.save_slot == 16 + 3 && sfn == 19, "a click inside the hovered plate saves slot 19 (page 2, row 3)");
    }
    // ---- the mapper carries the cursor and the raw wheel ----
    {
        input::Mapper m;
        input::Config cfg;
        cfg.assign = input::preset1();
        m.configure(cfg);
        input::Snapshot s;
        s.display = disp(1920, 1080);
        s.cursor_x = 960; s.cursor_y = 540; s.wheel = -120;
        const input::Frame& f = m.step(s);
        check(f.x == 512 && f.y == 431 && f.wheel == -120, "Mapper: Frame.x/y from the client position, the wheel raw (-120)");
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
