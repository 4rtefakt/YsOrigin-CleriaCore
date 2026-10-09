// cleria-view — the gamepad in the shell: the Input > Controller page (a live, procedurally drawn
// controller with its bindings, rebinding on the picture, sticks / triggers / vibration settings),
// the hot-plug notice, and retail's force feedback (FUN_005d3500).
// Textual part of main.cpp: #included after view_shell.inl (it uses Shell, ui, settings(), g_sdl_in);
// ONE translation unit, not standalone. Spec: build/shell_re/PAD.md; logic: src/cleria/pad.
#include "cleria/pad.hpp"

static_assert(SDL_GAMEPAD_TYPE_XBOXONE == 3 && SDL_GAMEPAD_TYPE_PS5 == 6 && SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO == 7 &&
              SDL_GAMEPAD_TYPE_GAMECUBE == 11, "pad::glyphs_for_sdl_type assumes SDL's SDL_GamepadType numbering");
static_assert(int(SDL_GAMEPAD_BUTTON_SOUTH) == pad::kSouth && int(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == pad::kRightShoulder &&
              int(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) == pad::kDpadRight && int(SDL_GAMEPAD_BUTTON_MISC1) == pad::kMisc1 &&
              pad::kLeftTrigger == input::kSdlLeftTrigger && pad::kRightTrigger == input::kSdlRightTrigger,
              "pad::Button mirrors SDL_GamepadButton");

// ---- retail force feedback ---------------------------------------------------------------------------
// FUN_005d3500(n): stop the last effect, start effect n — only with [input] GamePad and ForceFeedback
// (0x6d602b / 0x6d5d93) [EXPORT]. The effects are retail's table (pad::retail_effect); SDL_RumbleGamepad
// replaces a running rumble the same way. [cleria.pad] VibrationIntensity scales it (1.0 = retail).
static void pad_rumble(int effect) {
    const Settings& s = settings().s;
    if (!g_sdl_in.pad || s.input.game_pad == 0 || s.input.force_feedback == 0) return;
    const pad::Rumble r = pad::retail_effect(effect, s.pad.vibration_intensity);
    if (r.ms) SDL_RumbleGamepad(g_sdl_in.pad, r.low, r.high, r.ms);
}

// ---- the live pad --------------------------------------------------------------------------------------
// What the page draws: the opened pad (g_sdl_in.pad), or CLERIA_PAD_FAKE for headless shots:
//   CLERIA_PAD_FAKE="type=playstation;lx=0.6;ly=-0.3;rx=0;ry=0;lt=0.7;rt=0;buttons=a,leftshoulder,dpup;row=12"
// (type xbox / playstation / nintendo / generic; buttons by their SDL names; row = the page row to select).
struct PadLive {
    bool present = false, simulated = false;
    std::string name;
    int type = 0;                     // SDL_GamepadType
    std::array<bool, 32> btn{};       // SDL_GamepadButton
    float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1, y down
    float lt = 0, rt = 0;             // 0..1
};

struct PadUi {
    bool open = false;                // Input > Controller is showing
    std::string msg;
    bool msg_ok = true;
    int fake_row = -1;                // CLERIA_PAD_FAKE row=
    bool fake_scroll = false;         // ...and scroll it into view once laid out
    bool fake_parsed = false;
    PadLive fake;
    input::Mapper probe;              // a private mapper: what the game reads from the stick right now
    // the hot-plug notice
    std::string toast;
    Uint64 toast_until = 0;
    bool primed = false;
    SDL_JoystickID last_id = 0;
};
static PadUi g_pad_ui;

static const PadLive* pad_fake() {
    PadUi& u = g_pad_ui;
    if (!u.fake_parsed) {
        u.fake_parsed = true;
        if (const char* e = std::getenv("CLERIA_PAD_FAKE")) {
            PadLive& p = u.fake;
            p.present = p.simulated = true;
            p.type = SDL_GAMEPAD_TYPE_STANDARD;
            std::string s = e;
            for (std::size_t i = 0; i <= s.size();) {
                std::size_t j = s.find(';', i);
                if (j == std::string::npos) j = s.size();
                const std::string kv = s.substr(i, j - i);
                i = j + 1;
                const std::size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                const float f = float(std::atof(v.c_str()));
                if (k == "type") {
                    p.type = v == "xbox" ? SDL_GAMEPAD_TYPE_XBOXONE : v == "playstation" ? SDL_GAMEPAD_TYPE_PS5
                           : v == "nintendo" ? SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO : SDL_GAMEPAD_TYPE_STANDARD;
                } else if (k == "lx") p.lx = f; else if (k == "ly") p.ly = f;
                else if (k == "rx") p.rx = f; else if (k == "ry") p.ry = f;
                else if (k == "lt") p.lt = f; else if (k == "rt") p.rt = f;
                else if (k == "row") u.fake_row = std::atoi(v.c_str());
                else if (k == "buttons") {
                    for (std::size_t a = 0; a <= v.size();) {
                        std::size_t b = v.find(',', a);
                        if (b == std::string::npos) b = v.size();
                        if (auto x = input::sdl_pad_from_name(v.substr(a, b - a)); x && *x >= 0 && *x < 32) p.btn[std::size_t(*x)] = true;
                        if (v.substr(a, b - a) == "dpup") p.btn[pad::kDpadUp] = true;
                        if (v.substr(a, b - a) == "dpdown") p.btn[pad::kDpadDown] = true;
                        if (v.substr(a, b - a) == "dpleft") p.btn[pad::kDpadLeft] = true;
                        if (v.substr(a, b - a) == "dpright") p.btn[pad::kDpadRight] = true;
                        a = b + 1;
                    }
                }
            }
            static const char* const kNames[] = {"Generic controller", "Xbox controller", "DualSense controller",
                                                 "Pro Controller", "Generic controller"};
            p.name = kNames[pad::glyphs_for_sdl_type(p.type)];
        }
    }
    return u.fake.present ? &u.fake : nullptr;
}

static PadLive pad_live() {
    if (const PadLive* f = pad_fake()) return *f;
    PadLive p;
    SDL_Gamepad* g = g_sdl_in.pad;
    if (!g) return p;
    p.present = true;
    const char* n = SDL_GetGamepadName(g);
    p.name = n && *n ? n : "Gamepad";
    p.type = int(SDL_GetGamepadType(g));
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 32; ++b) p.btn[std::size_t(b)] = SDL_GetGamepadButton(g, SDL_GamepadButton(b));
    auto ax = [&](SDL_GamepadAxis a) { return std::clamp(float(SDL_GetGamepadAxis(g, a)) / 32767.0f, -1.0f, 1.0f); };
    p.lx = ax(SDL_GAMEPAD_AXIS_LEFTX); p.ly = ax(SDL_GAMEPAD_AXIS_LEFTY);
    p.rx = ax(SDL_GAMEPAD_AXIS_RIGHTX); p.ry = ax(SDL_GAMEPAD_AXIS_RIGHTY);
    p.lt = std::clamp(ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f, 1.0f);
    p.rt = std::clamp(ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f, 1.0f);
    return p;
}
static bool live_pressed(const PadLive& p, const Settings& s, int b) {
    if (b == pad::kLeftTrigger) return p.lt * 32767.0f > s.pad.trigger_threshold * 32767.0f;
    if (b == pad::kRightTrigger) return p.rt * 32767.0f > s.pad.trigger_threshold * 32767.0f;
    return b >= 0 && b < 32 && p.btn[std::size_t(b)];
}

