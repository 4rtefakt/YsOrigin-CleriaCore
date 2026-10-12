// The Time Attack / Arena stage select: build/mode_re/STAGE_SELECT.md. See stageselect.hpp.
#include "cleria/stageselect.hpp"

#include <algorithm>
#include <cstdio>

#include "cleria/mouse.hpp"

namespace cleria {

namespace stages {
namespace {
// [BYTES .rdata 0x68c4cc -> the four name tables; 0x68cba4 -> the scene tables]
constexpr const char* kNames[4][14] = {
    {"Boss Rush", "Vagullion", "Velagunder", "Epona 1", "Nygtilger", "Gelaldy", "Jenocres", "Khonsclard", "Pictimos", "Epona 2",
     "Zava", "Kishgal", "Dalles", "Darm"},
    {"Boss Rush", "Vagullion", "Velagunder", "Epona 1", "Nygtilger", "Gelaldy", "Jenocres", "Khonsclard", "Pictimos", "Epona 2",
     "Zava", "Kishgal", "Dalles", nullptr},
    {"Boss Rush", "Epona", "Velagunder", "Vagullion", "Nygtilger", "Gelaldy", "Jenocres", "Khonsclard", "Pictimos", "Kishgal",
     "Zava", "Toal Fact", "Dalles", nullptr},
    {"Boss Rush", "Galleon", "Velagunder", "Hugo Fact 1", "Nygtilger", "Shion", "Gelaldy", "Khonsclard", "Pictimos", "Kishgal",
     "Zava", "Hugo Fact 2", "Dalles", "Darm"},
};
constexpr const char* kScenes[14] = {"S_1080", "S_1080", "S_1099", "S_2080", "S_2099", "S_3099", "S_4013", "S_4099", "S_5099",
                                     "S_6080", "S_6082", "S_6099", "S_7080", "S_7099"};
constexpr const char* kScenesToal[14] = {"S_1080", "S_1080", "S_1099", "S_2080", "S_2099", "S_3080", "S_3099", "S_4099", "S_5099",
                                         "S_6080", "S_6082", "S_6099", "S_7080", "S_7099"};
constexpr int kCount[4] = {14, 13, 13, 14};                                       // 0x68c460
constexpr const char* kArenaNames[7] = {"Wailing Blue", "Flooded Prison", "Guilty Fire", "Silent Sands", "Blighted Blood",
                                        "Demonic Core", "???"};                    // 0x68c57c
constexpr const char* kArenaScenes[7] = {"S_9109", "S_9119", "S_9129", "S_9139", "S_9149", "S_9159", "S_9159"};   // 0x68c604
}  // namespace

int time_attack_count(int character) { return kCount[character & 3]; }
const char* time_attack_name(int character, int row) { return kNames[character & 3][std::clamp(row, 0, 13)]; }
const char* time_attack_scene(int character, int row) {
    return ((character & 3) == 3 ? kScenesToal : kScenes)[std::clamp(row, 0, 13)];
}
const char* arena_name(int row) { return kArenaNames[std::clamp(row, 0, 6)]; }
const char* arena_scene(int row) { return kArenaScenes[std::clamp(row, 0, 6)]; }

std::string time_text(std::uint32_t ms) {
    if (ms == 0) return "-#>3-#>1:#>2-#>3-#>1:#>2-#>3-#>3-#>1";                    // 0x69a2bc
    const std::uint32_t t = ms / 1000, mmm = ms % 1000, min = t / 60, ss = t - min * 60;
    const std::uint32_t h = std::min(min / 60, 9999u), mm = min - (min / 60) * 60;
    char buf[40];
    if (h) std::snprintf(buf, sizeof buf, "%u:%02u:%02u:%03u", h, mm, ss, mmm);    // 0x69a2e4
    else std::snprintf(buf, sizeof buf, "%02u:%02u:%03u", mm, ss, mmm);            // 0x699380
    return buf;
}
}  // namespace stages

namespace {

DrawCmd text(const std::string& s, float x, float y, std::uint32_t colour) {     // FUN_00453120 / FUN_00453c10, edx = 2
    DrawCmd c;
    c.kind = DrawCmd::TextMiddle; c.text = s; c.x = x; c.y = y; c.diffuse = colour;
    c.anchor_x = 2; c.anchor_y = 0;
    return c;
}

constexpr int kColX[4] = {233, 380, 553, 681};                                    // 0x6a2668 (TA) = 0x6a2658 (Arena)
constexpr const char* kCols[4] = {"Easy", "Normal", "Hard", "Nightmare"};         // 0x68c440

}  // namespace

void StageSelect::tick_fade() {                                                   // [BYTES 0x576b50..0x576c36]
    for (int i = 0; i < 2; ++i) fade_[i] = target_[i] ? std::min(fade_[i] + 0.05f, 1.0f) : std::max(fade_[i] - 0.05f, 0.0f);
}

std::uint32_t StageSelect::title_tint() const {                                   // [EXPORT 00576c90.c:80-106]
    const float f = fade_[1] != 0.0f ? fade_[1] : fade_[0];
    const std::uint32_t g = 0xFF - std::uint32_t(std::clamp(f * 80.0f, 0.0f, 80.0f));
    return 0xFF000000u | g << 16 | g << 8 | g;
}

void StageSelect::update(const WndInput& in, const StageModel& m, StageEvents& ev) {
    if (!open_) return;
    const int count = m.rows();
    if (in.confirm_pressed || in.click_pressed) {                                 // pad+0x24 & 0x12000 [BYTES 0x57beb0..]
        ev.sounds.push_back(0);
        if (cursor_ >= count) cursor_ = -1;
        if (cursor_ == -1) { ev.back = true; target_[0] = target_[1] = false; }
        else ev.launch = cursor_;
        open_ = false;
        return;
    }
    if (in.cancel_pressed) {                                                      // & 0x4000: Back, no sound here
        ev.back = true; target_[0] = target_[1] = false; open_ = false;
        return;
    }
    if (in.mouse_mode) {                                                          // 0x7325d4 == 1 [BYTES 0x57c383..0x57c480]
        const int mx = mouse::pillar_inv(in.mx, in.display);                      // FUN_0057b3c0
        const float y0 = m.mode == 2 ? 176.0f : 144.0f;
        const float dy = float(in.my) - y0;
        if (mx - 96 >= 0 && mx - 96 < 640 && dy >= 0 && dy < float(count + 1) * 32.0f) cursor_ = int(dy * 0.03125f);
        else cursor_ = -1;
        if (cursor_ != hover_ && cursor_ != -1) ev.sounds.push_back(3);
        hover_ = cursor_;
    } else {                                                                      // [BYTES 0x57c485..0x57c4d0]
        cursor_ += in.vstep;
        if (cursor_ < 0) cursor_ = count;
        else if (cursor_ >= count + 1) cursor_ = 0;
        if (in.vstep != 0) ev.sounds.push_back(3);
    }
}

void StageSelect::draw(DrawList& out, const StageModel& m) const {
    if (fade_[0] != 0.0f) panel(out, m, false);                                   // [EXPORT 00576c90.c:264-269]
    if (fade_[1] != 0.0f) panel(out, m, true);
    if (open_ && cursor_ != -1) {                                                 // FUN_0057cee0
        static constexpr int kIcon[4] = {3, 4, 1, 2};                             // 0x74a964 / 68 / 5c / 60 = 0x74a958[id]
        const bool arena = m.mode == 2;
        DrawCmd c;
        c.kind = DrawCmd::Quad;
        c.tex = {TexRef::Icon, kIcon[m.character & 3], nullptr};
        c.x = arena ? 24.8f : 44.8f; c.y = float(40 + 20 * cursor_) * 1.6f + (arena ? 108.8f : 76.8f); c.w = 25.6f; c.h = 25.6f;
        c.flags = 0x24041; c.diffuse = 0xFFFFFFFF;
        c.anchor_x = 2; c.anchor_y = 0;
        out.push_back(c);
    }
}

void StageSelect::panel(DrawList& out, const StageModel& m, bool arena) const {
    const float f = fade_[arena];
    const std::uint32_t A = f >= 1.0f ? 0xFFu : std::uint32_t(f * 256.0f);
    const std::uint32_t white = A << 24 | 0xFFFFFF, dim = A << 24 | 0x586868, sel = A << 24 | 0xFFFF00;
    const std::uint32_t pc = 0xFF - ((2 * m.ms / 3) & 0xFF);                      // [BYTES 0x57d4fc..0x57d54b]
    const std::uint32_t pulse = A << 24 | pc << 16 | 0xFF00 | pc;
    const int cursor = open_ ? cursor_ : -1;
    const int count = arena ? m.arena_rows : stages::time_attack_count(m.character), cols = (m.nya ? 1 : 0) + 3;
    const records::RecordTable* table = arena ? m.ar : m.ta;
    const float base = arena ? 108.8f : 76.8f, name_x = arena ? 56.8f : 76.8f;

    DrawCmd bg;                                                                   // tarec_back: alpha 0 at the left edge
    bg.kind = DrawCmd::Quad;
    bg.tex = {TexRef::Named, 0, "TAREC_BACK.DDS"};
    bg.x = 0; bg.y = 0; bg.w = 1024; bg.h = 768;
    bg.u0 = 0.07f; bg.v0 = 0; bg.u1 = 1; bg.v1 = 1;
    bg.flags = 0x20041;
    bg.anchor_x = 0; bg.anchor_y = 0;                                             // FUN_005e4100 alone: the whole frame
    bg.diffuse = white; bg.hgrad = true; bg.diffuse_left = 0x00FFFFFF;
    out.push_back(bg);

    out.push_back(text("< RANKING >", 76.8f, 51.2f, white));                      // 0x6a19d4
    for (int i = 0; i < cols; ++i)
        out.push_back(text(kCols[i], 76.8f + float(kColX[i]), arena ? 140.8f : 108.8f, m.picking && i != m.column ? dim : white));
    for (int r = 0; r < count; ++r) {
        const float y = float(40 + 20 * r) * 1.6f + base;
        const std::uint32_t row = !m.picking ? white : r == cursor ? sel : white;
        out.push_back(text(arena ? stages::arena_name(r) : stages::time_attack_name(m.character, r), name_x, y, row));
        for (int d = 0; d < cols; ++d) {
            const std::uint32_t ms = table ? table->ms(m.character, d, r) : 0;
            const bool mark = table && table->mark(m.character, d, r) != 0;
            const std::string t = stages::time_text(ms);
            const float right = float(210 + 100 * d) * 1.6f + 76.8f;
            out.push_back(text(t, right - text_width_middle(t), y, mark ? pulse : m.picking && d != m.column ? dim : row));
        }
    }
    if (m.picking)                                                                // row index = count
        out.push_back(text("Cancel", name_x, float(20 * (count + 2)) * 1.6f + base, cursor == count ? sel : white));
}

}  // namespace cleria
