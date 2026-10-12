// The title screen: build/menu_re/TITLE_DRAW.md. See titlescreen.hpp.
#include "cleria/titlescreen.hpp"

#include <string>
#include <algorithm>

namespace cleria {

TitleAction TitleScreen::update(const WndInput& in, bool book_open, const TitleGates& gates, const RichMetrics& m,
                                std::vector<int>& sounds) {
    // §3.1: build a fresh list when no window and no book is up.
    if (!list_.alive() && !book_open) {
        std::vector<std::string> items;
        actions_.clear();
        auto add = [&](const char* t, TitleAction a) { items.push_back(t); actions_.push_back(a); };
        if (kind_ == 0) {
            add("New Game", TitleAction::NewGame);
            add("Load Game", TitleAction::LoadGame);
            if (gates.any_clear()) add("Bonus Modes", TitleAction::None);   // toggles the list
            if (gates.achievements) add("Achievements", TitleAction::Achievements);
            add("Options", TitleAction::Options);
            add("Exit", TitleAction::Exit);
        } else {
            if (gates.any_clear()) add("Time Attack", TitleAction::TimeAttack);
            if (gates.nya && gates.all_clear()) {
                add("Arena Mode", TitleAction::Arena);
                add("Bonus Shop", TitleAction::BonusShop);
            }
            add("Back", TitleAction::None);
        }
        // §3.3: origin (512, (b + 310) * 1.6), items from origin.y + 6.4 every 38.4.
        int b = kind_ == 0 ? (gates.any_clear() ? 8 : 30) : 0;
        if (!gates.achievements) b += 16;
        if (kind_ == 1) b += gates.all_clear() ? 32 : 48;
        const std::vector<std::string> labels(items.size());
        list_.menu(512, float(b + 310) * 1.6f, "<nocount>                 ", items, labels, m, false, false);
        list_.set_no_frame(true);
        list_.set_cursor(std::min(cursor_mem_ + 1, int(items.size())));
        list_.arm();
        fired_ = false;
    }
    list_.update(in, sounds);
    if (fired_ || !list_.accepted()) return TitleAction::None;
    fired_ = true;
    const int i = list_.chosen_index() - 1;                 // the item, 0-based
    if (i < 0 || i >= int(actions_.size())) return TitleAction::None;
    const TitleAction a = actions_[std::size_t(i)];
    switch (a) {
        case TitleAction::NewGame: cursor_mem_ = 0; break;
        case TitleAction::LoadGame: cursor_mem_ = 1; break;
        case TitleAction::Options: cursor_mem_ = gates.achievements ? 4 : 3; break;
        case TitleAction::TimeAttack: cursor_mem_ = 0; break;   // FUN_00575ca0 / FUN_00575dd0: 0x74ac3d = 0 / 1
        case TitleAction::Arena: cursor_mem_ = 1; break;
        case TitleAction::BonusShop: cursor_mem_ = 2; break;
        case TitleAction::None:                              // Bonus Modes / Back (0x575f30)
            kind_ ^= 1;
            cursor_mem_ = (kind_ ^ 1) * 2;
            break;
        default: break;
    }
    return a;
}

void TitleScreen::draw(DrawList& out, std::uint32_t tint) const {
    DrawCmd p;                                              // TitlePillar16-9 / 16-10 (menu tex 54 / 55)
    p.kind = DrawCmd::Pillars;
    p.tex = {TexRef::Menu, 54, nullptr};
    p.diffuse = tint;
    out.push_back(p);
    DrawCmd logo;                                           // title_000: opaque, the 4:3 field, rows 0..767
    logo.kind = DrawCmd::Quad;
    logo.tex = {TexRef::Named, 0, "TITLE_000.DDS"};
    logo.x = 0; logo.y = 0; logo.w = 1024; logo.h = 768;
    logo.u0 = 0; logo.v0 = 0; logo.u1 = 1; logo.v1 = 0.75f;
    logo.flags = 0x20001;
    logo.diffuse = tint;
    logo.anchor_x = 2; logo.anchor_y = 0;
    out.push_back(logo);
    list_.draw(out);
}

}  // namespace cleria