// The connected pads, in SDL's order (GamePadID is 1-based into it, FUN_005d5b20).
static std::vector<std::string> pad_names() {
    std::vector<std::string> v;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n && ids; ++i) {
        const char* nm = SDL_GetGamepadNameForID(ids[i]);
        v.push_back(nm && *nm ? nm : "Gamepad");
    }
    SDL_free(ids);
    return v;
}
static void pad_reopen() {
    if (g_sdl_in.pad) { SDL_CloseGamepad(g_sdl_in.pad); g_sdl_in.pad = nullptr; }
    g_sdl_in.open_pad();
}

// ---- the always-on layer: hot-plug notice, late open, the nav swap ----------------------------------------
static void pad_hud(ImDrawList* dl, float w, float h) {
    PadUi& u = g_pad_ui;
    ui::pad_swap_ok = settings().s.pad.swap_confirm_cancel;
    // A pad plugged in while the launcher (which does not route SDL events to the input layer) or the
    // panel is up is opened here.
    if (!g_sdl_in.pad && SDL_HasGamepad()) g_sdl_in.open_pad();
    const SDL_JoystickID id = g_sdl_in.pad ? SDL_GetGamepadID(g_sdl_in.pad) : 0;
    if (!u.primed) { u.primed = true; u.last_id = id; return; }   // the pad at boot needs no notice
    if (id != u.last_id && !std::getenv("CLERIA_DRIVE")) {        // drives stay deterministic
        if (id) {
            const char* n = SDL_GetGamepadName(g_sdl_in.pad);
            u.toast = std::string(n && *n ? n : "Controller") + " connected";
        } else {
            u.toast = "Controller disconnected";
        }
        u.toast_until = SDL_GetTicks() + 3500;
    }
    u.last_id = id;
    if (!u.toast.empty() && SDL_GetTicks() < u.toast_until) ui::toast(dl, w, h, "Controller", u.toast.c_str());
}

