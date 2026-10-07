// CleriaCore's shell UI kit: the look of the launcher and the F1 settings panel (modelled on
// Dusklight's). Pages call only this small API (ui::section / toggle / slider / choice / button);
// Dear ImGui is just the engine underneath — text rendering, clipping, input — and none of its
// own widgets are drawn, so the backend can be swapped without touching any page.
#include "imgui_internal.h"   // ImGui::ShadeVertsLinearColorGradientKeepAlpha

namespace ui {

// ---- palette (sampled from the reference screenshots) ----------------------------------------
constexpr ImU32 kDim        = IM_COL32(0, 0, 0, 110);       // over the game behind the panel
constexpr ImU32 kPanel      = IM_COL32(22, 21, 16, 238);
constexpr ImU32 kPanelEdge  = IM_COL32(156, 132, 66, 225);
constexpr ImU32 kRule       = IM_COL32(120, 104, 58, 190);
constexpr ImU32 kDivider    = IM_COL32(58, 55, 44, 255);
constexpr ImU32 kGold       = IM_COL32(214, 176, 62, 255);
constexpr ImU32 kTabOff     = IM_COL32(141, 135, 122, 255);
constexpr ImU32 kTabOn      = IM_COL32(244, 240, 230, 255);
constexpr ImU32 kSection    = IM_COL32(112, 107, 92, 255);
constexpr ImU32 kRow        = IM_COL32(31, 30, 23, 255);
constexpr ImU32 kRowEdge    = IM_COL32(52, 50, 41, 255);
constexpr ImU32 kRowSel     = IM_COL32(43, 41, 26, 255);
constexpr ImU32 kLabel      = IM_COL32(234, 230, 218, 255);
constexpr ImU32 kValue      = IM_COL32(207, 202, 189, 255);
constexpr ImU32 kMuted      = IM_COL32(124, 120, 106, 255);
constexpr ImU32 kDesc       = IM_COL32(221, 216, 204, 255);
constexpr ImU32 kGood       = IM_COL32(190, 214, 106, 255);
constexpr ImU32 kWarn       = IM_COL32(230, 176, 64, 255);

struct Fonts { ImFont* body = nullptr; ImFont* medium = nullptr; ImFont* bold = nullptr;
               ImFont* cond = nullptr; ImFont* cond_med = nullptr; };
inline Fonts F;
inline float S = 1.0f;          // UI scale: the window height over the 1125-px reference

// Fira Sans / Fira Sans Condensed (SIL OFL), shipped next to the exe in fonts/.
inline bool load_fonts(const fs::path& dir) {
    ImGuiIO& io = ImGui::GetIO();
    auto load = [&](const char* f) -> ImFont* {
        const fs::path p = dir / f;
        std::error_code ec;
        if (!fs::exists(p, ec)) return nullptr;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        return io.Fonts->AddFontFromFileTTF(p.string().c_str(), 18.0f, &cfg);
    };
    F.body = load("FiraSans-Regular.ttf");
    F.medium = load("FiraSans-Medium.ttf");
    F.bold = load("FiraSans-SemiBold.ttf");
    F.cond = load("FiraSansCondensed-SemiBold.ttf");
    F.cond_med = load("FiraSansCondensed-Medium.ttf");
    ImFont* fallback = io.Fonts->AddFontDefault();
    for (ImFont** f : {&F.body, &F.medium, &F.bold, &F.cond, &F.cond_med}) if (!*f) *f = fallback;
    return F.body != fallback;
}

inline std::string upper(const char* s) {
    std::string u(s);
    for (char& c : u) c = char(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
inline ImVec2 text_size(ImFont* f, float px, const char* t) { return f->CalcTextSizeA(px, FLT_MAX, 0.0f, t); }
inline void text(ImDrawList* dl, ImFont* f, float px, ImVec2 p, ImU32 c, const char* t) { dl->AddText(f, px, p, c, t); }

// ---- input, read once per frame ----------------------------------------------------------------
struct Nav {
    bool up = false, down = false, left = false, right = false, ok = false, back = false;
    bool tab_prev = false, tab_next = false;
};
// The mouse over [a, b]. Unclipped: the kit draws on the foreground list outside any ImGui
// window, and the clipped test would clip to the implicit 400x400 debug window.
inline bool hovering(const ImVec2& a, const ImVec2& b) { return ImGui::IsMouseHoveringRect(a, b, false); }

// The pad's share of the navigation (view_pad.inl): [cleria.pad] SwapConfirmCancel swaps its OK and
// Back, and while the Controller page is drawn (pad_test_frame = that frame) the pad is free for the
// live test: only the d-pad moves and Start activates; the sticks, face buttons and bumpers just light up.
inline bool pad_swap_ok = false;
inline int pad_test_frame = -100;
// While a text_field is being edited (its frame, below) the keys are typing, not navigation.
inline int text_edit_frame = -100;

inline Nav read_nav() {
    Nav n;
    if (ImGui::GetFrameCount() - text_edit_frame <= 1) return n;   // a text_field owns the keys
    auto pr = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const bool test = ImGui::GetFrameCount() - pad_test_frame <= 1;
    auto st = [&](ImGuiKey k) { return !test && pr(k); };   // a pad key the live test frees
    const ImGuiKey pad_ok = pad_swap_ok ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
    const ImGuiKey pad_back = pad_swap_ok ? ImGuiKey_GamepadFaceDown : ImGuiKey_GamepadFaceRight;
    n.up = pr(ImGuiKey_UpArrow) || pr(ImGuiKey_W) || pr(ImGuiKey_GamepadDpadUp) || st(ImGuiKey_GamepadLStickUp);
    n.down = pr(ImGuiKey_DownArrow) || pr(ImGuiKey_S) || pr(ImGuiKey_GamepadDpadDown) || st(ImGuiKey_GamepadLStickDown);
    n.left = pr(ImGuiKey_LeftArrow) || pr(ImGuiKey_A) || pr(ImGuiKey_GamepadDpadLeft) || st(ImGuiKey_GamepadLStickLeft);
    n.right = pr(ImGuiKey_RightArrow) || pr(ImGuiKey_D) || pr(ImGuiKey_GamepadDpadRight) || st(ImGuiKey_GamepadLStickRight);
    n.ok = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
           ImGui::IsKeyPressed(test ? ImGuiKey_GamepadStart : pad_ok, false);
    n.back = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
             (!test && ImGui::IsKeyPressed(pad_back, false));
    n.tab_prev = pr(ImGuiKey_Q) || st(ImGuiKey_GamepadL1) || pr(ImGuiKey_PageUp);
    n.tab_next = pr(ImGuiKey_E) || st(ImGuiKey_GamepadR1) || pr(ImGuiKey_PageDown) ||
                 ImGui::IsKeyPressed(ImGuiKey_Tab, true);
    return n;
}

// ---- the row list of one settings page ---------------------------------------------------------
// Immediate mode: a page calls the row functions every frame in order; the list numbers them,
// draws them, keeps the selected one in view and reports its description.
struct List {
    int sel = 0;                    // the selected row (keyboard / pad focus)
    float scroll = 0, scroll_to = 0;
    // per frame
    ImDrawList* dl = nullptr;
    ImVec2 org{}, size{};
    float y = 0;                    // layout cursor (content space)
    int n = 0;                      // rows laid out so far
    int count_last = 0;             // rows of the previous frame
    float content_h = 0;
    std::string desc;
    Nav nav{};
    bool focus = true;              // the panel has input
    bool mouse_moved = false;
    std::vector<float> row_y;       // each row's content y this frame (for keep-in-view)
    std::vector<bool> focusable, focusable_last;   // read-only rows are skipped by the keys
};

inline float row_h() { return 34.0f * S; }
inline float row_gap() { return 4.0f * S; }

inline void list_begin(List& L, ImDrawList* dl, ImVec2 org, ImVec2 size, const Nav& nav, bool focus) {
    L.dl = dl; L.org = org; L.size = size; L.nav = nav; L.focus = focus;
    L.y = 4.0f * S; L.n = 0; L.desc.clear(); L.row_y.clear(); L.focusable.clear();
    ImGuiIO& io = ImGui::GetIO();
    L.mouse_moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    if (focus && L.count_last > 0 && int(L.focusable_last.size()) == L.count_last && (nav.up || nav.down)) {
        const int step = nav.up ? L.count_last - 1 : 1;
        int s = L.sel;
        for (int k = 0; k < L.count_last; ++k) {
            s = (s + step) % L.count_last;
            if (L.focusable_last[s]) { L.sel = s; break; }
        }
    }
    if (focus && hovering(org, {org.x + size.x, org.y + size.y}))
        L.scroll_to -= io.MouseWheel * 60.0f * S;
    dl->PushClipRect(org, {org.x + size.x, org.y + size.y}, true);
}

inline void list_end(List& L) {
    L.dl->PopClipRect();
    L.count_last = L.n;
    L.focusable_last = L.focusable;
    L.content_h = L.y;
    if (L.n > 0) L.sel = std::clamp(L.sel, 0, L.n - 1);
    if (L.n > 0 && !L.focusable[L.sel])                 // land on the first row that takes focus
        for (int k = 0; k < L.n; ++k) if (L.focusable[k]) { L.sel = k; break; }
    // keep the selected row in view when the keyboard moved it
    if ((L.nav.up || L.nav.down) && L.sel < int(L.row_y.size())) {
        const float top = L.row_y[L.sel], bot = top + row_h();
        if (top - L.scroll_to < 8.0f * S) L.scroll_to = top - 40.0f * S;
        if (bot - L.scroll_to > L.size.y - 8.0f * S) L.scroll_to = bot - L.size.y + 40.0f * S;
    }
    L.scroll_to = std::clamp(L.scroll_to, 0.0f, std::max(0.0f, L.content_h - L.size.y));
    L.scroll += (L.scroll_to - L.scroll) * 0.35f;
    if (std::fabs(L.scroll_to - L.scroll) < 0.5f) L.scroll = L.scroll_to;
    // the scroll bar: a thin track at the right edge, only when the page overflows
    if (L.content_h > L.size.y + 1.0f) {
        const float x = L.org.x + L.size.x - 4.0f * S;
        const float h = L.size.y * L.size.y / L.content_h;
        const float t = (L.size.y - h) * (L.scroll / std::max(1.0f, L.content_h - L.size.y));
        L.dl->AddRectFilled({x, L.org.y + t}, {x + 3.0f * S, L.org.y + t + h}, IM_COL32(90, 84, 66, 255), 2.0f * S);
    }
}

inline void section(List& L, const char* title) {
    L.y += (L.n > 0 ? 14.0f : 2.0f) * S;
    const float px = 18.0f * S;
    text(L.dl, F.cond, px, {L.org.x + 1.0f * S, L.org.y + L.y - L.scroll}, kSection, upper(title).c_str());
    L.y += px + 8.0f * S;
}

struct RowIo { bool selected = false, activate = false; int delta = 0; ImVec2 a{}, b{}; };

// Lay out and draw one row's frame; returns its input this frame.
inline RowIo row_frame(List& L, const char* desc, bool enabled, bool focusable = true) {
    RowIo r;
    const int i = L.n++;
    L.row_y.push_back(L.y);
    L.focusable.push_back(focusable);
    const float w = L.size.x - 12.0f * S;
    r.a = {L.org.x, L.org.y + L.y - L.scroll};
    r.b = {r.a.x + w, r.a.y + row_h()};
    L.y += row_h() + row_gap();
    const bool hover = L.focus && hovering(r.a, r.b) &&
                       hovering(L.org, {L.org.x + L.size.x, L.org.y + L.size.y});
    if (hover && L.mouse_moved && focusable) L.sel = i;
    r.selected = i == L.sel;
    if (r.selected && desc) L.desc = desc;
    if (enabled && L.focus) {
        if (r.selected) {
            if (L.nav.ok) r.activate = true;
            if (L.nav.left) r.delta = -1;
            if (L.nav.right) r.delta = +1;
        }
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { L.sel = i; r.selected = true; r.activate = true; }
        if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { L.sel = i; r.selected = true; r.delta = -1; }
    }
    const float rnd = 4.0f * S;
    if (r.selected) {
        L.dl->AddRectFilled(r.a, r.b, kRowSel, rnd);
        L.dl->AddRect(r.a, r.b, kGold, rnd, 0, 2.0f * S);
    } else {
        L.dl->AddRectFilled(r.a, r.b, kRow, rnd);
        L.dl->AddRect(r.a, r.b, kRowEdge, rnd, 0, 1.0f * S);
    }
    return r;
}

inline void row_texts(List& L, const RowIo& r, const char* label, const char* value, bool enabled) {
    const float lpx = 16.5f * S, vpx = 17.0f * S;
    const float cy = (r.a.y + r.b.y) * 0.5f;
    text(L.dl, F.cond, lpx, {r.a.x + 13.0f * S, cy - lpx * 0.55f}, enabled ? kLabel : kMuted, upper(label).c_str());
    if (!value) return;
    const ImVec2 vs = text_size(r.selected ? F.medium : F.body, vpx, value);
    float x = r.b.x - 13.0f * S - vs.x;
    if (r.selected && enabled) {                       // "• On": the gold marker of the edited value
        L.dl->AddCircleFilled({x - 8.0f * S, cy}, 2.2f * S, kGold);
        text(L.dl, F.medium, vpx, {x, cy - vpx * 0.55f}, kTabOn, value);
    } else {
        text(L.dl, F.body, vpx, {x, cy - vpx * 0.55f}, enabled ? kValue : kMuted, value);
    }
}

inline bool toggle(List& L, const char* label, bool* v, const char* desc = nullptr, bool enabled = true) {
    const RowIo r = row_frame(L, desc, enabled);
    bool changed = false;
    if (r.activate || r.delta != 0) { *v = !*v; changed = true; }
    row_texts(L, r, label, *v ? "On" : "Off", enabled);
    return changed;
}

// A stepped value; `fmt` formats it (printf, the value as a float).
inline bool slider(List& L, const char* label, float* v, float lo, float hi, float step, const char* fmt,
                   const char* desc = nullptr, bool enabled = true) {
    const RowIo r = row_frame(L, desc, enabled);
    const float old = *v;
    if (r.delta != 0) *v = std::clamp(*v + step * float(r.delta), lo, hi);
    char buf[48];
    std::snprintf(buf, sizeof buf, fmt, *v);
    row_texts(L, r, label, buf, enabled);
    return *v != old;
}

inline bool choice(List& L, const char* label, int* idx, const char* const* items, int count,
                   const char* desc = nullptr, bool enabled = true) {
    const RowIo r = row_frame(L, desc, enabled);
    const int old = *idx;
    if (count > 0) {
        if (r.delta != 0) *idx = (*idx + r.delta + count) % count;
        else if (r.activate) *idx = (*idx + 1) % count;
    }
    row_texts(L, r, label, count > 0 ? items[std::clamp(*idx, 0, count - 1)] : "", enabled);
    return *idx != old;
}

// A label / value row whose activation starts something (a rebind): returns true on OK / click.
inline bool choice_label(List& L, const char* label, const char* value, const char* desc = nullptr, bool enabled = true) {
    const RowIo r = row_frame(L, desc, enabled);
    row_texts(L, r, label, value, enabled);
    return r.activate;
}

// A full-width action ("Configure Inputs"): centred text, no value.
inline bool button(List& L, const char* label, const char* desc = nullptr, bool enabled = true) {
    const RowIo r = row_frame(L, desc, enabled);
    const float px = 17.0f * S;
    const ImVec2 ts = text_size(F.body, px, label);
    text(L.dl, F.body, px, {(r.a.x + r.b.x - ts.x) * 0.5f, (r.a.y + r.b.y) * 0.5f - px * 0.55f},
         enabled ? kLabel : kMuted, label);
    return r.activate;
}

// A read-only line (a path, a status) inside a page.
inline void info(List& L, const char* label, const char* value, ImU32 value_col = kValue, const char* desc = nullptr) {
    RowIo r = row_frame(L, desc, false, /*focusable*/false);
    const float lpx = 16.5f * S, vpx = 16.0f * S;
    const float cy = (r.a.y + r.b.y) * 0.5f;
    text(L.dl, F.cond, lpx, {r.a.x + 13.0f * S, cy - lpx * 0.55f}, kLabel, upper(label).c_str());
    const ImVec2 vs = text_size(F.body, vpx, value);
    const float maxw = (r.b.x - r.a.x) * 0.62f;
    std::string v = value;
    if (vs.x > maxw) {                                  // keep the tail of long paths
        while (v.size() > 4 && text_size(F.body, vpx, ("..." + v).c_str()).x > maxw) v.erase(0, 1);
        v = "..." + v;
    }
    const ImVec2 vs2 = text_size(F.body, vpx, v.c_str());
    text(L.dl, F.body, vpx, {r.b.x - 13.0f * S - vs2.x, cy - vpx * 0.55f}, value_col, v.c_str());
}

// The first row of a sub-page (build/shell_re/SHELL_CORE.md, tab policy): "<  MODS / HELLO LUA". OK / click
// returns true (go back), as Esc / Backspace / the pad's Back do through Page::back.
inline bool back_row(List& L, const char* parent, const char* title, const char* desc = nullptr) {
    const RowIo r = row_frame(L, desc ? desc : "Back. (Esc / Backspace / the pad's Back also return.)", true);
    const float lpx = 16.5f * S, cy = (r.a.y + r.b.y) * 0.5f;
    float x = r.a.x + 13.0f * S;
    const ImU32 arrow = r.selected ? kGold : kTabOff;   // a chevron pointing back
    L.dl->AddLine({x + 6.0f * S, cy - 6.0f * S}, {x, cy}, arrow, 1.8f * S);
    L.dl->AddLine({x, cy}, {x + 6.0f * S, cy + 6.0f * S}, arrow, 1.8f * S);
    x += 18.0f * S;
    const std::string p = upper(parent), sep = "  /  ", t = upper(title);
    text(L.dl, F.cond, lpx, {x, cy - lpx * 0.55f}, kTabOff, p.c_str());
    x += text_size(F.cond, lpx, p.c_str()).x;
    text(L.dl, F.cond, lpx, {x, cy - lpx * 0.55f}, kMuted, sep.c_str());
    x += text_size(F.cond, lpx, sep.c_str()).x;
    text(L.dl, F.cond, lpx, {x, cy - lpx * 0.55f}, kGold, t.c_str());
    return r.activate;
}

// ---- the settings panel ------------------------------------------------------------------------
// A tab. `back` (optional): the page shows a sub-page and Esc / Back should return to the page instead of
// closing the panel (returns true when it went back). `open_sub` (optional): open a named sub-page
// ("Mods:sample.hello" -> "sample.hello"), for CLERIA_SHELL_PAGE / the drive's shell= (Panel::select).
struct Page {
    std::string name;
    std::function<void(List&)> draw;
    List list;
    std::function<bool(List&)> back;
    std::function<bool(const std::string&)> open_sub;
};

struct Panel {
    std::vector<Page> pages;
    int tab = 0;
    bool open = false;
    ImVec2 side_a{}, side_b{};   // this frame's right-hand pane (under the description), for a page's picture

    // Adds a tab, or replaces the one of that name in place (the Debug page is re-added after each Return to Title).
    Page& add(const std::string& name, std::function<void(List&)> draw) {
        for (Page& p : pages)
            if (p.name == name) { p.draw = std::move(draw); return p; }
        pages.push_back({name, std::move(draw), {}, {}, {}});
        return pages.back();
    }
    // "Tab" or "Tab:sub" (CLERIA_SHELL_PAGE, the drive's shell=): select that tab and open its sub-page.
    bool select(const std::string& spec) {
        const std::size_t c = spec.find(':');
        const std::string tab_name = spec.substr(0, c), sub = c == std::string::npos ? std::string() : spec.substr(c + 1);
        for (int i = 0; i < int(pages.size()); ++i) {
            if (pages[std::size_t(i)].name != tab_name) continue;
            tab = i;
            Page& p = pages[std::size_t(i)];
            if (sub.empty()) {                          // the tab itself: leave a sub-page it shows
                if (p.back) p.back(p.list);
                return true;
            }
            return p.open_sub && p.open_sub(sub);
        }
        return false;
    }

    // Draw the panel centred in a w x h screen; returns false when it was closed this frame.
    bool draw(ImDrawList* dl, float w, float h, const Nav& nav) {
        if (!open || pages.empty()) return open;
        if (nav.back) {                              // a sub-page goes back to its page; a page closes the panel
            Page& cur = pages[std::size_t(std::clamp(tab, 0, int(pages.size()) - 1))];
            if (!(cur.back && cur.back(cur.list))) { open = false; return false; }
        }
        dl->AddRectFilled({0, 0}, {w, h}, kDim);
        const float pw = std::min(w - 40.0f * S, 850.0f * S), ph = std::min(h - 40.0f * S, 600.0f * S);
        const ImVec2 a{(w - pw) * 0.5f, (h - ph) * 0.5f}, b{a.x + pw, a.y + ph};
        dl->AddRectFilled(a, b, kPanel, 8.0f * S);
        dl->AddRect(a, b, kPanelEdge, 8.0f * S, 0, 1.5f * S);
        // tabs
        if (nav.tab_prev) tab = (tab + int(pages.size()) - 1) % int(pages.size());
        if (nav.tab_next) tab = (tab + 1) % int(pages.size());
        float tpx = 16.0f * S, gap = 37.0f * S;
        const float tab_h = 50.0f * S;
        {   // many tabs: narrow the gaps, then the type, so the row stays clear of the close cross
            float sum = 0;
            for (const Page& p : pages) sum += text_size(F.cond, tpx, upper(p.name.c_str()).c_str()).x;
            const float avail = pw - 19.0f * S - 56.0f * S, n = float(pages.size());
            if (sum + gap * n > avail) gap = std::max(14.0f * S, (avail - sum) / n);
            if (sum + gap * n > avail) tpx *= std::max(0.6f, (avail - gap * n) / sum);
        }
        float x = a.x + 19.0f * S;
        for (int i = 0; i < int(pages.size()); ++i) {
            const std::string t = upper(pages[i].name.c_str());
            const ImVec2 ts = text_size(F.cond, tpx, t.c_str());
            const ImVec2 ta{x - 10.0f * S, a.y}, tb{x + ts.x + 10.0f * S, a.y + tab_h};
            if (hovering(ta, tb) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) tab = i;
            const bool on = i == tab;
            text(dl, F.cond, tpx, {x, a.y + (tab_h - tpx) * 0.5f}, on ? kTabOn : kTabOff, t.c_str());
            if (on) dl->AddRectFilled({ta.x, tb.y - 3.0f * S}, {tb.x, tb.y}, kGold);
            x += ts.x + gap;
        }
        // the close cross
        {
            const ImVec2 c{b.x - 26.0f * S, a.y + tab_h * 0.5f};
            const float r = 6.0f * S;
            const bool hov = hovering({c.x - 14 * S, c.y - 14 * S}, {c.x + 14 * S, c.y + 14 * S});
            const ImU32 col = hov ? kTabOn : kTabOff;
            dl->AddLine({c.x - r, c.y - r}, {c.x + r, c.y + r}, col, 1.6f * S);
            dl->AddLine({c.x - r, c.y + r}, {c.x + r, c.y - r}, col, 1.6f * S);
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { open = false; return false; }
        }
        dl->AddLine({a.x, a.y + tab_h}, {b.x, a.y + tab_h}, kRule, 1.0f * S);
        // the list (left) and the description (right)
        const float split = a.x + pw * 0.5f;
        Page& pg = pages[std::clamp(tab, 0, int(pages.size()) - 1)];
        List& L = pg.list;
        const ImVec2 lo{a.x + 19.0f * S, a.y + tab_h + 14.0f * S};
        side_a = {split + 20.0f * S, a.y + tab_h + 20.0f * S}; side_b = {b.x - 20.0f * S, b.y - 12.0f * S};
        list_begin(L, dl, lo, {split - lo.x - 6.0f * S, b.y - lo.y - 12.0f * S}, nav, true);
        pg.draw(L);
        list_end(L);
        dl->AddLine({split, a.y + tab_h}, {split, b.y}, kDivider, 1.0f * S);
        if (!L.desc.empty()) {
            const float dpx = 17.5f * S;
            dl->AddText(F.body, dpx, {split + 20.0f * S, a.y + tab_h + 20.0f * S}, kDesc, L.desc.c_str(), nullptr,
                        b.x - split - 40.0f * S);
        }
        return open;
    }
};

// ---- the launcher's main menu -----------------------------------------------------------------
struct MainMenu {
    int sel = 0;
    // Returns the activated item (0..n-1) or -1.
    int draw(ImDrawList* dl, float w, float h, const Nav& nav, const char* const* items, int n, bool focus) {
        // the left darkening, so the menu reads over any background
        dl->AddRectFilledMultiColor({0, 0}, {w * 0.62f, h}, IM_COL32(0, 0, 0, 205), IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 205));
        dl->AddRectFilledMultiColor({0, h * 0.72f}, {w, h}, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
        // the wordmark: a small "presents" line over a big gold-gradient title
        const float x0 = 75.0f * S;
        const float ppx = 30.0f * S;
        text(dl, F.bold, ppx, {x0, 330.0f * S}, IM_COL32(246, 242, 232, 255), "CLERIACORE");
        const float cw = text_size(F.bold, ppx, "CLERIACORE ").x;
        text(dl, F.medium, ppx * 0.86f, {x0 + cw, 330.0f * S + ppx * 0.1f}, IM_COL32(220, 214, 200, 255), "PRESENTS");
        const float tpx = 128.0f * S;
        const ImVec2 tp{x0 - 6.0f * S, 372.0f * S};
        text(dl, F.bold, tpx, {tp.x + 4.0f * S, tp.y + 5.0f * S}, IM_COL32(0, 0, 0, 150), "YS ORIGIN");   // shadow
        const int v0 = dl->VtxBuffer.Size;
        text(dl, F.bold, tpx, tp, IM_COL32(255, 255, 255, 255), "YS ORIGIN");
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, {0, tp.y + tpx * 0.18f},
                                                      {0, tp.y + tpx * 0.98f}, IM_COL32(255, 214, 92, 255),
                                                      IM_COL32(236, 112, 26, 255));
        // the items
        if (focus) {
            if (nav.up) sel = (sel + n - 1) % n;
            if (nav.down) sel = (sel + 1) % n;
        }
        int hit = -1;
        const float ipx = 27.0f * S, ih = 44.0f * S, step = 54.0f * S;
        for (int i = 0; i < n; ++i) {
            const ImVec2 a{x0, 640.0f * S + step * float(i)}, b{a.x + 330.0f * S, a.y + ih};
            if (focus && hovering(a, b)) {
                if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) sel = i;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { sel = i; hit = i; }
            }
            const bool on = i == sel;
            if (on)
                dl->AddRectFilledMultiColor(a, b, IM_COL32(238, 212, 128, 255), IM_COL32(200, 168, 80, 0),
                                            IM_COL32(200, 168, 80, 0), IM_COL32(238, 212, 128, 255));
            text(dl, F.cond_med, ipx, {a.x + 13.0f * S, a.y + (ih - ipx) * 0.5f},
                 on ? IM_COL32(28, 22, 10, 255) : IM_COL32(244, 240, 230, 255), upper(items[i]).c_str());
        }
        if (focus && nav.ok) hit = sel;
        return hit;
    }
};

// Bottom-left status: a check (or a warning) and a muted second line.
inline void status(ImDrawList* dl, float h, bool ok, const char* line1, const char* line2) {
    const float x = 75.0f * S, y = h - 100.0f * S, px = 22.0f * S;
    const ImU32 col = ok ? kGood : kWarn;
    if (ok) {
        dl->AddLine({x, y + px * 0.55f}, {x + 5.0f * S, y + px * 0.8f}, col, 1.8f * S);
        dl->AddLine({x + 5.0f * S, y + px * 0.8f}, {x + 12.0f * S, y + px * 0.25f}, col, 1.8f * S);
    } else {
        dl->AddTriangle({x + 6.0f * S, y + px * 0.15f}, {x, y + px * 0.9f}, {x + 12.0f * S, y + px * 0.9f}, col, 1.6f * S);
    }
    text(dl, F.body, px, {x + 22.0f * S, y}, col, line1);
    if (line2) text(dl, F.body, 18.0f * S, {x, y + 34.0f * S}, IM_COL32(150, 145, 130, 255), line2);
}

// Bottom-right: the version.
inline void version(ImDrawList* dl, float w, float h, const char* v) {
    const float px = 21.0f * S;
    const ImVec2 ts = text_size(F.body, px, v);
    text(dl, F.body, px, {w - 75.0f * S - ts.x, h - 100.0f * S}, IM_COL32(236, 232, 222, 255), v);
}

// A centred modal card (the rebind prompt): a gold-ruled box over a dimmed panel.
inline void prompt(ImDrawList* dl, float w, float h, const char* head, const char* what, const char* hint) {
    dl->AddRectFilled({0, 0}, {w, h}, IM_COL32(0, 0, 0, 120));
    const float bw = 420.0f * S, bh = 150.0f * S;
    const ImVec2 a{(w - bw) * 0.5f, (h - bh) * 0.5f}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, kPanel, 8.0f * S);
    dl->AddRect(a, b, kGold, 8.0f * S, 0, 1.5f * S);
    auto centred = [&](ImFont* f, float px, float y, ImU32 c, const char* t) {
        const ImVec2 ts = text_size(f, px, t);
        text(dl, f, px, {(a.x + b.x - ts.x) * 0.5f, y}, c, t);
    };
    centred(F.cond, 15.0f * S, a.y + 22.0f * S, kSection, upper(head).c_str());
    centred(F.bold, 26.0f * S, a.y + 50.0f * S, kTabOn, upper(what).c_str());
    centred(F.body, 15.5f * S, a.y + 106.0f * S, kMuted, hint);
}

