// Retail mouse play and the game cursor: the cursor transform of the mapper FUN_005d2ba0 and its
// inverse (the warp FUN_005d33f0), the cursor manager FUN_004263c0 (visibility, the fades, mouse
// mode, the field arrow's angle and click-to-move), the sprite quad FUN_00426f20, the spawn warp
// FUN_00426bc0, the skill switch FUN_0046cb00 (the wheel and the skill bits), and the menu hit tests
// (GoWndDialog FUN_005789a0, the csprite test FUN_005a4760, the inverse pillarbox FUN_0057b3c0).
// Pure: no SDL, no OS. Spec: build/shell_re/MOUSE.md (every constant re-read from yso_win.exe with
// tools/exe_peek.py; see mouse.cpp). Provenance: [BYTES] disassembly, [EXPORT] Ghidra export,
// [H] hand-chosen, [not modelled].
//
// Units: the "record" cursor (input::Frame::x / y, the record's +0x00 / +0x04) is in an over-scanned
// 1024x768 space; the sprite is drawn in 1024x768 units mapped to the back buffer; the player's
// projected position and the click-to-move anchor are in back-buffer pixels (MOUSE.md §2.4).
#pragma once
#include <cstdint>

namespace cleria::input { struct Frame; }

namespace cleria::mouse {

// ---- the display the cursor maps through ---------------------------------------------------------
struct Display {
    int bb_w = 1024, bb_h = 768;    // [graphics] BackBufferWidth/Height: 0x7035c4/c8 (the mapper's
                                    // divisors), 0x765100/04, PP 0x765148/4c (one value in CleriaCore)
    bool windowed = false;          // 0x764882 PP.Windowed: the x quirk of §1.1
    int origin_x = 0, origin_y = 0; // the client area's screen origin (ClientToScreen), windowed only
    bool stretch = false;           // 0x765150 [graphics] StretchAspect: A = 1 [not modelled: always 0]
    // GetClientRect (FUN_00426c70): s = back buffer / client. 0 = the back-buffer size (s = 1).
    int client_w = 0, client_h = 0;
};
// 0x765178 = A (FUN_005d3ab0): W / ((4H)/3) when wider than 4:3, else 1.0 [EXPORT].
double aspect(const Display& d);

struct Point { int x = 0, y = 0; };
// §1.1 [BYTES 0x5d2be8..0x5d2c55]: GetCursorPos (here: the client position + the windowed origin)
// to the record ints. All int32 with C truncation:
//   x = (((sx - ox - cx) * 560) / 512 + cx) * 1024 / W,   y = (((sy - oy) * 432) / 384) * 768 / H
// cx = W/2 (+ origin when windowed, FUN_005d62b0), ox = oy = origin when windowed, else 0.
Point cursor_from_client(int client_x, int client_y, const Display& d);
// §1.3 [EXPORT FUN_005d33f0]: the SetCursorPos target of a warp to record (X, Y), back in client
// pixels. mode 0 = the full-width stretch (W*X/1024); mode != 0 with A != 1 = the pillarbox
// FUN_005d3be0(axis 0, 2, X).
Point client_from_cursor(int X, int Y, int mode, const Display& d);

// FUN_005d3be0(axis 0, mode 2, v) [BYTES 0x5d3be0..0x5d3e20]: an authored x of the pillarboxed 4:3
// field in back-buffer pixels: trunc(v / 1024 * W43 + (W - W43) / 2). Returns v itself when A == 1
// or StretchAspect (the function's early return).
double pillar_x(double v, const Display& d);
// FUN_0057b3c0 / FUN_005a4760's x-anchor-2 branch [EXPORT]: a full-width x back into the authored
// 4:3 field: p = 512(A-1)/A, trunc((x - p) / (1024 - 2p) * 1024). Identity when A == 1.
int pillar_inv(int x, const Display& d);

// ---- the cursor manager FUN_004263c0 (MOUSE.md §3, §4.1) -----------------------------------------
struct Context {
    bool logic = true;          // 0x74c0f0: a real logic step (else only the draw is queued)
    bool menu = false;          // 0x7325e4: a GoWndZzz / GoWndMain-dialog / GoWndShop2 / book window is alive
    bool event = false;         // 0x7325e8: a script fix / lock, or a GoWndText
    bool demo_hide = false;     // *0x7302b4 + 0x280 == 1 (the attract demo)
    bool player = false;        // 0x74c09c: a player actor (the field look, the draw angle)
    bool controlled = false;    // 0x749d44: the controlled player (click-to-move)
    // The click-to-move anchor (§4.1): the controlled player's (x, y', z) through 0x796170, in
    // back-buffer pixels. anchor_ok = false for a NaN projection (the steer is skipped).
    float anchor_x = 0, anchor_y = 0;
    bool anchor_ok = false;
    bool eight_way = true;      // 0x7035c0 == 8 (else 4-way: the pause book's mapper mode)
};

struct CursorState {
    float px = 0, py = 0;       // 0x7325c8/cc: the player's screen position (FUN_00473540; the host sets it)
    bool mouse_mode = false;    // 0x7325d4
    bool moved = false;         // 0x7325d8: the record moved >= 2 units this tick (the menus' hover gate)
    bool visible = false;       // 0x7325dc: the target visibility
    bool arrow = false;         // 0x7325e0: the vestigial arrow channel (only gates the draw queue)
    bool menu = false;          // 0x7325e4 (latched each step)
    bool event = false;         // 0x7325e8
    std::uint32_t arrow_colour = 0;   // 0x7325ec
    std::uint32_t colour = 0;   // 0x7325f0: the vertex colour (A = floor(alpha * 256), RGB white)
    float arrow_alpha = 0;      // 0x7325f4
    float alpha = 0;            // 0x7325f8
    float move_angle = 0;       // 0x7325fc: player -> cursor, y-up (the mover's angle in mouse mode, the lunge)
    float draw_angle = 0;       // 0x732600: the field sprite's rotation
    float aux_angle = 0;        // 0x732604: += 0.1, then the last steer angle; no reader [EXPORT globals]
    int hold = 0;               // 0x732608: 3 while MOUSE_DIR is held, counts down
    int idle = 0;               // 0x73260c: the hide timer
    int arrow_t = 0;            // 0x732610
    float fx = 0, fy = 0;       // 0x73261c/20: the clamped float copy (1024 units / s)
    float X = 0, Y = 0;         // 0x732624/28: the drawn cursor (1024x768 units)
    float Xi = 0, Yi = 0;       // 0x73262c/30: floor(X), floor(Y), the draw anchor
    int prev_x = 0, prev_y = 0; // 0x73265c/60: the last record ints