// ---- the controller picture ----------------------------------------------------------------------------------
namespace padpic {

constexpr ImU32 kBody = IM_COL32(48, 46, 39, 255), kBodyEdge = IM_COL32(104, 96, 74, 255);
constexpr ImU32 kKey = IM_COL32(27, 26, 21, 255), kKeyEdge = IM_COL32(118, 110, 90, 255);
constexpr ImU32 kLit = IM_COL32(246, 232, 176, 255);
constexpr ImU32 kLine = IM_COL32(170, 158, 120, 150);

struct Part {
    int button = -1;          // SDL button (or a trigger); -1 = the d-pad block (no binding)
    ImVec2 c{};               // centre
    float rx = 0, ry = 0;     // half extents (circle: rx = radius, ry = 0)
    bool hit(ImVec2 m) const {
        if (ry == 0.0f) return (m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y) <= rx * rx;
        return std::fabs(m.x - c.x) <= rx && std::fabs(m.y - c.y) <= ry;
    }
};

// The face buttons' colours: Xbox A/B/X/Y, PlayStation's shapes; Nintendo and generic are neutral.
inline ImU32 face_colour(pad::Glyphs g, int b) {
    if (g == pad::kXbox) {
        static const ImU32 k[4] = {IM_COL32(110, 190, 70, 255), IM_COL32(222, 74, 60, 255), IM_COL32(64, 132, 226, 255),
                                   IM_COL32(236, 196, 52, 255)};
        return k[b];
    }
    if (g == pad::kPlayStation) {
        static const ImU32 k[4] = {IM_COL32(132, 178, 236, 255), IM_COL32(236, 102, 102, 255), IM_COL32(220, 146, 210, 255),
                                   IM_COL32(72, 214, 164, 255)};
        return k[b];
    }
    return ui::kLabel;
}

inline void ps_shape(ImDrawList* dl, int b, ImVec2 c, float r, ImU32 col, float th) {
    switch (b) {
        case pad::kSouth:
            dl->AddLine({c.x - r, c.y - r}, {c.x + r, c.y + r}, col, th);
            dl->AddLine({c.x - r, c.y + r}, {c.x + r, c.y - r}, col, th);
            break;
        case pad::kEast: dl->AddCircle(c, r * 1.1f, col, 20, th); break;
        case pad::kWest: dl->AddRect({c.x - r, c.y - r}, {c.x + r, c.y + r}, col, 0, 0, th); break;
        default: dl->AddTriangle({c.x, c.y - r * 1.15f}, {c.x - r * 1.1f, c.y + r * 0.8f}, {c.x + r * 1.1f, c.y + r * 0.8f}, col, th); break;
    }
}

struct Callout { ImVec2 anchor; std::string text; ImU32 col; };

// Draw the controller centred at `c` with unit `u` (the body is ~92u wide, ~70u tall). `focus` = the
// button to ring in gold (the selected action's), `hover` = the button under the mouse. Returns the
// parts (for hit tests) through `parts`.
inline void draw(ImDrawList* dl, ImVec2 c, float u, pad::Glyphs g, const PadLive& p, const Settings& s,
                 const pad::PadButtons& binds, int focus, int hover, std::vector<Part>& parts) {
    const float S = ui::S;
    auto P = [&](float x, float y) { return ImVec2{c.x + x * u, c.y + y * u}; };
    const bool offset = pad::offset_sticks(g);
    const ImVec2 ls = offset ? P(-26, -8) : P(-12, 9), rs = offset ? P(13, 8) : P(12, 9);
    const ImVec2 dp = offset ? P(-13, 8) : P(-26, -8), fb = P(26, -8);
    parts.clear();

    // triggers (behind the body): the pull fills from the bottom; the line is the press threshold
    for (int side = 0; side < 2; ++side) {
        const float sx = side ? 1.0f : -1.0f;
        const ImVec2 a = P(sx < 0 ? -35.0f : 22.0f, -38.0f), b = P(sx < 0 ? -22.0f : 35.0f, -27.0f);
        const int btn = side ? pad::kRightTrigger : pad::kLeftTrigger;
        const float pull = side ? p.rt : p.lt;
        const bool lit = live_pressed(p, s, btn);
        dl->AddRectFilled({a.x - 1.5f * S, a.y - 1.5f * S}, {b.x + 1.5f * S, b.y}, kBodyEdge, 3.0f * u, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(a, b, kKey, 3.0f * u, ImDrawFlags_RoundCornersTop);
        if (pull > 0.0f) {
            const float y = b.y - (b.y - a.y) * pull;
            dl->AddRectFilled({a.x, y}, b, lit ? kLit : IM_COL32(150, 136, 92, 255), pull > 0.9f ? 3.0f * u : 0.0f,
                              ImDrawFlags_RoundCornersTop);
        }
        const float ty = b.y - (b.y - a.y) * s.pad.trigger_threshold;
        dl->AddLine({a.x, ty}, {b.x, ty}, IM_COL32(214, 176, 62, 140), 1.0f * S);
        parts.push_back({btn, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, (b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f});
    }
    // shoulders
    for (int side = 0; side < 2; ++side) {
        const ImVec2 a = P(side ? 17.0f : -39.0f, -27.5f), b = P(side ? 39.0f : -17.0f, -21.0f);
        const int btn = side ? pad::kRightShoulder : pad::kLeftShoulder;
        dl->AddRectFilled({a.x - 1.5f * S, a.y - 1.5f * S}, {b.x + 1.5f * S, b.y}, kBodyEdge, 3.0f * u, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(a, b, live_pressed(p, s, btn) ? kLit : kKey, 3.0f * u, ImDrawFlags_RoundCornersTop);
        parts.push_back({btn, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, (b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f});
    }
    // the body: the shapes once enlarged in the edge colour, then in the body colour (a clean outline)
    for (int pass = 0; pass < 2; ++pass) {
        const float e = pass == 0 ? 2.0f * S : 0.0f;
        const ImU32 col = pass == 0 ? kBodyEdge : kBody;
        dl->AddRectFilled({P(-39, -22).x - e, P(-39, -22).y - e}, {P(39, 10).x + e, P(39, 10).y + e}, col, 13.0f * u);
        for (float sx : {-1.0f, 1.0f}) {
            dl->AddCircleFilled(P(sx * 30.0f, 10.0f), 15.0f * u + e, col, 40);
            dl->AddCircleFilled(P(sx * 34.0f, 21.0f), 11.0f * u + e, col, 40);
        }
        dl->AddRectFilled({P(-30, 0).x, P(-30, 0).y - e}, {P(30, 14).x, P(30, 14).y + e}, col, 6.0f * u);
    }

    // a round key: fill, edge, lit / ring
    auto key = [&](int btn, ImVec2 at, float r, ImU32 glyph_col, const char* label) {
        const bool lit = live_pressed(p, s, btn);
        dl->AddCircleFilled(at, r, lit ? kLit : kKey, 28);
        dl->AddCircle(at, r, kKeyEdge, 28, 1.0f * S);
        if (label && *label) {
            const float px = std::max(10.0f * S, r * 1.35f);
            const ImVec2 ts = ui::text_size(ui::F.cond, px, label);
            ui::text(dl, ui::F.cond, px, {at.x - ts.x * 0.5f, at.y - px * 0.55f}, lit ? IM_COL32(30, 26, 14, 255) : glyph_col, label);
        }
        parts.push_back({btn, at, r, 0.0f});
    };
    // sticks: the well, the cap moved by the deflection, LS / RS on the cap
    auto stick = [&](int btn, ImVec2 at, float x, float y) {
        const float well = 8.5f * u, cap = 6.0f * u, travel = 2.6f * u;
        dl->AddCircleFilled(at, well, IM_COL32(22, 21, 17, 255), 32);
        dl->AddCircle(at, well, kKeyEdge, 32, 1.0f * S);
        const ImVec2 cc{at.x + x * travel, at.y + y * travel};
        const bool lit = live_pressed(p, s, btn);
        const bool moved = x * x + y * y > 0.0004f;
        dl->AddCircleFilled(cc, cap, lit ? kLit : IM_COL32(58, 56, 47, 255), 28);
        dl->AddCircle(cc, cap, moved ? IM_COL32(236, 228, 204, 255) : kKeyEdge, 28, (moved ? 1.6f : 1.0f) * S);
        dl->AddCircle(cc, cap * 0.62f, IM_COL32(90, 84, 66, 255), 24, 1.0f * S);
        parts.push_back({btn, at, well, 0.0f});
    };
    stick(pad::kLeftStick, ls, p.lx, p.ly);
    stick(pad::kRightStick, rs, p.rx, p.ry);
    // the d-pad (POV 0: always movement, never bound)
    {
        const float hw = 1.9f * u, len = 5.6f * u;
        const ImVec2 arms[4][2] = {{{dp.x - hw, dp.y - len}, {dp.x + hw, dp.y - hw}}, {{dp.x - hw, dp.y + hw}, {dp.x + hw, dp.y + len}},
                                   {{dp.x - len, dp.y - hw}, {dp.x - hw, dp.y + hw}}, {{dp.x + hw, dp.y - hw}, {dp.x + len, dp.y + hw}}};
        dl->AddRectFilled({dp.x - len - 1.0f * S, dp.y - hw - 1.0f * S}, {dp.x + len + 1.0f * S, dp.y + hw + 1.0f * S}, kKeyEdge, 1.5f * u);
        dl->AddRectFilled({dp.x - hw - 1.0f * S, dp.y - len - 1.0f * S}, {dp.x + hw + 1.0f * S, dp.y + len + 1.0f * S}, kKeyEdge, 1.5f * u);
        dl->AddRectFilled({dp.x - hw, dp.y - hw}, {dp.x + hw, dp.y + hw}, kKey);
        for (int k = 0; k < 4; ++k)
            dl->AddRectFilled(arms[k][0], arms[k][1], live_pressed(p, s, pad::kDpadUp + k) ? kLit : kKey, 1.2f * u);
        parts.push_back({-1, dp, len, len});
    }
    // face buttons
    {
        const float sp = 6.8f * u, r = 3.7f * u;
        const ImVec2 at[4] = {{fb.x, fb.y + sp}, {fb.x + sp, fb.y}, {fb.x - sp, fb.y}, {fb.x, fb.y - sp}};
        for (int b = 0; b < 4; ++b) {
            const ImU32 col = face_colour(g, b);
            key(b, at[b], r, col, g == pad::kPlayStation ? "" : pad::button_short(g, b));
            if (g == pad::kPlayStation)
                ps_shape(dl, b, at[b], r * 0.45f, live_pressed(p, s, b) ? IM_COL32(30, 26, 14, 255) : col, 1.4f * S);
        }
    }
    // back / start (pills), guide
    for (int side = 0; side < 2; ++side) {
        const int btn = side ? pad::kStart : pad::kBack;
        const ImVec2 at = P(side ? 8.5f : -8.5f, -11.0f);
        const float hw = 3.4f * u, hh = 1.7f * u;
        const bool lit = live_pressed(p, s, btn);
        dl->AddRectFilled({at.x - hw, at.y - hh}, {at.x + hw, at.y + hh}, lit ? kLit : kKey, hh);
        dl->AddRect({at.x - hw, at.y - hh}, {at.x + hw, at.y + hh}, kKeyEdge, hh, 0, 1.0f * S);
        parts.push_back({btn, at, hw, hh});
    }
    key(pad::kGuide, P(0.0f, offset ? -3.0f : 1.5f), 3.2f * u, ui::kMuted, "");

    // focus / hover rings
    for (const Part& pt : parts) {
        if (pt.button < 0 || (pt.button != focus && pt.button != hover)) continue;
        const ImU32 col = pt.button == focus ? ui::kGold : IM_COL32(244, 240, 230, 200);
        if (pt.ry == 0.0f) dl->AddCircle(pt.c, pt.rx + 2.0f * S, col, 32, 2.0f * S);
        else dl->AddRect({pt.c.x - pt.rx - 2.0f * S, pt.c.y - pt.ry - 2.0f * S}, {pt.c.x + pt.rx + 2.0f * S, pt.c.y + pt.ry + 2.0f * S},
                         col, 3.0f * S, 0, 2.0f * S);
    }

    // callouts: every bound action, plus the d-pad's fixed movement (the sticks' is under their monitors)
    std::vector<Callout> left, right;
    auto add = [&](ImVec2 anchor, std::string t, ImU32 col) {
        (anchor.x < c.x ? left : right).push_back({anchor, std::move(t), col});
    };
    for (const Part& pt : parts) {
        const int a = pad::action_on(binds, pt.button);
        std::string t = a >= 0 ? pad::action_short(a) : pt.button < 0 ? "Move" : "";
        if (t.empty()) continue;
        const bool fixed = a < 0;
        const ImU32 col = pt.button >= 0 && pt.button == focus ? ui::kGold
                        : live_pressed(p, s, pt.button) ? ui::kTabOn : fixed ? ui::kMuted : ui::kValue;
        add(pt.c, t, col);
    }
    const float px = 13.5f * S, gap = 15.5f * S;
    for (int side = 0; side < 2; ++side) {
        std::vector<Callout>& v = side ? right : left;
        std::sort(v.begin(), v.end(), [](const Callout& a, const Callout& b) { return a.anchor.y < b.anchor.y; });
        std::vector<float> ys(v.size());
        const float top = P(0, -40).y, bot = P(0, 33).y;
        for (std::size_t i = 0; i < v.size(); ++i)
            ys[i] = std::max(i ? ys[i - 1] + gap : top, std::min(v[i].anchor.y, bot));
        if (!ys.empty() && ys.back() > bot) {
            const float shift = ys.back() - bot;
            for (float& y : ys) y -= shift;
        }
        const float col_x = side ? P(47, 0).x : P(-47, 0).x;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const Callout& co = v[i];
            const ImVec2 ts = ui::text_size(ui::F.cond_med, px, co.text.c_str());
            const float tx = side ? col_x + 4.0f * S : col_x - 4.0f * S - ts.x;
            ui::text(dl, ui::F.cond_med, px, {tx, ys[i] - px * 0.55f}, co.col, co.text.c_str());
            const ImVec2 elbow{side ? col_x - 3.0f * u : col_x + 3.0f * u, ys[i]};
            dl->AddLine({col_x, ys[i]}, elbow, kLine, 1.0f * S);
            dl->AddLine(elbow, co.anchor, kLine, 1.0f * S);
            dl->AddCircleFilled(co.anchor, 1.8f * S, co.col == ui::kMuted ? kLine : co.col, 10);
        }
    }
}

// One stick's monitor: the full range, the deadzone (retail's radial threshold), the outer deadzone
// (analog walk), the 8 sectors with the one the game would move in lit, and the raw position.
inline void monitor(ImDrawList* dl, ImVec2 c, float R, float x, float y, float dz, float outer, bool show_outer,
                    bool drives, std::uint32_t dir_bits, const char* title, const char* state, ImU32 state_col) {
    const float S = ui::S;
    dl->AddCircleFilled(c, R, IM_COL32(26, 25, 20, 255), 48);
    // the sector the mapper picked (8-way: the direction bits -> an angle; up = -y)
    if (drives && dir_bits) {
        const float ax = float((dir_bits & 4) ? 1 : 0) - float((dir_bits & 2) ? 1 : 0);
        const float ay = float((dir_bits & 1) ? 1 : 0) - float((dir_bits & 8) ? 1 : 0);
        const float a0 = std::atan2(ay, ax);
        const float h = 3.14159265f / 8.0f;
        dl->PathLineTo(c);
        dl->PathArcTo(c, R, a0 - h, a0 + h, 12);
        dl->PathFillConvex(IM_COL32(214, 176, 62, 70));
    }
    for (int k = 0; k < 8; ++k) {
        const float a = 3.14159265f / 8.0f + float(k) * 3.14159265f / 4.0f;
        dl->AddLine({c.x + std::cos(a) * R * dz, c.y + std::sin(a) * R * dz}, {c.x + std::cos(a) * R, c.y + std::sin(a) * R},
                    IM_COL32(60, 57, 46, 255), 1.0f * S);
    }
    dl->AddCircle(c, R, ui::kRowEdge, 48, 1.2f * S);
    dl->AddCircleFilled(c, R * dz, IM_COL32(120, 54, 40, 90), 40);
    dl->AddCircle(c, R * dz, IM_COL32(214, 110, 80, 200), 40, 1.2f * S);
    if (show_outer && outer < 0.999f) dl->AddCircle(c, R * outer, IM_COL32(214, 176, 62, 160), 48, 1.0f * S);
    // the raw position (the square gate of a pad can reach |v| > 1: clamp the dot to the rim)
    float px_ = x, py_ = y;
    const float m = std::sqrt(px_ * px_ + py_ * py_);
    if (m > 1.0f) { px_ /= m; py_ /= m; }
    const ImVec2 d{c.x + px_ * R, c.y + py_ * R};
    if (m > 0.02f) dl->AddLine(c, d, drives ? ui::kGold : ui::kMuted, 1.2f * S);
    dl->AddCircleFilled(d, 3.6f * S, drives ? ui::kGold : ui::kLabel, 16);
    const float tpx = 13.0f * S;
    const ImVec2 t1 = ui::text_size(ui::F.cond, tpx, title);
    ui::text(dl, ui::F.cond, tpx, {c.x - t1.x * 0.5f, c.y + R + 5.0f * S}, ui::kSection, title);
    const ImVec2 t2 = ui::text_size(ui::F.body, tpx, state);
    ui::text(dl, ui::F.body, tpx, {c.x - t2.x * 0.5f, c.y + R + 5.0f * S + tpx + 2.0f * S}, state_col, state);
}

}  // namespace padpic

// ---- the page --------------------------------------------------------------------------------------------
// Input > Controller. Rows on the left (status, the eight pad actions, sticks, vibration); the live
// picture in the right pane under the description. Called from the Input tab while g_pad_ui.open.
static void pad_page(ui::List& L, Shell& shell) {
    PadUi& u = g_pad_ui;
    Settings& s = settings().s;
    auto persist = [] { settings().touch(); settings().save(); };
    ui::pad_test_frame = ImGui::GetFrameCount();
    const PadLive live = pad_live();
    const pad::Glyphs g = pad::resolve_glyphs(s.pad.glyphs, live.type, live.present);
    pad::PadButtons binds = pad::pad_buttons(s);

    auto bind = [&](int a, int button) {
        const int old = binds[std::size_t(a)];
        const int r = pad::bind_swap(binds, a, button);
        if (r == -2) {
            u.msg = "The d-pad always moves; it can't be bound to an action.";
            u.msg_ok = false;
            return;
        }
        pad::set_pad_buttons(s, binds);
        persist();
        u.msg = std::string(pad::action_short(a)) + " is now on " + pad::button_name(g, button) + ".";
        if (r >= 0)
            u.msg += std::string(" ") + pad::action_short(r) + (old >= 0 ? std::string(" moved to ") + pad::button_name(g, old) + "."
                                                                         : std::string(" is now unbound."));
        u.msg_ok = true;
    };
    // a finished capture of one of the pad actions (this frame the pad's buttons are not navigation)
    const bool just_captured = shell.capture.done;
    if (shell.capture.done &&shell.capture.kind == Shell::Capture::Pad && shell.capture.slot >= input::PAD_ACTION &&
        shell.capture.slot <= input::PAD_SWORD_REVU) {
        const auto cap = shell.capture;
        shell.capture = {};
        const int a = cap.slot - input::PAD_ACTION;
        if (cap.cleared) {
            binds[std::size_t(a)] = -1;
            pad::set_pad_buttons(s, binds);
            persist();
            u.msg = std::string(pad::action_short(a)) + " is unbound.";
            u.msg_ok = true;
        } else if (!cap.cancelled) {
            bind(a, cap.value);
        }
    }
    if (u.fake_row >= 0) { L.sel = u.fake_row; u.fake_row = -1; u.fake_scroll = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false) && !shell.capture.active && !just_captured) {
        u.open = false;
        u.msg.clear();
    }

    if (ui::button(L, "< Back", "Back to the input options. (Gamepad: Back / View.)")) { u.open = false; u.msg.clear(); }

    ui::section(L, "Controller");
    {
        const std::vector<std::string> names = pad_names();
        std::string st;
        ImU32 col = ui::kGood;
        if (s.debug.no_initialize_game_ctrl.value_or(0) != 0) { st = "Disabled ([debug] NoInitializeGameCtrl)"; col = ui::kWarn; }
        else if (live.simulated) st = live.name + " (simulated)";
        else if (live.present) st = live.name + " connected";
        else { st = "No controller connected"; col = ui::kWarn; }
        ui::info(L, "Status", st.c_str(), col,
                 "Plug a controller in at any time; CleriaCore picks it up straight away.");
        std::vector<std::string> items;
        for (std::size_t i = 0; i < names.size(); ++i) items.push_back(std::to_string(i + 1) + ". " + names[i]);
        if (items.empty()) items.push_back("None");
        std::vector<const char*> ptrs;
        for (const std::string& t : items) ptrs.push_back(t.c_str());
        int idx = std::clamp(int(s.input.game_pad_id) - 1, 0, int(items.size()) - 1);
        if (ui::choice(L, "Use controller", &idx, ptrs.data(), int(ptrs.size()),
                       "Which controller to read when several are connected. (The original config tool's "
                       "joystick list, [input] GamePadID.)", names.size() > 1)) {
            s.input.game_pad_id = idx + 1;
            persist();
            pad_reopen();
        }
        bool use = s.input.game_pad != 0;
        if (ui::toggle(L, "Use gamepad", &use,
                       "Read the controller's buttons and sticks in the game. (The original's Use Game Pad, "
                       "[input] GamePad.)")) {
            s.input.game_pad = use;
            persist();
        }
        static const char* const kGlyphItems[] = {"Auto", "Xbox", "PlayStation", "Nintendo", "Generic"};
        if (ui::choice(L, "Button glyphs", &s.pad.glyphs, kGlyphItems, pad::kGlyphCount,
                       "How buttons are named and drawn. Auto follows the connected controller.")) persist();
    }

    ui::section(L, "Buttons");
    const int first_action_row = L.n;
    for (int a = 0; a < pad::kPadActions; ++a) {
        const int b = binds[std::size_t(a)];
        const std::string v = b >= 0 ? pad::button_name(g, b) : "---";
        const std::string label = input::slot_label(input::PAD_ACTION + a);
        if (ui::choice_label(L, label.c_str(), v.c_str(),
                             "Select, then press the controller button for this action, or click a button on the "
                             "picture. A button that is already used swaps with this one. Esc cancels, Delete "
                             "clears.")) {
            shell.capture.start(Shell::Capture::Pad, input::PAD_ACTION + a, label);
            u.msg.clear();
        }
    }
    const int sel_action = L.sel >= first_action_row && L.sel < first_action_row + pad::kPadActions ? L.sel - first_action_row : -1;
    if (ui::toggle(L, "Swap confirm / cancel", &s.pad.swap_confirm_cancel,
                   "Menus and dialogue confirm with the Jump button and cancel with the Attack button "
                   "(Nintendo style). In play, Attack and Jump stay where they are. Not a retail option."))
        persist();
    if (ui::toggle(L, "Bind by button name", &s.cleria.native_pad_binds,
                   "Store the bindings by the controller's own button names (A, B, LT...) instead of the "
                   "original game's numbered DirectInput buttons. Not a retail option.")) {
        persist();
        binds = pad::pad_buttons(s);
    }
    if (ui::button(L, "Reset buttons to default",
                   s.cleria.native_pad_binds
                       ? "CleriaCore's layout: Attack A, Jump B, Magic X, Boost Y, Menu Start, Walk LT, skills LB / RB."
                       : "The original game's pad layout: Buttons 1, 2, 3, 4, 10, 9, 5, 8 (on an Xbox pad: A, B, X, Y, "
                         "right stick, left stick, LB, Menu).")) {
        pad::reset_pad_bindings(s);
        persist();
        binds = pad::pad_buttons(s);
        u.msg = "Controller buttons reset.";
        u.msg_ok = true;
    }

    ui::section(L, "Sticks");
    {
        float dz = std::round(s.input.dead_zone * 100.0f);
        if (ui::slider(L, "Deadzone", &dz, 5.0f, 95.0f, 5.0f, "%.0f%%",
                       "How far a stick must move before Adol does. The original game's own setting and default "
                       "(35%, [input] DeadZone).")) {
            s.input.dead_zone = dz / 100.0f;
            persist();
        }
        if (ui::toggle(L, "Analog walk", &s.cleria.linear_stick_magnitude,
                       "Walk or run by how far the stick is pushed. In the original a stick past the deadzone "
                       "always runs; only the Walk button walks. Not a retail option."))
            persist();
        const bool aw = s.cleria.linear_stick_magnitude;
        float rw = std::round(s.input.run_walk_threshold * 100.0f);
        if (ui::slider(L, "Run threshold", &rw, 10.0f, 100.0f, 5.0f, "%.0f%%",
                       "With analog walk: how far to push the stick to run. ([input] RunWalkThreshold, 50%.)", aw)) {
            s.input.run_walk_threshold = rw / 100.0f;
            persist();
        }
        float od = std::round(s.pad.outer_dead_zone * 100.0f);
        if (ui::slider(L, "Outer deadzone", &od, 50.0f, 100.0f, 5.0f, "%.0f%%",
                       "With analog walk: how far out the stick counts as fully pushed. 100% = the edge.", aw)) {
            s.pad.outer_dead_zone = od / 100.0f;
            persist();
        }
        static const char* const kCurves[] = {"Linear", "Smooth"};
        if (ui::choice(L, "Response curve", &s.pad.response_curve, kCurves, 2,
                       "With analog walk: Linear follows the stick; Smooth starts from the deadzone and rises "
                       "slowly, for finer walking.", aw))
            persist();
        float tt = std::round(s.pad.trigger_threshold * 100.0f);
        if (ui::slider(L, "Trigger threshold", &tt, 5.0f, 95.0f, 5.0f, "%.0f%%",
                       "How far to pull LT / RT before they count as pressed. (The original game cannot read "
                       "the triggers at all.)")) {
            s.pad.trigger_threshold = tt / 100.0f;
            persist();
        }
    }

    ui::section(L, "Vibration");
    {
        bool ff = s.input.force_feedback != 0;
        if (ui::toggle(L, "Vibration", &ff,
                       "Rumble when you are hit, cast a skill, unleash a Boost attack and in boss fights - the "
                       "original game's own effects. ([input] ForceFeedback, off by default.)")) {
            s.input.force_feedback = ff;
            persist();
        }
        float vi = std::round(s.pad.vibration_intensity * 100.0f);
        if (ui::slider(L, "Vibration strength", &vi, 10.0f, 100.0f, 10.0f, "%.0f%%",
                       "Scales every rumble. 100% = the original strength. Not a retail option.", ff)) {
            s.pad.vibration_intensity = vi / 100.0f;
            persist();
        }
        if (ui::button(L, "Test vibration",
                       "One second of the original's strongest effect (its config tool's Vibration Test).",
                       ff && g_sdl_in.pad && s.input.game_pad != 0))
            pad_rumble(pad::kTestEffect);
    }
    if (!u.msg.empty()) ui::info(L, "", u.msg.c_str(), u.msg_ok ? ui::kGood : ui::kWarn);
    if (u.fake_scroll && L.sel < int(L.row_y.size())) {
        u.fake_scroll = false;
        L.scroll = L.scroll_to = std::max(0.0f, L.row_y[std::size_t(L.sel)] - L.size.y * 0.4f);
    }

    // ---- the picture, in the right pane under the description ----
    const ImVec2 A = shell.panel.side_a, B = shell.panel.side_b;
    const float S = ui::S;
    float desc_h = 0.0f;
    if (!L.desc.empty())
        desc_h = ui::F.body->CalcTextSizeA(17.5f * S, FLT_MAX, B.x - A.x, L.desc.c_str()).y;
    ImDrawList* dl = L.dl;
    dl->PushClipRect(A, B, false);
    const float hint_px = 12.5f * S;
    const char* hint = live.present ? "Pad: D-pad moves  \xE2\x80\xA2  Start selects  \xE2\x80\xA2  Back returns"
                                    : "Click a button on the picture to bind the selected action.";
    {
        const ImVec2 ts = ui::text_size(ui::F.body, hint_px, hint);
        ui::text(dl, ui::F.body, hint_px, {(A.x + B.x - ts.x) * 0.5f, B.y - hint_px - 2.0f * S}, ui::kMuted, hint);
    }
    const float W = B.x - A.x;
    const float mon_R = std::min(50.0f * S, W * 0.14f);
    const float mon_cy = B.y - hint_px - 12.0f * S - 2.0f * 13.0f * S - 10.0f * S - mon_R;
    const float top = A.y + desc_h + 22.0f * S;
    const float avail_h = (mon_cy - mon_R - 16.0f * S) - top;
    // the unit: the callout columns (at +-47u) and their widest label must fit the pane
    float label_w = ui::text_size(ui::F.cond_med, 13.5f * S, "Move").x;
    for (int a = 0; a < pad::kPadActions; ++a)
        if (binds[std::size_t(a)] >= 0) label_w = std::max(label_w, ui::text_size(ui::F.cond_med, 13.5f * S, pad::action_short(a)).x);
    const float unit = std::max(0.5f, std::min((W * 0.5f - label_w - 10.0f * S) / 47.0f, avail_h / 76.0f));
    const ImVec2 pc{(A.x + B.x) * 0.5f, top + 40.0f * unit};

    // the mapper's view of this pad right now (the same FUN_005d2ba0 the game runs)
    input::Snapshot snap;
    snap.pad_present = live.present;
    auto to1024 = [](float v) { return std::clamp(int(v * 1024.0f), -1024, 1024); };
    snap.lx = to1024(live.lx); snap.ly = to1024(live.ly); snap.rx = to1024(live.rx); snap.ry = to1024(live.ry);
    {
        const int vy = (live.btn[pad::kDpadDown] ? 1 : 0) - (live.btn[pad::kDpadUp] ? 1 : 0);
        const int vx = (live.btn[pad::kDpadRight] ? 1 : 0) - (live.btn[pad::kDpadLeft] ? 1 : 0);
        static constexpr std::int32_t kPov[3][3] = {{31500, 0, 4500}, {27000, -1, 9000}, {22500, 18000, 13500}};
        snap.pov = kPov[vy + 1][vx + 1];
    }
    u.probe.configure(mapper_config(s));
    const input::Frame fr = u.probe.step(snap);

    // hover / click on the picture
    static std::vector<padpic::Part> parts;   // the previous frame's layout, for the hit test
    int hover = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool in_pane = ui::hovering(A, B) && !shell.capture.active;
    if (in_pane)
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            if (it->button >= 0 && it->hit(mouse)) { hover = it->button; break; }
    if (hover >= 0) ui::hand();
    if (hover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (sel_action >= 0) bind(sel_action, hover);
        else if (const int a = pad::action_on(binds, hover); a >= 0) L.sel = first_action_row + a;
    }
    const int focus = sel_action >= 0 ? binds[std::size_t(sel_action)] : -1;
    padpic::draw(dl, pc, unit, g, live, s, binds, focus, hover, parts);
    if (hover >= 0) {   // the hovered button: its name and action, over the picture
        const int a = pad::action_on(binds, hover);
        const std::string t = std::string(pad::button_name(g, hover)) + "  \xE2\x80\xA2  " +
                              (a >= 0 ? pad::action_short(a) : "unbound") +
                              (sel_action >= 0 ? std::string("   (click: ") + pad::action_short(sel_action) + ")" : "");
        const float px = 13.5f * S;
        const ImVec2 ts = ui::text_size(ui::F.medium, px, t.c_str());
        ui::text(dl, ui::F.medium, px, {(A.x + B.x - ts.x) * 0.5f, top - px - 4.0f * S}, ui::kTabOn, t.c_str());
    }

    // the stick monitors and the trigger bars
    {
        const float dzf = float(u.probe.dead_zone_int()) / 1024.0f;
        const bool aw = s.cleria.linear_stick_magnitude;
        // FUN_005d2ba0: (lX, lY) when outside the deadzone, else (lRx, lRy) when outside, else the hat
        const int dzi = u.probe.dead_zone_int();
        const bool on = s.input.game_pad != 0 && live.present;
        const bool left_drives = on && snap.lx * snap.lx + snap.ly * snap.ly >= dzi * dzi;
        const bool right_drives = on && !left_drives && snap.rx * snap.rx + snap.ry * snap.ry >= dzi * dzi;
        auto state = [&](bool drives, float x, float y, char* buf, std::size_t n) -> ImU32 {
            if (s.input.game_pad == 0) { std::snprintf(buf, n, "Gamepad off"); return ui::kMuted; }
            if (!drives) {
                const bool inside = x * x + y * y < dzf * dzf;
                std::snprintf(buf, n, inside ? "In the deadzone" : "Left stick wins");
                return ui::kMuted;
            }
            static const char* const kDir[16] = {"", "S", "W", "SW", "E", "SE", "", "", "N", "", "NW", "", "NE", "", "", ""};
            const bool run = fr.magnitude >= s.input.run_walk_threshold;
            if (aw) std::snprintf(buf, n, "%s %s  %.0f%%", run ? "Run" : "Walk", kDir[fr.dir & 15], double(std::min(fr.magnitude, 1.0f) * 100.0f));
            else std::snprintf(buf, n, "Run %s", kDir[fr.dir & 15]);
            return run ? ui::kGood : ui::kWarn;
        };
        char b1[48], b2[48];
        const ImU32 c1 = state(left_drives, live.lx, live.ly, b1, sizeof b1);
        const ImU32 c2 = state(right_drives, live.rx, live.ry, b2, sizeof b2);
        const float cx = (A.x + B.x) * 0.5f;
        // both sticks move: the right one when the left is in its deadzone (FUN_005d2ba0)
        padpic::monitor(dl, {cx - W * 0.30f, mon_cy}, mon_R, live.lx, live.ly, dzf, s.pad.outer_dead_zone, aw, left_drives, fr.dir,
                        "LEFT STICK", b1, c1);
        padpic::monitor(dl, {cx + W * 0.04f, mon_cy}, mon_R, live.rx, live.ry, dzf, s.pad.outer_dead_zone, aw, right_drives, fr.dir,
                        "RIGHT STICK", b2, c2);
        // triggers
        const float bw = 12.0f * S, bh = mon_R * 2.0f;
        for (int k = 0; k < 2; ++k) {
            const float x0 = cx + W * 0.27f + float(k) * (bw + 18.0f * S);
            const ImVec2 a{x0, mon_cy - mon_R}, b{x0 + bw, mon_cy + mon_R};
            const float v = k ? live.rt : live.lt;
            const bool lit = live_pressed(live, s, k ? pad::kRightTrigger : pad::kLeftTrigger);
            dl->AddRectFilled(a, b, IM_COL32(26, 25, 20, 255), 3.0f * S);
            if (v > 0.0f) dl->AddRectFilled({a.x, b.y - bh * v}, b, lit ? ui::kGold : IM_COL32(150, 136, 92, 255), 3.0f * S);
            dl->AddRect(a, b, ui::kRowEdge, 3.0f * S, 0, 1.0f * S);
            const float ty = b.y - bh * s.pad.trigger_threshold;
            dl->AddLine({a.x - 3.0f * S, ty}, {b.x + 3.0f * S, ty}, IM_COL32(214, 110, 80, 220), 1.2f * S);
            const char* nm = pad::button_short(g, k ? pad::kRightTrigger : pad::kLeftTrigger);
            const float tpx = 13.0f * S;
            const ImVec2 ts = ui::text_size(ui::F.cond, tpx, nm);
            ui::text(dl, ui::F.cond, tpx, {(a.x + b.x - ts.x) * 0.5f, b.y + 5.0f * S}, ui::kSection, nm);
        }
    }
    dl->PopClipRect();
}