// Bottom-centre: a notice card.
inline void toast(ImDrawList* dl, float w, float h, const char* head, const char* body) {
    const float bw = 340.0f * S, bh = 74.0f * S;
    const ImVec2 a{(w - bw) * 0.5f, h - 105.0f * S}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, IM_COL32(26, 24, 16, 235), 6.0f * S);
    dl->AddRect(a, b, kPanelEdge, 6.0f * S, 0, 1.2f * S);
    text(dl, F.cond, 13.5f * S, {a.x + 19.0f * S, a.y + 15.0f * S}, kWarn, upper(head).c_str());
    text(dl, F.body, 16.0f * S, {a.x + 19.0f * S, a.y + 40.0f * S}, kDesc, body);
}

// ---- a one-line text field (e.g. a Lua mod's server / slot / password) ----------------------------
// OK / click on the row starts editing; typed text (SDL text input, so any keyboard layout), Ctrl+V
// pastes, Backspace deletes (Ctrl+Backspace clears), Enter or a click elsewhere keeps the text, Esc
// restores it. While editing, read_nav() yields nothing, so W/A/S/D/Q/E/Space type instead of
// navigating. `secret` shows a bullet per character (the text itself is never drawn). Returns true
// when an edit was kept and changed the value. The drive_text_* hooks stand in for the keyboard in a
// CLERIA_DRIVE run.
struct TextEdit {
    const void* id = nullptr;       // the value being edited
    std::string buf;
    int frame = -100;               // the last frame it was drawn active
};
inline TextEdit text_edit;
inline std::string drive_text_activate;    // the field with this label starts editing
inline std::string drive_text_input;       // typed into the active field
inline bool drive_text_commit = false;     // Enter on the active field