    // One call per frame (FUN_004281c0), BEFORE the player tick. `rec` is the frame's record: the
    // click-to-move writes rec.dir / angle / magnitude (§4.1); retail restores the record after the
    // object ticks (FUN_004106e0), so pass the copy only the player tick reads. Returns true when
    // the steer wrote the record.
    bool step(input::Frame& rec, const Context& c, const Display& d = {});
    // FUN_00426e30: the float copy -> the clamped, scaled draw position (also run by step).
    void place(const Display& d);
    // The draw queue gate (0x7325ec | 0x7325f0 != 0) [BYTES 0x426b5b].
    bool queued() const { return arrow_colour != 0 || colour != 0; }
};

// §2.3 the vertex colour of an alpha that changed this step: 0x00FFFFFF | floor(alpha*256) << 24,
// 0xFFFFFFFF at 1.0 (the float-bit trick at 0x42665d) [BYTES].
std::uint32_t fade_colour(float alpha);
// §2.4 the field arrow's angle [BYTES 0x4267ab..0x426818]: atan2(-dy, dx) with
// dx = (X - Px) * 1024 * (1/512), dy = (Y - Py) * 1024 / 376.32 (cursor in 1024 units, player in
// back-buffer px: a retail unit mismatch, exact only at 1024x768). `prev` when dx = dy = 0.
float draw_angle(float X, float Y, float Px, float Py, float prev);
// §4.1 the click-to-move direction bits [BYTES 0x426a87..0x426b4d]: from the SCREEN CENTRE (512, 360),
// atan2(-(Y - 360) * 2.7210884, (X - 512) * 2), quantised 8-way (0x6b7384) or 4-way (0x6b7380).
std::uint32_t steer_bits(float X, float Y, bool eight_way);
// The click-to-move anchor's height [BYTES 0x42684f..0x4268a9]: base = a[+0x230] + a[+0x324]
// (the smoothed floor + h, plus h: the camera look-at, engine_arch C15), d = a.y - base;
// y' = d > 5 ? base + d - 5 : d < -h ? base + d + h : base; minus 2.0.
float steer_anchor_y(float origin_y, float smoothed_230, float height_324);
// FUN_00473540 [BYTES 0x473907..0x473962]: the height FUN_00473540 projects for 0x7325c8: the floor
// +0x318 clamped into [feet - 2.75, feet] (feet = a.y - a[+0x324]).
float shadow_y(float feet_y, float floor_318);

// ---- the sprite (§2, FUN_00426f20) -----------------------------------------------------------------
// CURSOR.DDS (64x64 A1R5G5B5, the image table's entry 3, handle 0x749d58). Four vertices of the
// triangle strip in back-buffer pixels (FUN_005e4100: floor(v * W/1024), floor(v * H/768); the D3D9
// -0.5 half-pixel shift is dropped, as the book renderer drops it), the UVs (transposed: u runs DOWN
// the screen, the 36x36-texel crop 0..0.5625), and the colour 0x7325f0.
struct Quad {
    float x[4] = {}, y[4] = {}, u[4] = {}, v[4] = {};
    std::uint32_t colour = 0;
};
// `field` = the rotated look (menuMode == 0 and a player, 0x74c09c): the corners (-15,-15), (-15,8),
// (8,-15), (8,8) (table 0x6981e4) turned by drawAngle - pi/4 about the anchor; else the upright
// quad (Xi-15..Xi+8, Yi-15..Yi+8). Squared about its centre when A != 1 (0x427187..0x4271ed).
Quad cursor_quad(const CursorState& s, bool field, const Display& d);

// FUN_00426bc0 [BYTES 0x426bc0..0x426c64]: the player spawn: moveAngle = drawAngle = theta and the
// cursor warp (mode 0) to (trunc(512 + 96 cos(-theta)), trunc(360 + 96 sin(-theta))).
Point spawn_cursor(float theta);

// ---- the skill switch (FUN_00465170 0x465a56..0x465b31, FUN_0046cb00) ----------------------------
// The wheel (rec.wheel, the raw WM_MOUSEWHEEL delta) and the skill bits 0x40 / 0x80 / 0x100..0x400
// request a bracelet; the mover applies it (0x4652ca..0x465377: when the ASO allows, bracelet
// 0x76a630 = the request, 0x77 -> 0xff; SE 0xa; the combo stage reset).
struct SkillSwitch {
    int cooldown = 0;           // player +0x764 (15 after a wheel step)
    int dir = 0;                // player +0x738 (the last step's direction)
    // One mover tick. `wheel` = rec.wheel (0 when not the player or under a script lock);
    // `pressed` = +0x50c; `probe_hit` = the attack press started an action (FUN_00460470), which
    // skips the switch; `bracelet` = 0x76a630 (-1 or 0xff = none); `owned(id)` = g_flags[id] > 0.
    // Returns the request (+0x518), or kNoRequest.
    static constexpr int kNoRequest = -2;
    template <class Owned>
    int tick(int wheel, std::uint32_t pressed, bool probe_hit, int bracelet, Owned owned) {
        if (cooldown != 0) { --cooldown; wheel = 0; }              // 0x465a56
        if (probe_hit) return kNoRequest;                          // 0x465ad2..0x465b18
        if (!((pressed & 0x7c0u) || wheel)) return kNoRequest;     // 0x465b1d
        return request(wheel, pressed, bracelet, owned);
    }
    // FUN_0046cb00 [BYTES 0x46cb00..0x46cc0c].
    template <class Owned>
    int request(int wheel, std::uint32_t pressed, int bracelet, Owned owned) {
        if (bracelet == -1) return owned(0x74) ? 0x74 : kNoRequest;
        int id = bracelet;
        int d = wheel;
        for (;;) {
            if (d != 0) {                                          // 0x46cb38: trunc(wheel / 120)
                const int q = d / 120;
                if (q > 0) d = 1;
                else if (q < 0) d = -1;
                else { d = 0; continue; }
                dir = d;
                cooldown = 15;                                     // 0x46cb61
            } else if (pressed & 0x40u) { d = 1; dir = 1; }        // Previous Skill
            else if (pressed & 0x80u) { d = -1; dir = -1; }        // Next Skill
            else {                                                 // Skill 1 / 2 / 3 (0x46cbd0)
                id = (pressed & 0x200u) ? 0x75 : (pressed & 0x400u) ? 0x76 : 0x74;
                if (!owned(id)) id = -1;
                return id != bracelet ? id : kNoRequest;
            }
            if (id == 0xff) id = d <= 0 ? 0x76 : 0x74;             // 0x46cb95
            else { id += d; if (id < 0x74) id = 0x76; else if (id >= 0x77) id = 0x74; }
            if (owned(id)) return id != bracelet ? id : kNoRequest;
            // Unowned (0x46cbc8): the loop re-enters at 0x46cb34 with d = +-1, which truncates to 0
            // (0x46cb6d) and falls through to the BITS: the keyboard bits step on and so skip an
            // unowned bracelet, but a wheel step lands on the Skill-1 path (0x74, or none) [BYTES].
            d = 0;
        }
    }
};

// ---- menus (§5) ------------------------------------------------------------------------------------
// The arrow tip: menus hit-test (rec.x - 16, rec.y - 16) on the RAW record ints (§5).
inline Point tip(int x, int y) { return {x - 16, y - 16}; }
// FUN_005a4760 [EXPORT]: a csprite rect (authored x, y, w, h; half-open) against a point; an x-anchor
// of 2 unprojects the point's x first (pillar_inv). The y-anchor-2 branch is dead for book sprites.
bool csprite_hit(float x, float y, float w, float h, int anchor_x, int px, int py, const Display& d);
// GoWndDialog's row under the cursor (FUN_005789a0 [BYTES 0x578a26..0x578ad2, 0x578d9b..0x578e71]),
// or -1: dy = trunc(my - top) - 16 in [0, trunc(h)); dx = trunc(mx - cx) - 16, 2|dx| < trunc(X(w));
// row = 1 - trunc((dy + 6.4) / -38.4) in [1, n). `left_gate` = path B's extra double(mx + 16) >=
// X(0.0); path A tests mx >= 0 instead. X() is pillar_x: back-buffer px compared with 1024-unit
// distances (exact at 4:3; a retail quirk at 16:9, MOUSE.md §5.1).
int wnddialog_row(int mx, int my, float cx, float top, float w, float h, int n, bool path_b, const Display& d);

}  // namespace cleria::mouse
