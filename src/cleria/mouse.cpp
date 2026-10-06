// Retail mouse play and the game cursor: build/shell_re/MOUSE.md. See mouse.hpp.
#include "cleria/mouse.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "cleria/input_map.hpp"

namespace cleria::mouse {

namespace {
constexpr float kPi = 3.1415927410125732f;          // 0x6bd6ac
// atan2 as FUN_0066f80a (x87 fpatan on the two pushed doubles), rounded to the float the code stores.
float atan2f_x87(float y, float x) { return float(std::atan2(double(y), double(x))); }
}  // namespace

// ---- the display ------------------------------------------------------------------------------------

double aspect(const Display& d) {
    if (d.stretch) return 1.0;                       // FUN_005d3ab0's StretchAspect override [H: 1.0]
    const double w = double(d.bb_w);
    const double w43 = double((d.bb_h * 4) / 3);
    return w43 < w ? w / w43 : 1.0;
}

Point cursor_from_client(int client_x, int client_y, const Display& d) {
    const int W = d.bb_w > 0 ? d.bb_w : 1024, H = d.bb_h > 0 ? d.bb_h : 768;
    int ox = 0, oy = 0, cx = W >> 1;                 // FUN_005d62b0
    int sx = client_x, sy = client_y;                // GetCursorPos
    if (d.windowed) {
        ox = d.origin_x; oy = d.origin_y;
        cx += d.origin_x;                            // ClientToScreen(&cx): the origin is in cx too
        sx += d.origin_x; sy += d.origin_y;
    }
    Point p;
    p.x = (((sx - ox - cx) * 0x230) / 512 + cx) * 1024 / W;   // [BYTES 0x5d2bef..0x5d2c1f]
    p.y = (((sy - oy) * 0x1b0) / 384) * 768 / H;              // [BYTES 0x5d2c21..0x5d2c55]
    return p;
}

Point client_from_cursor(int X, int Y, int mode, const Display& d) {
    const int W = d.bb_w > 0 ? d.bb_w : 1024, H = d.bb_h > 0 ? d.bb_h : 768;
    int ox = 0, oy = 0, cx = W >> 1;
    if (d.windowed) { ox = d.origin_x; oy = d.origin_y; cx += d.origin_x; }
    const int px = (mode == 0 || aspect(d) == 1.0) ? (W * X) / 1024 : int(pillar_x(double(X), d));
    Point p;
    p.x = ((px - cx) * 0x200) / 0x230 + ox + cx;     // SetCursorPos (screen)
    p.y = (((H * Y) / 0x300) * 0x180) / 0x1b0 + oy;
    if (d.windowed) { p.x -= d.origin_x; p.y -= d.origin_y; }   // back to client
    return p;
}

double pillar_x(double v, const Display& d) {
    if (d.stretch || aspect(d) == 1.0) return v;     // 0x5d3c49: the input back
    const int W = d.bb_w, H = d.bb_h;
    double w43 = double((H * 4) / 3);
    if (w43 > double(W)) w43 = double(W);            // narrower than 4:3 (unreachable with A != 1)
    if (w43 == 1024.0) return double(int(v)) + (double(W) - w43) * 0.5;   // 0x5d3d30
    return double(int(v / 1024.0 * w43 + (double(W) - w43) * 0.5));        // 0x5d3dca
}

int pillar_inv(int x, const Display& d) {
    const double A = aspect(d);
    if (A == 1.0) return x;
    const double p = ((A * 1024.0 - 1024.0) * 0.5) / (A * 1024.0) * 1024.0;
    return int(((double(x) - p) / ((1024.0 - p) - p)) * 1024.0);
}

// ---- the cursor manager -----------------------------------------------------------------------------

std::uint32_t fade_colour(float alpha) {
    const float f = alpha + 1.0f;                    // 0x426653: + 1.0, then the bits
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return (0u - (u >> 30)) | (u << 9) | 0xffffffu;
}

float draw_angle(float X, float Y, float Px, float Py, float prev) {
    const float dy = (Y - Py) * 1024.0f / 376.32000732421875f;    // 0x6bde70, 0x6bdc7c
    const float dx = (X - Px) * 1024.0f * 0.001953125f;           // 0x6bd008
    if (dy * dy + dx * dx > 0.0f) return atan2f_x87(-dy, dx);
    return prev;
}

std::uint32_t steer_bits(float X, float Y, bool eight_way) {
    static constexpr std::uint8_t kQuant4[4] = {2, 1, 4, 8};             // 0x6b7380
    static constexpr std::uint8_t kQuant8[8] = {2, 3, 1, 5, 4, 0xc, 8, 0xa};   // 0x6b7384
    const float dy = Y - 360.0f, dx = X - 512.0f;                         // 0x6bdc74, 0x6bdcfc
    const float c = atan2f_x87(-(dy * 2.721088409423828f), dx + dx);     // 0x6bd650
    if (eight_way) {
        const float q = ((c + kPi) + 0.39269909262657166f) * 1.2732394933700562f;
        return kQuant8[int(std::floor(q)) & 7];
    }
    const float q = ((c + kPi) + 0.7853981852531433f) * 0.6366197466850281f;
    return kQuant4[int(std::floor(q)) & 3];
}

float steer_anchor_y(float origin_y, float smoothed_230, float h) {
    float base = smoothed_230 + h;
    const float d = origin_y - base;
    if (d > 5.0f) base += d - 5.0f;                  // 0x6bd868
    else if (-h > d) base += d - (-h);
    return base - 2.0f;                              // 0x6bd580
}

float shadow_y(float feet_y, float floor_318) {
    if (floor_318 > feet_y) return feet_y;
    const float lo = feet_y - 2.75f;                 // 0x6bd654
    return lo > floor_318 ? lo : floor_318;
}

void CursorState::place(const Display& d) {
    // FUN_00426c70: s = back buffer / client; the clamp box 0x73263c = {-48, -48, 1120, 864} / s.
    const float sx = d.client_w > 0 ? float(d.bb_w) / float(d.client_w) : 1.0f;
    const float sy = d.client_h > 0 ? float(d.bb_h) / float(d.client_h) : 1.0f;
    const float lox = -48.0f / sx, hix = (-48.0f + 1120.0f) / sx;
    const float loy = -48.0f / sy, hiy = (-48.0f + 864.0f) / sy;
    if (lox > fx) fx = lox; else if (fx > hix) fx = hix;
    X = fx * sx;
    Xi = float(std::floor(double(X)));
    if (loy > fy) fy = loy; else if (fy > hiy) fy = hiy;
    Y = fy * sy;
    Yi = float(std::floor(double(Y)));
}

bool CursorState::step(input::Frame& rec, const Context& c, const Display& d) {
    menu = c.menu;
    event = c.event;
    if (!c.logic) return false;                      // 0x42641c: only the draw is queued
    fx = float(rec.x);
    fy = float(rec.y);
    place(d);
    // Motion: |dx| > 1 or |dy| > 1 on the record ints [BYTES 0x426453..0x4264c9].
    if (std::abs(rec.x - prev_x) <= 1 && std::abs(rec.y - prev_y) <= 1) {
        if (rec.dir != 0) mouse_mode = false;        // a keyboard / pad direction takes over
        moved = false;
    } else {
        if (menu) { visible = true; mouse_mode = true; }
        else if (rec.held & 0x1000u) mouse_mode = true;   // the field: a drag with MOUSE_DIR held
        moved = true;
    }
    prev_x = rec.x;
    prev_y = rec.y;
    // The idle hide [BYTES 0x4264db..0x426529].
    if (idle == 0) {
        if (rec.dir != 0 || rec.magnitude != 0.0f || (rec.held & 0xfffeffffu)) idle = 1;
    } else if (idle <= 0x3d) {
        ++idle;
    } else {
        visible = false;
    }
    if (mouse_mode && !visible) { idle = 0; visible = true; }
    if (c.demo_hide) visible = false;
    // The vestigial arrow channel [BYTES 0x42656b..0x4265b0].
    if (!menu && !event && (rec.held & 0x1000u)) { arrow_t = 0; arrow = true; }
    else if (arrow_t <= 0x24) ++arrow_t;
    else arrow = false;
    aux_angle += 0.1f;
    // The fades: +0.1 in, -0.08 out (0x6bd19c / 0x6bd180) [BYTES 0x4265e1..0x426673].
    auto fade = [](bool on, float& a, std::uint32_t& col) {
        if (!on) {
            col = 0;
            if (!(a > 0.0f)) return;
            a -= 0.07999999821186066f;
        } else {
            col = 0xffffffffu;
            if (!(a < 1.0f)) return;
            a += 0.10000000149011612f;
        }
        if (0.0f > a) a = 0.0f;
        else if (a > 1.0f) a = 1.0f;
        col = fade_colour(a);
    };
    fade(visible && (menu || !event), alpha, colour);
    fade(arrow, arrow_alpha, arrow_colour);
    if (alpha == 0.0f) return false;                 // 0x426704
    // The centre angle (no weights) [BYTES 0x42670a..0x42679c]: overwritten below with the player
    // angle when there is one; what moveAngle holds in a menu or with no player (MOUSE.md §9 new).
    {
        const float dx = X - 512.0f, dy = Y - 360.0f;
        if (dy * dy + dx * dx > 0.0f) move_angle = atan2f_x87(-dy, dx);
    }
    if (c.player) draw_angle = mouse::draw_angle(X, Y, px, py, draw_angle);
    if (rec.held & 0x1000u) hold = 3;
    else if (hold != 0) --hold;
    if (!c.controlled || !c.anchor_ok) return false; // 0x426842, the __isnan tests
    // Click-to-move [BYTES 0x426967..0x426b4d]: both points in back-buffer pixels.
    const float cX = float(d.bb_w) * X * 0.0009765625f;
    const float cY = float(d.bb_h) * Y / 768.0f;
    const float dxp = c.anchor_x - cX, dyp = c.anchor_y - cY;
    const float d2 = dyp * dyp + dxp * dxp;
    float use = move_angle;
    if (d2 != 0.0f) {
        const float ang = atan2f_x87(dyp, -dxp);     // player -> cursor, y-up
        aux_angle = ang;
        if (colour != 0) { move_angle = ang; use = ang; }
    }
    if (0.25f > d2) { hold = 0; return false; }      // within 0.5 px: stop (0x6bd244)
    if (hold == 0) return false;
    rec.angle = use;
    rec.magnitude = 1.0f;
    rec.dir = steer_bits(X, Y, c.eight_way);
    return true;
}

// ---- the sprite -------------------------------------------------------------------------------------

Quad cursor_quad(const CursorState& s, bool field, const Display& d) {
    Quad q;
    q.colour = s.colour;
    // The UVs [BYTES 0x426f9f..0x427060]: v1 (0.5625, 0) sits BELOW v0: transposed.
    static constexpr float kU[4] = {0, 0.5625f, 0, 0.5625f}, kV[4] = {0, 0, 0.5625f, 0.5625f};
    for (int i = 0; i < 4; ++i) { q.u[i] = kU[i]; q.v[i] = kV[i]; }
    if (field) {
        // [BYTES 0x42707e..0x427113]: a = drawAngle - pi/4; x = fx*s - fy*c + Xi, y = fx*c + fy*s + Yi.
        static constexpr float kC[4][2] = {{-15, -15}, {-15, 8}, {8, -15}, {8, 8}};   // 0x6981e4
        const double a = double(s.draw_angle - 0.7853981852531433f);
        const float c = float(std::cos(a)), sn = float(std::sin(a));
        for (int i = 0; i < 4; ++i) {
            q.x[i] = kC[i][0] * sn + s.Xi - kC[i][1] * c;
            q.y[i] = kC[i][0] * c + s.Yi + kC[i][1] * sn;
        }
    } else {
        // [BYTES 0x427117..0x427171]: (Xi-15, Yi-15), (Xi-15, Yi+8), (Xi+8, Yi-15), (Xi+8, Yi+8).
        const float l = s.Xi - 15.0f, r = s.Xi + 8.0f, t = s.Yi - 15.0f, b = s.Yi + 8.0f;
        q.x[0] = l; q.y[0] = t; q.x[1] = l; q.y[1] = b; q.x[2] = r; q.y[2] = t; q.x[3] = r; q.y[3] = b;
    }
    // FUN_005e4100(4, verts, 0x1c): 1024x768 -> back-buffer pixels.
    const float kx = float(d.bb_w) / 1024.0f, ky = float(d.bb_h) / 768.0f;
    for (int i = 0; i < 4; ++i) { q.x[i] = std::floor(q.x[i] * kx); q.y[i] = std::floor(q.y[i] * ky); }
    // A != 1: square about the centre on the vertex slots [BYTES 0x427187..0x4271ed].
    if (aspect(d) != 1.0) {
        const float w = q.x[2] - q.x[0], h = q.y[1] - q.y[0];
        const float dd = (w - h) * 0.5f;
        q.x[0] += dd;
        q.x[1] += dd;
        q.x[2] = q.x[0] + h;
        q.x[3] = q.x[1] + h;
    }
    return q;
}

Point spawn_cursor(float theta) {
    const double a = double(-theta);
    Point p;
    p.y = int(float(std::sin(a)) * 96.0f + 360.0f);   // 0x6bdae8, 0x6bdc74
    p.x = int(float(std::cos(a)) * 96.0f + 512.0f);   // 0x6bdcfc
    return p;
}

// ---- menus ------------------------------------------------------------------------------------------

bool csprite_hit(float x, float y, float w, float h, int anchor_x, int px, int py, const Display& d) {
    if (anchor_x == 2) px = pillar_inv(px, d);
    return x <= float(px) && y <= float(py) && float(px) < w + x && float(py) < h + y;
}

int wnddialog_row(int mx, int my, float cx, float top, float w, float h, int n, bool path_b, const Display& d) {
    const int dy = int(float(my) - top) - 16;
    if (dy < 0 || dy >= int(h)) return -1;
    if (path_b) { if (double(mx + 16) < pillar_x(0.0, d)) return -1; }
    else if (mx < 0) return -1;
    const int xw = int(pillar_x(double(w), d));
    const int dx = int(float(mx) - cx) - 16;
    if (std::abs(dx) * 2 >= xw) return -1;
    const int row = 1 - int((float(dy) + 6.400000095367432f) / -38.400001525878906f);
    if (row < 1 || row >= n) return -1;
    return row;
}

}  // namespace cleria::mouse