inline void utf8_append(std::string& s, unsigned c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { s += char(0xE0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
    else {
        s += char(0xF0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 0x3F));
        s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F));
    }
}
inline void utf8_pop(std::string& s) {
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}
inline std::size_t utf8_count(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

inline bool text_field(List& L, const char* label, std::string* v, const char* desc = nullptr, bool secret = false,
                       const char* empty = "(not set)", std::size_t max_len = 128) {
    const RowIo r = row_frame(L, desc, true);
    TextEdit& e = text_edit;
    const int now = ImGui::GetFrameCount();
    bool active = e.id == v && now - e.frame <= 1, kept = false, started = false;
    if (!active && (r.activate || drive_text_activate == label)) {
        e.id = v;
        e.buf = *v;
        active = started = true;
        drive_text_activate.clear();
        L.sel = L.n - 1;
    }
    if (active) {
        e.frame = text_edit_frame = now;
        ImGui::GetCurrentContext()->PlatformImeData.WantTextInput = true;   // the backend's SDL_StartTextInput
        ImGuiIO& io = ImGui::GetIO();
        if (!started) {                                  // the key / click that opened it is not an edit
            for (ImWchar c : io.InputQueueCharacters)
                if (c >= 0x20 && c != 0x7F && e.buf.size() < max_len) utf8_append(e.buf, c);
            for (char c : drive_text_input)
                if (e.buf.size() < max_len && static_cast<unsigned char>(c) >= 0x20) e.buf += c;
            drive_text_input.clear();
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) { if (io.KeyCtrl) e.buf.clear(); else utf8_pop(e.buf); }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
                if (const char* clip = ImGui::GetClipboardText())
                    for (const char* p = clip; *p && *p != '\r' && *p != '\n' && e.buf.size() < max_len; ++p) e.buf += *p;
            const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                               drive_text_commit;
            const bool away = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovering(r.a, r.b);
            drive_text_commit = false;
            if (enter || away) {
                kept = e.buf != *v;
                *v = e.buf;
                e.id = nullptr;
                active = false;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                e.id = nullptr;                          // the old text stays
                active = false;
            }
        }
    }
    // the value, right-aligned like every row; while editing, the text and a caret in a dark well
    const std::string& src = active ? e.buf : *v;
    std::string shown;
    if (secret) for (std::size_t i = utf8_count(src); i > 0; --i) shown += "\xE2\x80\xA2";
    else shown = src;
    const float lpx = 16.5f * S, vpx = 17.0f * S, cy = (r.a.y + r.b.y) * 0.5f;
    text(L.dl, F.cond, lpx, {r.a.x + 13.0f * S, cy - lpx * 0.55f}, kLabel, upper(label).c_str());
    const float maxw = (r.b.x - r.a.x) * 0.58f;
    if (active) {
        const ImVec2 wa{r.b.x - 10.0f * S - maxw, r.a.y + 5.0f * S}, wb{r.b.x - 8.0f * S, r.b.y - 5.0f * S};
        L.dl->AddRectFilled(wa, wb, IM_COL32(14, 13, 10, 255), 3.0f * S);
        L.dl->AddRect(wa, wb, kGold, 3.0f * S, 0, 1.0f * S);
        std::string t = shown;                           // keep the caret end in view
        while (!t.empty() && text_size(F.body, vpx, t.c_str()).x > maxw - 24.0f * S) t.erase(0, 1);
        while (!t.empty() && (static_cast<unsigned char>(t.front()) & 0xC0) == 0x80) t.erase(0, 1);
        const float tw = text_size(F.body, vpx, t.c_str()).x;
        const float x = wb.x - 12.0f * S - tw;
        text(L.dl, F.body, vpx, {x, cy - vpx * 0.55f}, kTabOn, t.c_str());
        if ((now / 30) % 2 == 0)
            L.dl->AddLine({x + tw + 2.0f * S, cy - vpx * 0.5f}, {x + tw + 2.0f * S, cy + vpx * 0.5f}, kGold, 1.5f * S);
    } else {
        std::string t = shown.empty() ? std::string(empty) : shown;
        const ImU32 col = shown.empty() ? kMuted : (r.selected ? kTabOn : kValue);
        if (text_size(F.body, vpx, t.c_str()).x > maxw) {
            while (t.size() > 4 && text_size(F.body, vpx, (t + "...").c_str()).x > maxw) utf8_pop(t);
            t += "...";
        }
        ImFont* f = r.selected ? F.medium : F.body;
        const ImVec2 vs = text_size(f, vpx, t.c_str());
        text(L.dl, f, vpx, {r.b.x - 13.0f * S - vs.x, cy - vpx * 0.55f}, col, t.c_str());
    }
    return kept;
}

}  // namespace ui
