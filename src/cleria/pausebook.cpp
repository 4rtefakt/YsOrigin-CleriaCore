// The in-game pause book: build/menu_re/PAUSEBOOK_DRAW.md. See pausebook.hpp.
#include "cleria/pausebook.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "cleria/cast.hpp"
#include "cleria/enemies.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/items.hpp"

namespace cleria {

namespace {

constexpr std::uint32_t kWhite = 0xFFFFFFFF, kDim = 0xFF808080, kDark = 0xFF303030;
constexpr std::uint32_t kStd = 0x24041, kOpaque = 0x20001;

// The tab table 0x703508 {x, y, tex, subId} (§2.1).
struct Tab { float x; int tex; int sub; };
constexpr Tab kTabs[9] = {
    {128, 1, 1}, {281, 2, 2}, {435, 3, 6}, {588, 6, 0x0b}, {742, 25, 0x12},
    {128, 4, 0}, {128, 3, 0}, {128, 6, 0}, {0, 0, 0}};

// The Equip neighbour graph (L, R, row-1, row+1), identical for every character (§3.1).
constexpr int kEquipNbr[22][4] = {
    {21, 19, 13, 1}, {6, 2, 0, 7}, {1, 3, 14, 8}, {2, 4, 15, 9}, {3, 5, 19, 10}, {4, 6, 20, 11},
    {5, 1, 21, 12}, {12, 8, 1, 13}, {7, 9, 2, 14}, {8, 10, 3, 15}, {9, 11, 4, 16}, {10, 12, 5, 17},
    {11, 7, 6, 18}, {18, 14, 7, 0}, {13, 15, 8, 2}, {14, 16, 9, 3}, {15, 17, 10, 19}, {16, 18, 11, 20},
    {17, 13, 12, 21}, {0, 20, 16, 4}, {19, 21, 17, 5}, {20, 0, 18, 6}};

constexpr float k48 = 4.7999992f;   // the float 0x40999998

void equip_cell(int i, int* col, int* row) {
    if (i == 0) { *col = 0; *row = 0; }
    else if (i <= 6) { *col = i - 1; *row = 1; }
    else if (i <= 12) { *col = i - 7; *row = 2; }
    else if (i <= 18) { *col = i - 13; *row = 3; }
    else { *col = i - 16; *row = 0; }
}
float equip_x0(int i) { int c, r; equip_cell(i, &c, &r); return 451.0f + 64.0f * float(c); }
float equip_y0(int i) { int c, r; equip_cell(i, &c, &r); return 86.4f + 76.8f * float(r); }
float item_x0(int i) { return 441.0f + 75.0f * float(i % 6); }
float item_y0(int i) { return 72.0f + 64.0f * float(i / 6); }

MenuSprite make(TexRef tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                std::uint32_t flags = kStd) {
    MenuSprite s;
    s.set_rect(x, y, w, h);
    s.set_uv(u0, v0, u1, v1);
    s.tex = tex;
    s.flags = flags;
    s.anchor_x = 2; s.anchor_y = 0;
    return s;
}
TexRef menu(int i) { return {TexRef::Menu, i}; }

// FUN_005a4760 at the arrow tip (rec.x - 16, rec.y - 16) [EXPORT; MOUSE.md §5.3].
bool hit(const MenuSprite& s, const BookInput& in) {
    const mouse::Point t = mouse::tip(in.mx, in.my);
    return mouse::csprite_hit(s.x, s.y, s.w, s.h, s.anchor_x, t.x, t.y, in.display);
}

// The page captions (EN 0x68eaf4) and the Equip per-cell captions (EN 0x68ea78).
const char* page_caption(int page) {
    switch (page) {
        case 0: return "View or change your equipment.";
        case 1: return "Examine or use an item.";
        case 2: case 6: return "Load a previously saved gameplay session.";
        case 3: case 7: return "Alter the game's settings.";
        case 4: return "Browse monster, character or tutorial data.";
        case 5: return "Save your gameplay session.";
        default: return "";
    }
}
const char* equip_caption(int cell) {
    if (cell == 0) return "Your equipped weapon.";
    if (cell <= 6) return "Equip body armor.";
    if (cell <= 12) return "Equip footwear.";
    if (cell <= 18) return "Equip accessory. Re-select equipped accessory to unequip.";
    return "Select a skill.";
}

// The weapon tier -> its level label (§3.3 step 4).
std::string weapon_label(int tier) {
    if (tier >= 10) return "Lv Max";
    if (tier >= 8) return "Lv 6";
    if (tier >= 6) return "Lv 5";
    if (tier >= 4) return "Lv 4";
    if (tier >= 2) return "Lv 3";
    if (tier >= 1) return "Lv 2";
    return "Lv 1";
}
std::string level_label(int v, int max) {
    if (v == max) return "Max";
    return "Lv" + std::to_string(v + 1);
}

}  // namespace

// Builder D's (the HUD's) spelling of the weapon's level: "Lv1".."Lv6", "Max" [BYTES 0x43ff9f..0x43fff7, the
// strings 0x6994f4..0x6994c4] — the book's thresholds, without its space and its "Lv " before Max.
std::string hud_weapon_label(int tier) {
    const std::string s = weapon_label(tier);
    return s == "Lv Max" ? "Max" : "Lv" + s.substr(3);
}
// ... and of an armor's / a footwear's: "Max" when its cell of 0x76a654 is 1, else "Lv%d" of the cell + 1
// [BYTES 0x440059..0x440090] — the book's level_label(v, 1).
std::string hud_gear_label(int strengthened) { return level_label(strengthened, 1); }

namespace {

bool usable(int id) {   // the Use list L (§6.4)
    switch (id) {
        case 0x54: case 0x55: case 0x56: case 0x59: case 0x70: case 0x5F: case 0x60: case 0x69: return true;
        default: return false;
    }
}

DrawCmd text_cmd(DrawCmd::Kind k, const std::string& s, float x, float y, std::uint32_t colour, float extra = 0) {
    DrawCmd c;
    c.kind = k; c.text = s; c.x = x; c.y = y; c.diffuse = colour; c.line_extra = extra;
    c.anchor_x = 2; c.anchor_y = 0;
    return c;
}

}  // namespace

// ---- the shell (FUN_005a6230 / FUN_005a68f0) ----------------------------------------------

void PauseBook::build_shell() {
    page_ = last_page_;
    nav_ = 0;
    back_ = make(menu(0), 0, 0, 1024, 768, 0, 0, 1, 0.75f, kOpaque);
    for (int i = 0; i < 9; ++i) {
        plaque_[i] = make(menu(i == 8 ? 0 : kTabs[i].tex), kTabs[i].x, -51.2f, 153.6f, 51.2f, 0, 0, 0.6f, 0.4f);
        plaque_[i].set_colour(kWhite, 0);
    }
    cursor_.pulse_specular_off();
    cursor_ = make(menu(8), kTabs[page_].x, -51.2f, 153.6f, 51.2f, 0, 0, 0.6f, 0.4f);
    cursor_.set_colour(kWhite, 0);
    gold_.set_uv(0, 0.4f, 0.6f, 0.8f);
    gold_.flags = kStd; gold_.anchor_x = 2; gold_.anchor_y = 0;
    gold_.tex = {};
    gold_.set_colour(0, 0);
}

void PauseBook::update_nav(const BookInput& in, BookEvents& ev) {
    bool busy = tween(gold_);
    busy |= tween(cursor_);
    for (MenuSprite& p : plaque_) busy |= tween(p);
    const BookInput none{};
    const BookInput& I = sub_ != 0 ? none : in;              // the navigator zeroes its copy while a page has focus
    switch (nav_) {
        case 0:
            if (sub_ == 4 || sub_ == 8 || sub_ == 0x11 || sub_ == 0x17) {
                // TITLE_DRAW.md §4.2: a direct entry slides its own plaque in; Load / Option
                // also slide the gold plaque in on top. The Cur00 cursor stays off screen.
                const int p = sub_ == 4 ? 5 : sub_ == 8 ? 6 : sub_ == 0x11 ? 7 : 8;
                page_ = p;
                plaque_[p].set_target(kTabs[p].x, 0, 16);
                if (sub_ == 8 || sub_ == 0x11) {
                    gold_.set_rect(128, -51.2f, 153.6f, 51.2f);
                    gold_.set_target(128, 0, 16);
                    gold_.tex = menu(sub_ == 8 ? 3 : 6);
                    gold_.set_colour(kWhite, 0);
                }
                nav_ = 1;
                break;
            }
            if (sub_ == 0) {
                cursor_.set_target(kTabs[page_].x, 0, 16);
                for (int i = 0; i < 5; ++i) plaque_[i].set_target(kTabs[i].x, 0, 16);
                nav_ = 1;
            }
            break;
        case 1:
            if (in.cancel && sub_ == 0) { sub_ = -1; ev.sounds.push_back(1); break; }
            if (!busy) {
                if (sub_ != 0) nav_ = 4;
                else { nav_ = 2; cursor_.pulse_specular_on(8); }
            }
            break;
        case 2: {
            if (I.cancel) { sub_ = -1; ev.sounds.push_back(1); break; }
            int p = page_;
            if (I.hstep != 0) {
                p = (I.hstep + 9 + page_) % 9;
                while (p >= 5) p = (I.hstep + 9 + p) % 9;
            }
            // Hover on motion: the loop scans the 9 plaques, only 0..4 take [EXPORT FUN_005a68f0].
            if (I.moved)
                for (int i = 0; i < 5; ++i)
                    if (hit(plaque_[i], I)) { p = i; break; }
            if (p != page_) {
                page_ = p;
                ev.sounds.push_back(3);
                cursor_.set_rect(kTabs[page_].x, 0, 153.6f, 51.2f);
            }
            // One click inside the selected plaque selects and opens [BYTES 0x5a7060..0x5a7075].
            if (I.confirm || (I.click && hit(plaque_[page_], I))) {
                nav_ = 3;
                ev.sounds.push_back(0);
                const float sp = 16.0f * float(page_) + 16.0f;
                gold_.set_rect(kTabs[page_].x, 0, 153.6f, 51.2f);
                gold_.tex = menu(kTabs[page_].tex);
                gold_.fade_to(kWhite, 0x808080, 24);
                gold_.set_target(128, 0, sp);
                cursor_.pulse_specular_off();
                cursor_.fade_to(0x00FFFFFF, 0, 24);
                cursor_.set_target(128, 0, sp);
                for (int i = 0; i < 5; ++i) plaque_[i].set_target(128, 0, 16.0f * float(i + 1));
                last_page_ = page_;
                sub_ = kTabs[page_].sub;
            }
            break;
        }
        case 3:
            if (!busy) { nav_ = 4; gold_.fade_to(gold_.diffuse_t, 0, 24); }
            break;
        case 4:
            if (sub_ == 0) nav_ = 5;
            break;
        case 5:
            gold_.fade_to(0x00FFFFFF, 0, 32);
            nav_ = 6;
            break;
        case 6:
            if (!busy) build_shell();
            break;
        default: break;
    }
}

// ---- Equip (FUN_005a7580 / FUN_005a8ef0 / FUN_005a8030 / FUN_005a9140) ---------------------

void PauseBook::build_equip(const BookModel& m) {
    const auto& grid = equip_grid(m.character);
    epanel_ = make(menu(9), 435, 64, 448, 352, 0, 0, 0.875f, 0.6875f);
    ecursor_.pulse_specular_off();
    ecursor_ = make(menu(10), equip_x0(ecur_), equip_y0(ecur_), 64, 64, 0, 0.4f, 0.125f, 0.65f);
    ecursor_.set_colour(kWhite, 0);
    ecursor_.pulse_specular_on(8);
    for (int i = 0; i < 22; ++i) {
        const int id = grid[std::size_t(i)];
        erec_count_[std::size_t(i)] = m.flags[id];
        const float x0 = equip_x0(i), y0 = equip_y0(i);
        etile_[i] = make(menu(10), x0 + k48, y0 + k48, 54.4f, 54.4f, 0, 0, 0.10625f, 0.2125f);
        eicon_[i] = make({TexRef::Icon, id}, x0 + 6.4f, y0 + 6.4f, 51.2f, 51.2f, 0, 0, 1, 1);
    }
    const std::int32_t slots[5] = {m.flags[151], m.flags[152], m.flags[153], m.inv->accessory, m.inv->magic};
    for (int k = 0; k < 5; ++k) {
        emarker_[k] = MenuSprite{};
        emarker_[k].set_rect(0, 0, 0, 0);
        emarker_[k].set_colour(0, 0);
        for (int i = 0; i < 22; ++i)
            if (grid[std::size_t(i)] == slots[k])
                emarker_[k] = make(menu(10), equip_x0(i) + k48, equip_y0(i) + k48, 54.4f, 54.4f,
                                   0.10625f, 0, 0.2125f, 0.2125f);
    }
    estate_ = 0;
    equip_colours(false, false);
}

void PauseBook::equip_colours(bool focused, bool animate) {
    const std::uint32_t C = focused ? kWhite : kDim;
    auto col = [&](MenuSprite& s) { if (animate) s.fade_to(C, 0, 16); else s.set_colour(C, 0); };
    col(epanel_);
    for (MenuSprite& t : etile_) col(t);
    for (MenuSprite& mk : emarker_) col(mk);
    for (int i = 0; i < 22; ++i) {
        const std::int32_t n = erec_count_[std::size_t(i)];
        if (n < 0) eicon_[i].set_colour(0, 0);
        else if (n == 0) eicon_[i].set_colour(kDark, 0);
        else col(eicon_[i]);
    }
}

void PauseBook::update_equip(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 1 ? in0 : none;
    bool busy = tween(epanel_) | tween(ecursor_);
    for (MenuSprite& t : etile_) busy |= tween(t);
    for (MenuSprite& i : eicon_) busy |= tween(i);
    for (MenuSprite& mk : emarker_) busy |= tween(mk);
    const auto& grid = equip_grid(m.character);
    switch (estate_) {
        case 0:
            if (sub_ == 1) { estate_ = 1; equip_colours(true, true); }
            break;
        case 1: {
            if (in.cancel) { sub_ = 0; ev.sounds.push_back(1); build_equip(m); break; }
            int cur = ecur_;
            if (in.hstep < 0) cur = kEquipNbr[cur][0];
            else if (in.hstep > 0) cur = kEquipNbr[cur][1];
            if (in.vstep < 0) cur = kEquipNbr[cur][2];
            else if (in.vstep > 0) cur = kEquipNbr[cur][3];
            if (in.moved)                                    // the 22 hit tiles 0x733730 [EXPORT FUN_005a8030]
                for (int i = 0; i < 22; ++i)
                    if (hit(etile_[i], in)) { cur = i; break; }
            if (cur != ecur_) {
                ecur_ = cur;
                ev.sounds.push_back(3);
                ecursor_.set_rect(equip_x0(cur), equip_y0(cur), 64, 64);
            }
            if (in.confirm || (in.click && hit(etile_[ecur_], in))) {
                const int id = grid[std::size_t(ecur_)];
                if (erec_count_[std::size_t(ecur_)] <= 0) {
                    ev.sounds.push_back(2);
                } else if (id == m.inv->accessory) {                 // re-select = unequip
                    m.inv->accessory = 0xFF;
                    emarker_[3].set_rect(0, 0, 0, 0);
                    emarker_[3].set_colour(0, 0);
                    ev.sounds.push_back(2);
                } else {
                    estate_ = 2;
                    ev.sounds.push_back(0);
                    for (MenuSprite* s : {&ecursor_, &etile_[ecur_], &eicon_[ecur_]}) s->fade_to(kWhite, 0x808080, 32);
                }
            }
            break;
        }
        case 2:
            if (!busy) {
                estate_ = 3;
                for (MenuSprite* s : {&ecursor_, &etile_[ecur_], &eicon_[ecur_]}) s->fade_to(kWhite, 0, 24);
            }
            break;
        case 3: {
            const int id = grid[std::size_t(ecur_)];
            std::int32_t* slot = nullptr;
            int k = 0;
            if (ecur_ == 0) { slot = &m.flags[151]; k = 0; }
            else if (ecur_ <= 6) { slot = &m.flags[152]; k = 1; }
            else if (ecur_ <= 12) { slot = &m.flags[153]; k = 2; }
            else if (ecur_ <= 18) { slot = &m.inv->accessory; k = 3; }
            else { slot = &m.inv->magic; k = 4; }
            if (*slot != id) {
                *slot = id;
                ev.sounds.push_back(0x0F);
                ev.clamp_hp = true;
            }
            emarker_[k] = make(menu(10), equip_x0(ecur_) + k48, equip_y0(ecur_) + k48, 54.4f, 54.4f,
                               0.10625f, 0, 0.2125f, 0.2125f);
            if (!busy && sub_ == 1) estate_ = 1;
            break;
        }
        default: break;
    }
}

void PauseBook::draw_equip(DrawList& out, const BookModel& m) const {
    const auto& grid = equip_grid(m.character);
    cleria::draw(out, epanel_);
    for (int i = 0; i < 22; ++i) {
        MenuSprite ic = eicon_[i];
        if (grid[std::size_t(i)] == 0 && m.flags[4] > 0) ic.tex = {TexRef::Icon, 4};
        cleria::draw(out, ic);
    }
    const std::int32_t slots[5] = {m.flags[151], m.flags[152], m.flags[153], m.inv->accessory, m.inv->magic};
    for (int k = 0; k < 5; ++k) {
        const bool set = k < 3 ? slots[k] != -1 : (slots[k] != 0xFF && slots[k] != -1);
        if (set) cleria::draw(out, emarker_[k]);
    }
    // The level labels (F_MINI, white, at the cell's bottom-right).
    auto label = [&](int i, const std::string& s) {
        const float w = text_width_mini(s);
        out.push_back(text_cmd(DrawCmd::TextMini, s, eicon_[i].x + 51.2f - w - 1.0f, eicon_[i].y + 54.4f - 14.0f, kWhite));
    };
    label(0, weapon_label(m.inv->tier[std::clamp(m.character, 0, 3)]));
    for (int k = 0; k < 3; ++k)
        if (erec_count_[std::size_t(19 + k)] > 0) label(19 + k, level_label(m.flags[182 + k], 3));
    for (int i = 1; i <= 12; ++i)
        if (erec_count_[std::size_t(i)] > 0) {
            const int id = grid[std::size_t(i)];
            label(i, level_label(id >= 0 && id < 54 ? m.inv->raval[id] : 0, 1));
        }
    if (estate_ != 0) cleria::draw(out, ecursor_);
}

// ---- Item (FUN_005a9d10 / FUN_005aac10 / FUN_005aa370 / FUN_005aad90) ----------------------

void PauseBook::build_item(const BookModel& m) {
    const auto& grid = item_grid(m.character);
    ipanel_ = make(menu(12), 403, 64, 518.4f, 409.6f, 0, 0, 1.0125f, 0.8f);
    icursor_.pulse_specular_off();
    icursor_ = make(menu(10), item_x0(icur_), item_y0(icur_), 64, 64, 0, 0.4f, 0.125f, 0.65f);
    icursor_.set_colour(kWhite, 0);
    icursor_.pulse_specular_on(8);
    ihi_ = make(menu(10), 0, 0, 0, 0, 0.10625f, 0, 0.2125f, 0.2125f);
    for (int i = 0; i < 30; ++i) {
        int id = grid[std::size_t(i)];
        irec_count_[std::size_t(i)] = id >= 0 ? m.flags[id] : -1;
        const float x0 = item_x0(i), y0 = item_y0(i);
        itile_[i] = make(menu(10), x0 + k48, y0 + k48, 54.4f, 54.4f, 0, 0, 0.10625f, 0.2125f);
        int icon = id;
        if (id == 0x5E && m.flags[93] > 0) icon = 0x5D;
        if (id == 0x69 && m.flags[104] > 0) icon = 0x68;
        iicon_[i] = make({TexRef::Icon, icon}, x0 + 6.4f, y0 + 6.4f, 51.2f, 51.2f, 0, 0, 1, 1);
        if (id >= 0 && id == m.inv->held) ihi_.set_rect(x0 + k48, y0 + k48, 54.4f, 54.4f);
    }
    istate_ = 0;
    item_colours(false, false);
}

void PauseBook::item_colours(bool bright, bool animate) {
    const std::uint32_t C = bright ? kWhite : kDim;
    auto col = [&](MenuSprite& s) { if (animate) s.fade_to(C, 0, 16); else s.set_colour(C, 0); };
    col(ipanel_);
    col(ihi_);
    for (MenuSprite& t : itile_) t.set_colour(bright ? 0xFFE0E0E0 : kDark, 0);
    for (int i = 0; i < 30; ++i) {
        if (irec_count_[std::size_t(i)] <= 0) iicon_[i].set_colour(kDark, 0);
        else col(iicon_[i]);
    }
}

void PauseBook::update_item(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 2 ? in0 : none;
    bool busy = tween(ipanel_) | tween(icursor_) | tween(ihi_);
    for (MenuSprite& t : itile_) busy |= tween(t);
    for (MenuSprite& i : iicon_) busy |= tween(i);
    m.flags[228] = icur_;
    const auto& grid = item_grid(m.character);
    switch (istate_) {
        case 0:
            if (sub_ == 2) { item_colours(true, true); istate_ = 1; }
            break;
        case 1: {
            if (in.cancel) { sub_ = 0; ev.sounds.push_back(1); build_item(m); break; }
            int col = icur_ % 6, row = icur_ / 6;
            if (in.hstep) col = (col + in.hstep + 6) % 6;
            if (in.vstep) row = (row + in.vstep + 5) % 5;
            int cur = row * 6 + col;
            if (in.moved)                                    // the 30 tiles 0x735f90 [EXPORT FUN_005aa370]
                for (int i = 0; i < 30; ++i)
                    if (hit(itile_[i], in)) { cur = i; break; }
            if (cur != icur_) {
                icur_ = cur;
                ev.sounds.push_back(3);
                icursor_.set_rect(item_x0(cur), item_y0(cur), 64, 64);
            }
            if (in.confirm || (in.click && hit(itile_[icur_], in))) {
                const int id = grid[std::size_t(icur_)];
                const std::int32_t n = irec_count_[std::size_t(icur_)];
                if (id < 0 || n <= 0) { ev.sounds.push_back(2); break; }
                const bool in_l = usable(id);
                if (id != m.inv->held) {
                    m.inv->held = id;
                    ihi_.set_rect(item_x0(icur_) + k48, item_y0(icur_) + k48, 54.4f, 54.4f);
                    istate_ = 2;
                    for (MenuSprite* s : {&icursor_, &itile_[icur_], &iicon_[icur_]}) s->fade_to(s->diffuse_t, 0x808080, 32);
                    if (!in_l) { ev.sounds.push_back(0); break; }
                }
                if (in_l) {
                    istate_ = 2;
                    ev.sounds.push_back(0);
                    icursor_.set_uv(0.125f, 0.4f, 0.25f, 0.65f);
                    icursor_.pulse_specular_off();
                    for (MenuSprite* s : {&icursor_, &itile_[icur_], &iicon_[icur_]}) s->fade_to(s->diffuse_t, 0x404040, 64);
                    sub_ = 3;
                    pitem_ = id;
                } else {
                    ev.sounds.push_back(2);
                }
            }
            break;
        }
        case 2:
            if (!busy) {
                istate_ = 3;
                for (MenuSprite* s : {&icursor_, &itile_[icur_], &iicon_[icur_]}) s->fade_to(s->diffuse_t, 0, 64);
            }
            break;
        case 3:
            if (sub_ == 2 && !busy) { build_item(m); item_colours(true, false); istate_ = 1; }
            break;
        default: break;
    }
}

// ---- the Use popup (FUN_005ab0d0 / FUN_005abd10) ----------------------------------------------

void PauseBook::build_popup() {
    pframe_ = make(menu(13), 0, 0, 204.8f, 102.4f, 0, 0, 0.8f, 0.4f);
    pyes_ = make(menu(13), 0, 0, 88, 26, 0, 0.625f, 0.34375f, 0.7265625f);
    pno_ = make(menu(13), 0, 0, 88, 26, 0, 0.75f, 0.34375f, 0.8515625f);
    pcur_.pulse_specular_off();
    pcur_ = make(menu(13), 0, 0, 93, 33, 0, 0.4625f, 0.36328125f, 0.59140623f);
    for (MenuSprite* s : {&pframe_, &pyes_, &pno_, &pcur_}) s->set_colour(0, 0);
    pstate_ = 0;
}

void PauseBook::update_popup(const BookInput& in, BookModel& m, BookEvents& ev) {
    if (close_next_) { close_next_ = false; sub_ = -1; return; }    // 0x739160
    bool busy = tween(pframe_) | tween(pyes_) | tween(pno_) | tween(pcur_);
    static constexpr float kT[2][2] = {{96, 32}, {96, 67}};
    auto place_cursor = [&] { pcur_.set_rect(pbx_ - 2 + kT[psel_][0], pby_ - 3 + kT[psel_][1], 93, 33); };
    switch (pstate_) {
        case 0:
            if (sub_ == 3) {
                pbx_ = float(int(item_x0(icur_) - 18.0f));
                pby_ = float(int(item_y0(icur_) - 28.8f));
                pframe_.set_rect(pbx_, pby_, 204.8f, 102.4f);
                pyes_.set_rect(pbx_ + 96, pby_ + 32, 88, 26);
                pno_.set_rect(pbx_ + 96, pby_ + 67, 88, 26);
                psel_ = 0;
                place_cursor();
                for (MenuSprite* s : {&pframe_, &pyes_, &pno_, &pcur_}) s->fade_to(kWhite, 0, 64);
                pcur_.pulse_specular_on(8);
                pstate_ = 1;
            }
            break;
        case 1: {
            if (in.cancel) {
                sub_ = 2; ev.sounds.push_back(1);
                pcur_.pulse_specular_off();
                for (MenuSprite* s : {&pframe_, &pyes_, &pno_, &pcur_}) s->set_colour(0, 0);
                pstate_ = 0;
                break;
            }
            int sel = psel_;
            if (in.vstep != 0) sel ^= 1;
            if (in.moved) {                                  // Yes / No [EXPORT FUN_005abd10 st 1]
                if (hit(pyes_, in)) sel = 0;
                else if (hit(pno_, in)) sel = 1;
            }
            if (sel != psel_) { psel_ = sel; ev.sounds.push_back(3); place_cursor(); }
            if (in.confirm || (in.click && hit(psel_ == 0 ? pyes_ : pno_, in))) {
                pstate_ = 4;
                pcur_.pulse_specular_off();
                (psel_ == 0 ? pyes_ : pno_).fade_to(kWhite, 0x808080, 64);
                ev.sounds.push_back(0);
                if (psel_ == 1) { sub_ = 2; break; }
                const bool can = m.player_can_act;
                switch (pitem_) {
                    case 0x54: case 0x56:                    // TOPIC_MAP.md §6.1: the warp map
                        if (m.start_script) m.start_script("@CheckWing");
                        else ev.script = "@CheckWing";
                        if (m.flags[198] > 0) {                 // warping is sealed here
                            ev.sounds.push_back(2);
                            if (m.start_script) m.start_script("@CheckWing2");
                            close_next_ = true;
                        }
                        pstate_ = 3;
                        mtower_.fade_to(kWhite, 0, 24);        // the pillars follow the tower
                        return;
                    case 0x55:                               // flag 503 = the O2 gauge is up ([not modelled]: 0)
                        m.flags[503] = 0;
                        ev.script = can ? "@Riranokaigara" : "@UseItemBoo";
                        break;
                    case 0x70: ev.script = can ? "@DarkRiranokaigara" : "@UseItemBoo"; break;
                    case 0x5F: ev.script = !can ? "@UseItemBoo" : m.underwater ? "@UseHarmonica2" : "@UseHarmonica"; break;
                    case 0x60: ev.script = can ? "@UseHammer" : "@UseItemBoo"; break;
                    case 0x69: ev.script = can ? "@UseSekizou" : "@UseItemBoo"; break;
                    case 0x59: ev.use_item = 0x59; ev.sounds.push_back(0x277); break;
                    default: break;
                }
                sub_ = -1;
            }
            break;
        }
        case 3:
            update_map(in, m, ev);
            break;
        case 4:
            if (!busy) { pstate_ = 5; pyes_.fade_to(pyes_.diffuse_t, 0, 64); pno_.fade_to(pno_.diffuse_t, 0, 64); }
            break;
        case 5:
            if (!busy) { for (MenuSprite* s : {&pframe_, &pyes_, &pno_, &pcur_}) s->fade_to(0, 0, 64); pstate_ = 0; }
            break;
        default: break;
    }
}

void PauseBook::draw_item(DrawList& out, const BookModel& m) const {
    const auto& grid = item_grid(m.character);
    cleria::draw(out, ipanel_);
    for (int i = 0; i < 30; ++i) {
        if (grid[std::size_t(i)] < 0) cleria::draw(out, itile_[i]);
        else if (irec_count_[std::size_t(i)] >= 0) cleria::draw(out, iicon_[i]);
    }
    cleria::draw(out, ihi_);
    if (istate_ != 0) cleria::draw(out, icursor_);
    for (int i = 0; i < 30; ++i) {
        const int id = grid[std::size_t(i)];
        const std::int32_t n = irec_count_[std::size_t(i)];
        if (n < 0 || (id != 0x57 && id != 0x58 && id != 0x59)) continue;
        const std::string s = std::to_string(n);
        out.push_back(text_cmd(DrawCmd::TextMini, s, itile_[i].x + 54.4f - text_width_mini(s) - 1.0f,
                               itile_[i].y + 43.4f, n == 0 ? 0xFFFF0000 : kWhite));
    }
}

// ---- status panel (FUN_005b05c0 / FUN_005b1680 / FUN_005b0860) -----------------------------

void PauseBook::draw_status(DrawList& out, const BookModel& m) const {
    constexpr float X = 182, Y = 60, R = X + 195;
    MenuSprite plate = make(menu(11), X, Y, 217.6f, 358.4f, 0, 0, 0.425f, 0.7f);
    plate.set_colour(kWhite, 0);
    cleria::draw(out, plate);
    char buf[64];
    auto right = [&](const std::string& s, float x_right, float y) {
        const float w = text_width_middle(s);
        out.push_back(text_cmd(DrawCmd::TextMiddle, s, x_right - w, y, kWhite));
        return x_right - w;
    };
    // The Equip preview: the candidate item swapped into its slot (only while Equip has focus).
    PlayerStats pv{};
    bool preview = false;
    if (sub_ == 1 && m.preview) {
        const int id = equip_grid(m.character)[std::size_t(ecur_)];
        const std::int32_t n = erec_count_[std::size_t(ecur_)];
        if ((id == 0 || (id >= 6 && id <= 53)) ? n > 0 : (id >= 54 && id <= 65 && n != 0)) {
            pv = m.preview(id); preview = true;
        }
    }
    auto arrow = [&](float y, float now, float then) {
        if (!preview || std::floor(then) == std::floor(now)) return;
        const bool up = then > now;
        MenuSprite a = make(menu(11), X + 92, y, 12.8f, 25.6f, up ? 0.55f : 0.575f, 0.15f, up ? 0.575f : 0.6f, 0.2f);
        a.set_colour(kWhite, 0);
        cleria::draw(out, a);
        out.push_back(text_cmd(DrawCmd::TextMiddle, std::to_string(int(std::floor(then))), X + 104, y,
                               up ? 0xFFFF0000 : 0xFF0000FF));
    };
    std::snprintf(buf, sizeof buf, "Lv.%2d", m.level);
    right(buf, R, Y + 12);
    std::snprintf(buf, sizeof buf, "/%3d", int(m.max_hp));
    const float hx = right(buf, R, Y + 51);
    std::snprintf(buf, sizeof buf, "HP %3d", int(m.hp));
    right(buf, hx, Y + 51);
    std::snprintf(buf, sizeof buf, "STR %7d", int(std::floor(m.atk)));
    right(buf, R, Y + 89);
    arrow(Y + 89, m.atk, pv.atk);
    std::snprintf(buf, sizeof buf, "DEF %7d", int(std::floor(m.def)));
    right(buf, R, Y + 128);
    arrow(Y + 128, m.def, pv.def);
    std::snprintf(buf, sizeof buf, "Exp %7d", int(std::floor(m.exp)));
    right(buf, R, Y + 166);
    if (m.level >= 60) std::snprintf(buf, sizeof buf, "Next -------");
    else std::snprintf(buf, sizeof buf, "Next %7d", int(std::floor(m.next_exp)) - int(std::floor(m.exp)));
    right(buf, R, Y + 204);
    // The difficulty word (bottom right).
    const char* diff = m.flags[201] ? "Inferno" : m.flags[203] ? "Very Easy" : m.flags[143] ? "Easy"
                     : m.flags[144] == 1 ? "Hard" : m.flags[144] == 2 ? "Nightmare" : "Normal";
    out.push_back(text_cmd(DrawCmd::TextMiddle, diff, 1024.0f - text_width_middle(diff) - 6.4f, 710.4f, kWhite));
}

// ---- caption + item-detail panel (FUN_005ae970 / FUN_005afb70 / FUN_005b0130) -------------

void PauseBook::set_detail(int id) {
    if (id >= 0 && id != 0xFF) { detail_id_ = id; dicon_.fade_to(kWhite, 0, 16); }
    else dicon_.fade_to(0x00FFFFFF, 0, 16);
}

void PauseBook::update_caption(const BookModel& m) {
    switch (sub_) {
        case 0: caption_ = page_caption(page_); set_detail(-1); break;
        case 1: {
            caption_ = equip_caption(ecur_);
            const int id = equip_grid(m.character)[std::size_t(ecur_)];
            set_detail(erec_count_[std::size_t(ecur_)] >= 0 ? id : -1);
            break;
        }
        case 2: case 3: {
            caption_ = page_caption(1);
            const int id = item_grid(m.character)[std::size_t(icur_)];
            set_detail(id >= 0 && irec_count_[std::size_t(icur_)] >= 0 ? id : -1);
            break;
        }
        case 10: caption_ = "Delete this data?"; set_detail(-1); break;
        case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: caption_.clear(); set_detail(-1); break;
        default: caption_ = page_caption(page_); set_detail(-1); break;
    }
    tween(dicon_);
}

void PauseBook::draw_caption_detail(DrawList& out, const BookModel& m) const {
    out.push_back(text_cmd(DrawCmd::TextGdi, caption_, 25.6f, 707.2f, kWhite));
    if ((dicon_.diffuse >> 24) == 0 || detail_id_ < 0) return;
    int id = detail_id_;
    if (id == 0 && m.flags[4] > 0) id = 4;
    if (id == 0x5E) id = 0x5D;
    if (id == 0x69) id = 0x68;
    MenuSprite plate = make(menu(31), 64, 448, 928, 256, 0, 0, 0.90625f, 1);
    plate.set_colour(kWhite, 0);
    cleria::draw(out, plate);
    const std::uint32_t tc = dicon_.diffuse;
    // The name: INVINFO, except the bracelets, which take the character's skill name.
    std::string name = m.items ? m.items->name(id) : std::string();
    static const char* kSkill[3][4] = {{"Ventus Bracelet", "Whirlwind", "Force Shield", "Godspeed"},
                                       {"Terra Bracelet", "Bolt Crash", "Trap Mine", "Thunder Claw"},
                                       {"Ignis Bracelet", "Phoenix Flame", "Fire Wheel", "Inferno"}};
    if (id >= 0x74 && id <= 0x76) name = kSkill[id - 0x74][std::clamp(m.character, 0, 3)];
    out.push_back(text_cmd(DrawCmd::TextGdi, name, 377.6f, 483.2f, tc));
    std::string stat;
    if (id <= 5) {
        const int tier = m.flags[148];
        if (tier != 0 && m.gi)
            stat = "STR +" + std::to_string(int(std::floor(m.gi->weapon_atk[std::clamp(m.character, 0, 3)][std::clamp(tier, 0, 10)])));
    } else if (id <= 53) {
        const int v = m.inv->raval[id];
        if (v != 0) stat = "Add. DEF +" + std::to_string(v);
    }
    if (!stat.empty()) out.push_back(text_cmd(DrawCmd::TextGdi, stat, 678.4f, 483.2f, tc));
    if (m.items)
        if (const Item* it = m.items->get(id))
            out.push_back(text_cmd(DrawCmd::TextGdi, it->description, 396.8f, 534.4f, tc, 4.0f));
    MenuSprite big = make({TexRef::Icon2, id}, 134.4f, 460.8f, 204.8f, 204.8f, 0, 0, 0.75f, 0.75f);
    big.set_colour(tc, 0);
    cleria::draw(out, big);
    if (id <= 5) out.push_back(text_cmd(DrawCmd::TextMiddle, weapon_label(m.inv->tier[std::clamp(m.character, 0, 3)]), 142.4f, 468.8f, tc));
    else if (id >= 0x74 && id <= 0x76)
        out.push_back(text_cmd(DrawCmd::TextMiddle, level_label(m.flags[182 + id - 0x74], 3), 142.4f, 468.8f, tc));
}

// ---- the book (FUN_005a4fd0 / FUN_005a5130 / FUN_005a5d50) --------------------------------

void PauseBook::open(const BookModel& m, int ctx) {
    open_ = true;
    static constexpr int kSub[5] = {0, 4, 8, 0x11, 0x17};
    sub_ = kSub[std::clamp(ctx, 0, 4)];
    frames_ = 0;
    icur_ = std::clamp(m.flags[228], 0, 29);
    build_shell();
    if (ctx >= 1) page_ = ctx == 1 ? 5 : ctx == 2 ? 6 : ctx == 3 ? 7 : 8;   // the direct pages (§4.2)
    build_item(m);
    build_popup();
    build_equip(m);
    build_svld(m, true);
    for (int k = 0; k < 3; ++k) build_dialog(k);
    dlg_[2].ret = sub_;                                      // FUN_005b6010(subId)
    build_option(m);
    build_map();
    build_category();
    build_bar();
    build_sheet(m);
    build_viewer();
    build_manual(m);
    build_charsel(m);
    dplate_ = MenuSprite{};
    dicon_ = MenuSprite{};
    dicon_.set_colour(0x00FFFFFF, 0);
    detail_id_ = -1;
    caption_.clear();
}

void PauseBook::update(const BookInput& in0, BookModel& m, BookEvents& ev) {
    if (!open_) return;
    BookInput in = frames_ < 2 ? BookInput{} : in0;          // the dispatcher zeroes the first 2 frames
    ++frames_;
    // The wheel debounce 0x7d9be0: a wheel tick, then the next 3 ticks' wheels are dropped [BYTES
    // 0x5a524f..0x5a5291]. [not modelled] the whole record zeroed while the page-transition
    // highlight tweens (0x74418c == 0): CleriaCore has no transition highlight.
    if (wheel_db_ > 0) { in.wheel = 0; --wheel_db_; }
    else if (in.wheel != 0) wheel_db_ = 3;
    update_nav(in, ev);
    update_equip(in, m, ev);
    update_item(in, m, ev);
    update_popup(sub_ == 3 ? in : BookInput{}, m, ev);
    update_caption(m);
    update_svld(in, m, ev);
    for (int k = 0; k < 3; ++k) update_dialog(k, in, m, ev);
    update_option(in, m, ev);
    update_category(in, m, ev);
    update_bar(in, m, ev);
    update_grid(in, m, ev);
    update_viewer(in, m, ev);
    update_manual(in, m, ev);
    update_charsel(in, m, ev);
    if (sub_ == -1) { open_ = false; ev.closed = true; }
}

void PauseBook::draw(DrawList& out, const BookModel& m) const {
    if (!open_) return;
    // The frame and tabs (FUN_005a73c0): AltPillar16-9 / 16-10 in raw pixels, then BACK00.
    DrawCmd pillars;
    pillars.kind = DrawCmd::Pillars;
    pillars.tex = menu(48);
    out.push_back(pillars);
    cleria::draw(out, back_);
    for (int i = 0; i < 9; ++i) if (i != page_) cleria::draw(out, plaque_[i]);
    if (page_ != 8) cleria::draw(out, plaque_[page_]);      // showTabs = (page != 8)
    cleria::draw(out, cursor_);
    cleria::draw(out, gold_);
    draw_caption_detail(out, m);
    switch (page_) {
        case 0: draw_equip(out, m); draw_status(out, m); break;
        case 1:
            draw_item(out, m);
            draw_status(out, m);
            if ((pframe_.diffuse >> 24) != 0) {
                cleria::draw(out, pframe_); cleria::draw(out, pyes_); cleria::draw(out, pno_); cleria::draw(out, pcur_);
            }
            draw_map(out, m);
            break;
        case 4: draw_topic(out, m); break;
        case 2: case 6: draw_svld(out); draw_dialog(out, 0); draw_dialog(out, 2); break;
        case 5: draw_svld(out); draw_dialog(out, 1); draw_dialog(out, 2); break;
        case 3: case 7: draw_option(out); break;
        case 8: draw_charsel(out); break;
        default: break;
    }
}


// ==== the Load / Save list (PAUSEBOOK_DRAW_SVLD_OPT.md §1) ====================================

namespace {

float svld_plate_y(int s) { return 130.0f + 72.0f * float(s); }
bool svld_sub(int sub) { return sub == 4 || sub == 6 || sub == 8; }

void tab_uv(MenuSprite& t, int i, bool selected) {
    const float u0 = 80.0f * float(i) / 1024.0f, u1 = (80.0f * float(i) + 80.0f) / 1024.0f;
    if (selected) t.set_uv(u0, 725.0f / 1024.0f, u1, 777.0f / 1024.0f);
    else t.set_uv(u0, 777.0f / 1024.0f, u1, 829.0f / 1024.0f);
}

}  // namespace

// FUN_005b3970(fromOpen)
void PauseBook::build_svld(const BookModel& m, bool from_open) {
    if (from_open && m.save_file_no) {
        const int v = std::clamp(*m.save_file_no, 0, 63);
        svpage_ = v >> 3;
        svrow_ = v & 7;
    }
    svstate_ = 0;
    svpanel_ = make(menu(14), 102.4f, 111, 832, 596, 0, 0, 0.8125f, 0.58203125f);
    const bool pulsing = svcursor_.spulse;
    MenuSprite c = make(menu(14), 149.4f, 127.0f + 72.0f * float(svrow_), 737, 64,
                        0, 0.64453125f, 0.7197265625f, 0.70703125f);
    c.spulse = svcursor_.spulse; c.spulse_dir = svcursor_.spulse_dir;
    c.spulse_cur = svcursor_.spulse_cur; c.spulse_step = svcursor_.spulse_step;
    c.saved_specular = svcursor_.saved_specular;
    svcursor_ = c;
    if (!pulsing) svcursor_.pulse_specular_on(8);
    for (int i = 0; i < 8; ++i) {
        svtab_[i] = make(menu(14), 210.4f + 80.0f * float(i), 64, 80, 52, 0, 0, 1, 1);
        tab_uv(svtab_[i], i, i == svpage_);
    }
    for (int s = 0; s < 8; ++s)
        svplate_[s] = make(menu(14), 152.4f, svld_plate_y(s), 731, 57, 0, 0.586914f, 0.713867f, 0.642578f);
    for (int k = 0; k < 4; ++k)
        svicon_[k] = make(menu(35), 0, 0, 48, 64, 0.1875f * float(k), 0, 0.1875f * float(k + 1), 0.5f);
    rescan(m);
    svld_colours(false, false);
}

// FUN_005b4e30(lit, anim): the panel, the 8 tabs and the 8 plates.
void PauseBook::svld_colours(bool lit, bool animate) {
    const std::uint32_t C = lit ? kWhite : kDim;
    auto col = [&](MenuSprite& s) { if (animate) s.fade_to(C, 0, 16); else s.set_colour(C, 0); };
    col(svpanel_);
    for (MenuSprite& t : svtab_) col(t);
    for (MenuSprite& p : svplate_) col(p);
}

void PauseBook::rescan(const BookModel& m) {
    for (int s = 0; s < 8; ++s) cards_[std::size_t(s)] = m.scan ? m.scan(svpage_ * 8 + s) : SlotCard{};
}

// FUN_005b5100
void PauseBook::update_svld(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = svld_sub(sub_) ? in0 : none;
    bool busy = tween(svpanel_) | tween(svcursor_);
    for (MenuSprite& t : svtab_) busy |= tween(t);
    for (MenuSprite& p : svplate_) busy |= tween(p);
    switch (svstate_) {
        case 0:
            if (svld_sub(sub_)) { svstate_ = 2; svld_colours(true, true); }   // [not modelled] the crash-backup box (8)
            break;
        case 2: {
            if (in.cancel) {
                ev.sounds.push_back(1);
                if (sub_ == 4 || sub_ == 8) sub_ = -1;
                else { sub_ = 0; build_svld(m, false); }
                break;
            }
            int row = (svrow_ + in.vstep + 8) & 7;
            int page = svpage_;
            if (row == svrow_) page = (svpage_ + in.hstep + 8) & 7;
            // The mouse [EXPORT FUN_005b5100]: motion hovers the 8 plates; a click on a tab sets the
            // page (not motion-gated); the wheel steps the row (up = row - 1) and warps the pointer to
            // the plate's bottom-right (883, 187 + 72 row), mode 2 [BYTES 0x5b5841..0x5b589c].
            if (in.moved)
                for (int s = 0; s < 8; ++s)
                    if (hit(svplate_[s], in)) { row = s; break; }
            if (in.click)
                for (int t = 0; t < 8; ++t)
                    if (hit(svtab_[t], in)) { page = t; break; }
            if (in.wheel != 0) {
                row = (row - (in.wheel > 0 ? 1 : -1) + 8) & 7;
                ev.warp = true; ev.warp_x = 883; ev.warp_y = 187 + 72 * row; ev.warp_mode = 2;
            }
            if (page != svpage_) {
                ev.sounds.push_back(3);
                tab_uv(svtab_[svpage_], svpage_, false);
                svpage_ = page;
                tab_uv(svtab_[svpage_], svpage_, true);
                rescan(m);
            }
            if (row != svrow_) {
                svrow_ = row;
                ev.sounds.push_back(3);
                svcursor_.set_rect(149.4f, 127.0f + 72.0f * float(row), 737, 64);
            }
            if (in.confirm || (in.click && hit(svplate_[svrow_], in))) {
                const int slot = svpage_ * 8 + svrow_;
                const bool card = cards_[std::size_t(svrow_)].status == 1;
                auto highlight = [&] {
                    svstate_ = 3;
                    svld_colours(false, true);
                    svcursor_.pulse_specular_off();
                    svplate_[svrow_].set_colour(kWhite, 0);
                };
                if (sub_ == 6 || sub_ == 8) {
                    if (!card) { ev.sounds.push_back(2); break; }
                    highlight();
                    ev.sounds.push_back(0);
                    sub_ = sub_ == 6 ? 7 : 9;
                } else if (sub_ == 4) {
                    highlight();
                    if (card) { ev.sounds.push_back(0); sub_ = 5; }
                    else {                                     // an empty / broken slot: write now
                        if (m.save_file_no) *m.save_file_no = slot;
                        ev.save_slot = slot;
                        ev.sounds.push_back(9);
                    }
                }
            }
            break;
        }
        case 3:
            if (!busy) svstate_ = 4;
            break;
        case 4:
            if (svld_sub(sub_) && !busy) {
                build_svld(m, false);
                svstate_ = 2;
                svld_colours(true, true);
            }
            break;
        default: break;
    }
}

// FUN_005b3e10
void PauseBook::draw_svld(DrawList& out) const {
    cleria::draw(out, svpanel_);
    for (const MenuSprite& t : svtab_) cleria::draw(out, t);
    char buf[64];
    for (int s = 0; s < 8; ++s) {
        const MenuSprite& P = svplate_[s];
        const SlotCard& r = cards_[std::size_t(s)];
        const std::uint32_t D = P.diffuse;
        cleria::draw(out, P);
        const float k = float(D & 0xFF) / 256.0f;
        const std::uint32_t cream = 0xFF000000u | (std::uint32_t(240.0f * k) << 16) |
                                    (std::uint32_t(210.0f * k) << 8) | std::uint32_t(180.0f * k);
        std::snprintf(buf, sizeof buf, "No.%02d", svpage_ * 8 + s + 1);
        out.push_back(text_cmd(DrawCmd::TextMiddle, buf, P.x + 25.6f, P.y + 22.4f, cream));
        if (r.status == 0) {
            out.push_back(text_cmd(DrawCmd::TextMiddle, "-------------- No Data ----------------",
                                   P.x + 140.8f, P.y + 19.2f, D));
            continue;
        }
        if (r.status != 1) {
            out.push_back(text_cmd(DrawCmd::TextMiddle, "----------- Broken Data --------------",
                                   P.x + 140.8f, P.y + 19.2f, D));
            continue;
        }
        std::snprintf(buf, sizeof buf, "Lv.%2d", r.level);
        out.push_back(text_cmd(DrawCmd::TextMiddle, buf, P.x + 147.2f, P.y + 8, D));
        static const char* kDiff[3] = {"NORMAL", "HARD", "NIGHTMARE"};
        const char* diff = r.inferno ? "Inferno" : r.very_easy ? "VERY EASY" : r.easy ? "EASY"
                         : kDiff[std::clamp(r.difficulty, 0, 2)];
        out.push_back(text_cmd(DrawCmd::TextMini, diff, P.x + 148.8f, P.y + 38.4f, D));
        out.push_back(text_cmd(DrawCmd::TextMiddle, r.date, P.x + 238.4f, P.y + 8, D));
        out.push_back(text_cmd(DrawCmd::TextMiddle, r.time, P.x + 257.6f, P.y + 33.6f, D));
        if (r.play_frames != 0) {
            out.push_back(text_cmd(DrawCmd::TextMini, "Play Time ", P.x + 412.8f, P.y + 4.8f, D));
            const std::uint64_t ms = std::uint64_t(r.play_frames) * 1000u / 60u;
            const std::uint64_t h = std::min<std::uint64_t>(ms / 3600000u, 9999u);
            std::snprintf(buf, sizeof buf, "%d:%02d:%02d:%03d", int(h), int(ms / 60000u % 60u),
                          int(ms / 1000u % 60u), int(ms % 1000u));
            out.push_back(text_cmd(DrawCmd::TextMini, buf, P.x + 412.8f + 90.0f, P.y + 4.8f, D));
        }
        DrawCmd area = text_cmd(DrawCmd::TextGdi, r.area, P.x + 412.8f, P.y + 16, D);
        area.scale = r.area.size() >= 34 ? 0.3f : r.area.size() >= 18 ? 0.5f : 0.8f;
        out.push_back(area);
        if (r.character >= 0 && r.character < 4) {
            MenuSprite ic = svicon_[r.character];
            ic.set_rect(250.4f, 127.0f + 72.0f * float(s), 48, 64);
            ic.set_colour(D, 0);
            cleria::draw(out, ic);
        }
    }
    if (svstate_ != 0) cleria::draw(out, svcursor_);
}

// ==== the Load / Save / Delete dialogs (§2) ===================================================

namespace {
constexpr int kDlgTex[3] = {15, 16, 17};
constexpr float kBtnX[3] = {531, 658, 658}, kBtnY[3] = {128, 128, 12};
constexpr int kDlgSub[3] = {-1, 5, 10};                     // Load is 7 (book) or 9 (title)
}  // namespace

// FUN_005b6c50 / FUN_005b7a60 / FUN_005b6010: everything hidden.
void PauseBook::build_dialog(int k) {
    Dialog& d = dlg_[k];
    const int ret = d.ret;
    d = Dialog{};
    d.ret = ret;
    d.bg = make(menu(kDlgTex[k]), 0, 0, 768, 166.4f, 0, 0, 0.75f, 0.65f);
    d.btn[0] = make(menu(kDlgTex[k]), 0, 0, 89, 25.6f, 0, 0.7f, 89.0f / 1024.0f, 0.8f);
    d.btn[1] = make(menu(kDlgTex[k]), 0, 0, 89, 25.6f, 89.6f / 1024.0f, 0.7f, 178.6f / 1024.0f, 0.8f);
    d.btn[2] = make(menu(kDlgTex[k]), 0, 0, 89, 25.6f, 179.2f / 1024.0f, 0.7f, 268.2f / 1024.0f, 0.8f);
    if (k == 1) d.cursor = make(menu(16), 0, 0, 96, 40, 268.8f / 1024.0f, 169.6f / 256.0f, 364.8f / 1024.0f, 209.6f / 256.0f);
    else d.cursor = make(menu(kDlgTex[k]), 0, 0, 96, 40, 267.0f / 1024.0f, 168.0f / 256.0f, 363.0f / 1024.0f, 208.0f / 256.0f);
    for (MenuSprite* s : {&d.bg, &d.btn[0], &d.btn[1], &d.btn[2], &d.cursor}) s->set_colour(0, 0);
    d.cursor.pulse_specular_on(8);
}

void PauseBook::update_dialog(int k, const BookInput& in0, BookModel& m, BookEvents& ev) {
    Dialog& d = dlg_[k];
    const bool mine = k == 0 ? (sub_ == 7 || sub_ == 9) : sub_ == kDlgSub[k];
    const BookInput none{};
    const BookInput& in = mine ? in0 : none;
    const int nbtn = k == 2 ? 2 : 3;
    bool busy = tween(d.bg) | tween(d.cursor);
    for (int b = 0; b < nbtn; ++b) busy |= tween(d.btn[b]);
    const float ox = float(int(152.4f - 18.0f)), oy = float(int(svld_plate_y(svrow_) - 56.0f));
    auto place_cursor = [&] {
        d.cursor.set_rect(ox + kBtnX[d.cur] - 3.5f, oy + kBtnY[d.cur] - 7.2f, 96, 40);
    };
    const int list_sub = k == 0 ? (sub_ == 9 ? 8 : 6) : k == 1 ? 4 : d.ret;
    switch (d.state) {
        case 0:
            if (!mine) break;
            d.bg.set_rect(ox, oy, 768, 166.4f);
            for (int b = 0; b < nbtn; ++b) d.btn[b].set_rect(ox + kBtnX[b], oy + kBtnY[b], 89, 25.6f);
            d.cur = (k == 0 && sub_ == 9) ? 0 : 1;
            place_cursor();
            d.bg.fade_to(kWhite, 0, 64); d.cursor.fade_to(kWhite, 0, 64);
            for (int b = 0; b < nbtn; ++b) d.btn[b].fade_to(kWhite, 0, 64);
            d.state = 1;
            break;
        case 1: {
            if (in.cancel) {
                ev.sounds.push_back(1);
                sub_ = list_sub;
                build_dialog(k);
                break;
            }
            int cur = d.cur;
            if (in.hstep != 0 && cur != 2) cur = (cur + in.hstep) & 1;
            if (in.vstep != 0 && k != 2) cur = cur != 2 ? 2 : 1;
            if (in.moved)                                    // the buttons [EXPORT FUN_005b6eb0 / 5b7cc0 / 5b6220]
                for (int b = 0; b < nbtn; ++b)
                    if (hit(d.btn[b], in)) { cur = b; break; }
            if (cur != d.cur) { d.cur = cur; ev.sounds.push_back(3); place_cursor(); }
            if (!(in.confirm || (in.click && hit(d.btn[d.cur], in)))) break;
            d.state = 2;
            d.cursor.pulse_specular_off();
            d.cursor.fade_to(kWhite, 0x808080, 64);
            d.btn[d.cur].fade_to(kWhite, 0x808080, 64);
            const int slot = svpage_ * 8 + svrow_;
            if (k == 0) {
                ev.sounds.push_back(0);
                if (d.cur == 0) {
                    if (m.save_file_no) *m.save_file_no = slot;
                    ev.load_slot = slot;
                    sub_ = -1;
                } else if (d.cur == 2) { dlg_[2].ret = list_sub; sub_ = 10; }
                else sub_ = list_sub;
            } else if (k == 1) {
                sub_ = 4;
                if (d.cur == 0) {
                    if (m.save_file_no) *m.save_file_no = slot;
                    ev.save_slot = slot;
                    ev.sounds.push_back(9);
                } else {
                    ev.sounds.push_back(0);
                    if (d.cur == 2) { dlg_[2].ret = 4; sub_ = 10; }
                }
            } else {
                ev.sounds.push_back(0);
                sub_ = d.ret;
                if (d.cur == 0) ev.delete_slot = slot;
            }
            break;
        }
        case 2:
            if (!busy) {
                d.state = 3;
                d.cursor.fade_to(d.cursor.diffuse_t, 0, 64);
                for (int b = 0; b < nbtn; ++b) d.btn[b].fade_to(d.btn[b].diffuse_t, 0, 64);
            }
            break;
        case 3:
            if (!busy) {
                d.state = 0;
                d.cursor.pulse_specular_on(8);
                d.bg.fade_to(0, 0, 64); d.cursor.fade_to(0, 0, 64);
                for (int b = 0; b < nbtn; ++b) d.btn[b].fade_to(0, 0, 64);
            }
            break;
        default: break;
    }
}

void PauseBook::draw_dialog(DrawList& out, int k) const {
    const Dialog& d = dlg_[k];
    if ((d.bg.diffuse >> 24) == 0) return;
    cleria::draw(out, d.bg);
    for (int b = 0; b < (k == 2 ? 2 : 3); ++b) cleria::draw(out, d.btn[b]);
    cleria::draw(out, d.cursor);
}

// ==== the Option page (§3) ====================================================================

namespace {
constexpr float kOptY[10] = {96, 150, 204, 258, 312, 366, 420, 474, 572, 626};
constexpr float kOptX[10] = {179, 179, 179, 179, 179, 179, 179, 409, 307, 307};
constexpr float kOptUV[10][2] = {{0, 0}, {0, 51}, {0, 102}, {0, 409}, {0, 460}, {0, 512}, {0, 563},
                                 {409, 307}, {0, 153}, {0, 204}};
constexpr float kChkY[4] = {259, 313, 367, 421};
void option_row_uv(MenuSprite& r, int row, bool lit) {
    const float u = kOptUV[row][0] + (lit ? 409.6f : 0.0f), v = kOptUV[row][1];
    r.set_uv(u / 1024.0f, v / 1024.0f, (u + 409.6f) / 1024.0f, (v + 51.2f) / 1024.0f);
}
}  // namespace

void PauseBook::place_knob(int i, int value) {
    oknob_[i].set_rect(652.0f + std::trunc(float(value) * 188.8f / 1024.0f), kOptY[i], 16, 51.2f);
}

// FUN_005ba1f0: the switch faces.
void PauseBook::option_checks(const BookModel& m) {
    for (int i = 0; i < 4; ++i) {
        const float u = (m.checks && m.checks[i]) ? 0.0f : 51.0f;
        ocheck_[i].set_uv(u / 1024.0f, 0.6f, (u + 51.2f) / 1024.0f, 0.65f);
    }
}

// FUN_005b9b30 (+ the slider and message-box builders)
void PauseBook::build_option(const BookModel& m) {
    ostate_ = 0;
    for (int r = 0; r < 10; ++r) {
        orow_spr_[r] = make(menu(19), kOptX[r], kOptY[r], 409.6f, 51.2f, 0, 0, 1, 1);
        option_row_uv(orow_spr_[r], r, false);
    }
    ocursor_.pulse_specular_off();
    ocursor_ = make(menu(19), kOptX[orow_], kOptY[orow_], orow_ == 7 ? 179.2f : 409.6f, 51.2f,
                    0, 0.35f, 0.4f, 0.4f);
    ocursor_.pulse_specular_on(8);
    for (int i = 0; i < 4; ++i) ocheck_[i] = make(menu(19), 732, kChkY[i], 51.2f, 51.2f, 0, 0, 1, 1);
    option_checks(m);
    for (int i = 0; i < 3; ++i) {
        otrack_[i] = make(menu(19), 652, kOptY[i], 204.8f, 51.2f, 0.4f, 0.35f, 0.6f, 0.4f);
        oknob_[i] = make(menu(19), 652, kOptY[i], 16, 51.2f, 0, 0.3f, 16.0f / 1024.0f, 0.35f);
        place_knob(i, m.sliders ? m.sliders[i] : 512);
    }
    for (int b = 0; b < 2; ++b) {
        Dialog& d = omsg_[b];
        d = Dialog{};
        d.bg = make(menu(20), 307, 300, 409.6f, 153.6f, 0, b == 0 ? 0.0f : 0.3f, 0.8f, b == 0 ? 0.3f : 0.6f);
        d.btn[0] = make(menu(20), 409, 398, 88, 26, 94.0f / 512, 384.0f / 512, 182.0f / 512, 410.0f / 512);
        d.btn[1] = make(menu(20), 537, 398, 88, 26, 182.0f / 512, 384.0f / 512, 270.0f / 512, 410.0f / 512);
        d.cur = 1;
        d.cursor = make(menu(20), 537 - 3, 398 - 3, 94, 32, 0, 378.0f / 512, 94.0f / 512, 410.0f / 512);
        d.cursor.pulse_specular_on(8);
        for (MenuSprite* s : {&d.bg, &d.btn[0], &d.btn[1], &d.cursor}) s->set_colour(0, 0);
    }
    option_colours(false, false);
}

// FUN_005bae20(lit, anim): the rows and the switches.
void PauseBook::option_colours(bool lit, bool animate) {
    const std::uint32_t C = lit ? kWhite : kDim;
    auto col = [&](MenuSprite& s) { if (animate) s.fade_to(C, 0, 16); else s.set_colour(C, 0); };
    for (MenuSprite& r : orow_spr_) col(r);
    for (MenuSprite& c : ocheck_) col(c);
}

// FUN_005ba2b0 (+ FUN_005b9480 sliders, FUN_005b8d40 message boxes)
void PauseBook::update_option(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    // The sliders.
    for (int i = 0; i < 3; ++i) {
        const bool active = sub_ == 0x0c + i;
        const float ku = active ? 16.0f : 0.0f, tu = active ? 614.4f : 409.6f;
        oknob_[i].set_uv(ku / 1024.0f, 0.3f, (ku + 16.0f) / 1024.0f, 0.35f);
        otrack_[i].set_uv(tu / 1024.0f, 0.35f, (tu + 204.8f) / 1024.0f, 0.4f);
        if (!active || !m.sliders) continue;
        if (in0.cancel) { sub_ = 0x0b; ev.sounds.push_back(1); continue; }
        int v = m.sliders[i];
        // The drag [BYTES 0x5b948c..0x5b96e7]: while 0x10000 is held, a hit on the knob grabs it
        // (+0x18 = knob.x - raw x); then on motion value = trunc((raw x + off - base) * max / 188.8),
        // with the RAW x (not unprojected: at 16:9 the knob outruns the pointer, a retail quirk).
        if (in0.click_held) {
            if (hit(oknob_[i], in0)) { odrag_[i] = true; odrag_off_[i] = int(oknob_[i].x - float(in0.mx)); }
            if (odrag_[i] && in0.moved) v = int((float(in0.mx) + float(odrag_off_[i]) - 652.0f) * 1024.0f / 188.8f);
        } else {
            odrag_[i] = false;
        }
        // The keys: +-max/128 a tick, only outside mouse mode and without MOUSE_DIR held.
        if (!in0.dir_held && !in0.mouse_mode) {
            if (in0.held_left) v -= 8;
            if (in0.held_right) v += 8;
        }
        v = std::clamp(v, 0, 1024);
        if (v != m.sliders[i]) { m.sliders[i] = v; place_knob(i, v); ev.options_changed = true; }
    }
    // The message boxes.
    for (int b = 0; b < 2; ++b) {
        Dialog& d = omsg_[b];
        const bool mine = sub_ == 0x0f + b;
        const BookInput& in = mine ? in0 : none;
        bool busy = tween(d.bg) | tween(d.cursor) | tween(d.btn[0]) | tween(d.btn[1]);
        auto all = [&](std::uint32_t c) { for (MenuSprite* s : {&d.bg, &d.btn[0], &d.btn[1], &d.cursor}) s->fade_to(c, 0, 64); };
        switch (d.state) {
            case 0:
                if (mine) { all(kWhite); d.state = 1; }
                break;
            case 1:
                if (in.cancel) { sub_ = 0x0b; ev.sounds.push_back(1); all(0); d.state = 0; break; }
                {
                    int cur = d.cur;
                    if (in.hstep != 0) cur ^= 1;
                    if (in.moved) {                          // Yes +0x16c / No +0x218 [EXPORT FUN_005b8d40]
                        if (hit(d.btn[0], in)) cur = 0;
                        else if (hit(d.btn[1], in)) cur = 1;
                    }
                    if (cur != d.cur) {
                        d.cur = cur;
                        ev.sounds.push_back(3);
                        d.cursor.set_rect(d.btn[d.cur].x - 3, d.btn[d.cur].y - 3, 94, 32);
                    }
                }
                if (in.confirm || (in.click && hit(d.btn[d.cur], in))) {
                    sub_ = 0x0b;
                    ev.sounds.push_back(0);
                    d.cursor.pulse_specular_off();
                    d.btn[d.cur].fade_to(kWhite, 0x808080, 64);
                    d.state = 2;
                    if (d.cur == 0) { sub_ = -1; ev.option_quit = b; }
                }
                break;
            case 2:
                if (!busy) { d.btn[d.cur].fade_to(kWhite, 0, 64); d.state = 3; }
                break;
            case 3:
                if (!busy) { d.cursor.pulse_specular_on(8); all(0); d.state = 0; }
                break;
            default: break;
        }
    }
    // The rows.
    const bool mine = sub_ == 0x0b || sub_ == 0x11;
    const BookInput& in = mine ? in0 : none;
    bool busy = tween(ocursor_);
    for (MenuSprite& r : orow_spr_) busy |= tween(r);
    for (MenuSprite& c : ocheck_) busy |= tween(c);
    for (int i = 0; i < 3; ++i) busy |= tween(otrack_[i]) | tween(oknob_[i]);
    switch (ostate_) {
        case 0:
            if (mine) { ostate_ = 1; option_colours(true, true); oenter_ = sub_; }
            break;
        case 1: {
            if (in.cancel) {
                ev.sounds.push_back(1);
                if (oenter_ == 0x11) sub_ = -1;
                else { sub_ = 0; build_option(m); }
                break;
            }
            int row = (orow_ + in.vstep + 10) % 10;
            if (in.moved)                                    // the 10 rows 0x73d564 [BYTES 0x5ba53f..0x5ba8b8]
                for (int r = 0; r < 10; ++r)
                    if (hit(orow_spr_[r], in)) { row = r; break; }
            if (row != orow_) {
                orow_ = row;
                ev.sounds.push_back(3);
                ocursor_.set_rect(kOptX[row], kOptY[row], row == 7 ? 179.2f : 409.6f, 51.2f);
            }
            if (!(in.confirm || (in.click && hit(orow_spr_[orow_], in)))) break;
            ev.sounds.push_back(0);
            if (orow_ >= 3 && orow_ <= 6) {
                if (m.checks) m.checks[orow_ - 3] = !m.checks[orow_ - 3];
                option_checks(m);
                ev.options_changed = true;
            } else if (orow_ == 7) {                         // Default
                if (m.sliders) {
                    for (int i = 0; i < 3; ++i) { m.sliders[i] = 512; place_knob(i, 512); }
                }
                if (m.checks) { m.checks[0] = false; m.checks[1] = true; m.checks[2] = false; m.checks[3] = false; }
                option_checks(m);
                ev.options_changed = true;
            } else {
                static constexpr int kSub[10] = {0x0c, 0x0d, 0x0e, 0, 0, 0, 0, 0, 0x0f, 0x10};
                sub_ = kSub[orow_];
                ostate_ = 2;
                for (MenuSprite& r : orow_spr_) r.fade_to(kDim, 0, 32);
                orow_spr_[orow_].fade_to(kWhite, 0x808080, 32);
            }
            break;
        }
        case 2:
            if (!busy) {
                orow_spr_[orow_].fade_to(kWhite, 0, 16);
                option_row_uv(orow_spr_[orow_], orow_, true);
                ostate_ = 3;
            }
            break;
        case 3:
            if (sub_ == 0x0b || sub_ == 0x11) {
                const int enter = oenter_;
                build_option(m);
                option_colours(true, false);
                sub_ = enter;
            }
            break;
        default: break;
    }
}

void PauseBook::draw_option(DrawList& out) const {
    for (const MenuSprite& r : orow_spr_) cleria::draw(out, r);
    for (const MenuSprite& c : ocheck_) cleria::draw(out, c);
    if (ostate_ == 1) cleria::draw(out, ocursor_);
    for (int i = 0; i < 3; ++i) { cleria::draw(out, otrack_[i]); cleria::draw(out, oknob_[i]); }
    for (const Dialog& d : omsg_) {
        if ((d.bg.diffuse >> 24) == 0) continue;
        cleria::draw(out, d.bg);
        cleria::draw(out, d.btn[0]);
        cleria::draw(out, d.btn[1]);
        if (d.state == 1) cleria::draw(out, d.cursor);
    }
}


// ==== the Topic page (PAUSEBOOK_DRAW_TOPIC_MAP.md §1-§5) ======================================

namespace {

struct TopicEntry {
    int id;
    const char* portrait;
    bool boss;
    int stats[6];                           // HP, STR, EXP, LV, DEF, GOLD (bosses only)
};
struct BossText { int id; const char* text; };
struct MapRec { float x, y, x2, y2; int link[6]; };
#include "cleria/pausebook_tables.inc"

constexpr std::uint32_t kGlow = 0x66FFFFFF;
constexpr float kBarY[3] = {120, 291, 442};
constexpr float kGlowNudge[3] = {12, -18, -18};
constexpr float kTBtnX[3] = {307.2f, 443.2f, 579.2f};
constexpr float kTBtnU[3] = {0.0f, 0.265625f, 0.53125f};
constexpr float kTBtnU1[3] = {0.265625f, 0.53125f, 0.796875f};
constexpr int kMonsterCounts[3] = {32, 32, 28};

float cell_x(int s) { return 96.0f + 208.0f * float(s / 8); }
float cell_y(int s) { return 64.0f + 52.0f * float(s % 8); }
MenuSprite glow_at(float x, float y, float w, float h) {
    MenuSprite g = make(menu(30), x, y, w, h, 0, 0, 0.703125f, 0.6796875f);
    g.set_colour(kGlow, 0);
    g.pulse_specular_on(8);
    return g;
}
const TopicEntry& topic_entry_at(int set, int global) {
    return set == 0 ? kTopicChars[std::clamp(global, 0, 27)] : kTopicMonsters[std::clamp(global, 0, 91)];
}
std::string right_text_fmt(const char* fmt, int v) {
    char b[32];
    std::snprintf(b, sizeof b, fmt, v);
    return b;
}

}  // namespace

int PauseBook::topic_count(int page) const {
    return tset_ == 0 ? 28 : kMonsterCounts[std::clamp(page, 0, 2)];
}
int PauseBook::topic_base() const {                         // FUN_005be540
    int b = 0;
    for (int p = 0; p < gpage_ && tset_ != 0; ++p) b += kMonsterCounts[p];
    return b;
}
int PauseBook::topic_entry(int slot) const {
    if (slot < 0 || slot >= topic_count(gpage_)) return -1;
    return topic_base() + slot;
}
bool PauseBook::topic_seen(const BookModel& m, int slot) const {   // FUN_005be580 + rec[0]
    const int g = topic_entry(slot);
    if (g < 0 || !m.topic_seen) return false;
    return m.topic_seen[topic_entry_at(tset_, g).id] != 0;
}

// FUN_005bb810
void PauseBook::build_category() {
    tstate_ = 0;
    for (int i = 0; i < 3; ++i) {
        tbar_[i] = make(menu(27 + i), 150, kBarY[i], 720, 174, 0, 0, 0.703125f, 0.6796875f);
        tbar_[i].set_colour(kDim, 0);
    }
    tglow_.pulse_specular_off();
    tglow_ = glow_at(150, kBarY[tcur_] + kGlowNudge[tcur_], 720, 174);
}

// FUN_005bbae0
void PauseBook::update_category(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 0x12 ? in0 : none;
    bool busy = tween(tglow_);
    for (MenuSprite& b : tbar_) busy |= tween(b);
    switch (tstate_) {
        case 0:
            if (sub_ == 0x12) { tstate_ = 1; for (MenuSprite& b : tbar_) b.fade_to(kWhite, 0, 16); }
            break;
        case 1: {
            if (in.cancel) { ev.sounds.push_back(1); sub_ = 0; build_category(); break; }
            const int cur = (in.vstep + tcur_ + 3) % 3;
            if (cur != tcur_) {
                tcur_ = cur;
                ev.sounds.push_back(3);
                tglow_.set_rect(150, kBarY[cur] + kGlowNudge[cur], 720, 174);
            }
            if (!in.confirm) break;
            if (tcur_ < 2) { sub_ = 0x13; enter_set(m, tcur_); }
            else sub_ = 0x16;
            ev.sounds.push_back(0);
            tstate_ = 2;
            for (MenuSprite& b : tbar_) b.fade_to(0, 0, 32);
            break;
        }
        case 2:
            if (!busy) tstate_ = 3;
            break;
        case 3:
            if (sub_ == 0x12) {
                build_category();
                for (MenuSprite& b : tbar_) b.set_colour(kWhite, 0);
            }
            break;
        default: break;
    }
}

// FUN_005bcdf0: nothing writes the per-set saved cursor, so every entry starts at 0 / page 0.
void PauseBook::enter_set(const BookModel& m, int set) {
    tset_ = set;
    gcur_ = 0;
    gpage_ = 0;
    build_sheet(m);
}

// FUN_005bce30
void PauseBook::build_sheet(const BookModel& m) {
    gstate_ = 0;
    tbook_ = make(menu(36), 0, 60, 1024, 768, 0, 0, 1, 0.75f);
    tbook_.set_colour(kDim, 0);
    const int sheet = tset_ == 0 ? 37 : 38 + std::clamp(gpage_, 0, 2);
    for (int s = 0; s < 32; ++s) {
        const float x = cell_x(s), y = cell_y(s);
        const float dv = topic_seen(m, s) ? 512.0f : 0.0f;
        tcell_[s] = make(menu(sheet), x, y + 56, 208, 52, x / 1024, (y + dv) / 1024, (x + 208) / 1024, (y + dv + 52) / 1024);
        tcell_[s].set_colour(kDim, 0);
    }
    gcursor_ = make(menu(47), 0, 0, 208, 52, 0, 0, 0.78125f, 0.8125f);
    gglow_.pulse_specular_off();
    gglow_ = glow_at(0, 0, 208, 52);
    place_grid_cursor(m);
}

void PauseBook::place_grid_cursor(const BookModel& m) {
    const MenuSprite& c = tcell_[std::clamp(gcur_, 0, 31)];
    gcursor_.set_rect(c.x, c.y - 4, 208, 52);
    gcursor_.set_colour(topic_seen(m, gcur_) ? kWhite : 0x60FFFFFF, 0);
    gglow_.set_rect(c.x, c.y, 208, 52);
}

// FUN_005bc260
void PauseBook::build_bar() {
    bstate_ = 0;
    for (int i = 0; i < 3; ++i) tbtn_[i] = make(menu(44), kTBtnX[i], 672, 136, 56, 0, 0, 1, 1);
    bar_uvs();
    bglow_.pulse_specular_off();
    bglow_ = glow_at(kTBtnX[bcur_], 672, 136, 56);
}
void PauseBook::bar_uvs() {
    for (int i = 0; i < 3; ++i) {
        const bool lit = i == bcur_;
        tbtn_[i].set_uv(kTBtnU[i], lit ? 0.4375f : 0.0f, kTBtnU1[i], lit ? 0.875f : 0.4375f);
    }
}

// FUN_005bdca0
void PauseBook::pager(const BookModel& m, int d) {
    const int n = tset_ == 0 ? 1 : 3;
    gpage_ = (gpage_ + n + d) % n;
    const int sheet = tset_ == 0 ? 37 : 38 + gpage_;
    for (int s = 0; s < 32; ++s) {
        const float x = cell_x(s), y = cell_y(s);
        const float dv = topic_seen(m, s) ? 512.0f : 0.0f;
        tcell_[s].tex = menu(sheet);
        tcell_[s].set_uv(x / 1024, (y + dv) / 1024, (x + 208) / 1024, (y + dv + 52) / 1024);
    }
    if (gcur_ >= topic_count(gpage_)) {
        gcur_ = topic_count(gpage_) - 1;
        gcursor_.set_rect(tcell_[gcur_].x, tcell_[gcur_].y, 208, 52);
        gglow_.set_rect(tcell_[gcur_].x, tcell_[gcur_].y, 208, 52);
    }
}

// FUN_005bc6a0
void PauseBook::update_bar(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 0x13 ? in0 : none;
    bool busy = tween(bglow_);
    for (MenuSprite& b : tbtn_) busy |= tween(b);
    switch (bstate_) {
        case 0:
            if (sub_ == 0x13) { bstate_ = 1; for (MenuSprite& b : tbtn_) b.fade_to(kWhite, 0, 16); }
            break;
        case 1: {
            if (in.cancel) { ev.sounds.push_back(1); sub_ = 0x12; build_bar(); break; }
            const int cur = (in.hstep + bcur_ + 3) % 3;
            if (cur != bcur_) {
                bcur_ = cur;
                ev.sounds.push_back(3);
                bar_uvs();
                bglow_.set_rect(kTBtnX[cur], 672, 136, 56);
            }
            if (!in.confirm) break;
            ev.sounds.push_back(0);
            if (bcur_ == 0) pager(m, -1);
            else if (bcur_ == 2) pager(m, +1);
            else {
                sub_ = 0x14;
                bstate_ = 2;
                for (MenuSprite& b : tbtn_) b.fade_to(kDim, 0, 32);
            }
            break;
        }
        case 2:
            if (!busy) bstate_ = 3;
            break;
        case 3:
            if (sub_ == 0x13) { build_bar(); for (MenuSprite& b : tbtn_) b.set_colour(kWhite, 0); }
            break;
        default: break;
    }
}

// FUN_005bd480
void PauseBook::update_grid(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 0x14 ? in0 : none;
    bool busy = tween(tbook_) | tween(gcursor_) | tween(gglow_);
    for (MenuSprite& c : tcell_) busy |= tween(c);
    switch (gstate_) {
        case 0:
            if (sub_ == 0x14) {
                gstate_ = 1;
                tbook_.fade_to(kWhite, 0, 16);
                for (MenuSprite& c : tcell_) c.fade_to(kWhite, 0, 16);
            }
            break;
        case 1: {
            if (in.cancel) { ev.sounds.push_back(1); sub_ = 0x13; build_sheet(m); break; }
            const int n = topic_count(gpage_);
            int c = gcur_;
            const int row = c % 8;
            if (in.vstep < 0) c = row == 0 ? std::min(c + 7, n - 1) : (c + n - 1) % n;
            else if (in.vstep > 0) c = c == n - 1 ? c - row : row == 7 ? c - 7 : (c + n + 1) % n;
            const int row2 = c % 8;
            if (in.hstep < 0) {
                c -= 8;
                if (c < 0) { c += 40; while (c >= n) c -= 8; }
            } else if (in.hstep > 0) {
                c += 8;
                if (c >= n) c = row2;
            }
            if (c != gcur_) { gcur_ = c; ev.sounds.push_back(3); place_grid_cursor(m); }
            if (!in.confirm) break;
            sub_ = 0x15;
            ev.sounds.push_back(0);
            gstate_ = 2;
            for (MenuSprite& cell : tcell_) cell.fade_to(kDim, 0, 32);
            tcell_[gcur_].fade_to(kWhite, 0x808080, 32);
            break;
        }
        case 2:
            if (!busy) gstate_ = 3;
            break;
        case 3:
            if (sub_ == 0x14) {
                build_sheet(m);
                tbook_.set_colour(kWhite, 0);
                for (MenuSprite& cell : tcell_) cell.set_colour(kWhite, 0);
            }
            break;
        default: break;
    }
}

// FUN_005be780
void PauseBook::build_viewer() {
    vstate_ = 0;
    vbg_ = make(menu(tset_ == 0 ? 43 : 41), 0, 60, 1024, 768, 0, 0, 1, 0.75f);
    vbg_.set_colour(kDim, 0);
    for (int b = 0; b < 2; ++b) {
        const int i = b == 0 ? 0 : 2;
        const bool lit = b == vcur_;
        vbtn_[b] = make(menu(44), kTBtnX[i], 672, 136, 56, kTBtnU[i], lit ? 0.4375f : 0.0f, kTBtnU1[i], lit ? 0.875f : 0.4375f);
        vbtn_[b].set_colour(kDim, 0);
    }
    const int i = vcur_ == 0 ? 0 : 2;
    vglow_.pulse_specular_off();
    vglow_ = glow_at(kTBtnX[i] * 1.6f, 672 * 1.6f, 136 * 1.6f, 56 * 1.6f);   // off-screen, as in the bytes
    vport_[0] = make({}, 120, 116, 360, 160, 0.234375f, 0.21875f, 0.9375f, 0.84375f);
    vport_[1] = make({}, 544, 116, 360, 160, 0.0625f, 0.21875f, 0.765625f, 0.84375f);
    vboss_pic_ = make({}, 120, 128, 360, 432, 0.234375f, 0.125f, 0.9375f, 0.96875f);
}

// FUN_005bff40(1)
void PauseBook::build_card(const BookModel& m) {
    const int c = gcur_, first = c - c % 2;
    const int g = topic_base() + first;
    const TopicEntry& e = topic_entry_at(tset_, g);
    auto text_of = [&](int id, int block) -> std::string {
        if (!m.casts) return {};
        const Cast* cast = m.casts->get(id);
        if (!cast) return {};
        if (block == 0 && vboss_) {
            for (const BossText& b : kBossTextEn) if (b.id == id) return b.text;
        }
        return block == 0 ? cast->description : cast->description2;
    };
    vboss_ = e.boss;
    if (vboss_) {
        vfirst_ = g + c % 2;
        vbg_.tex = menu(42);
        const TopicEntry& b = topic_entry_at(tset_, vfirst_);
        vtext_[0][0] = text_of(b.id, 0);
        vtext_[0][1] = text_of(b.id, 1);
        vboss_pic_.tex = {TexRef::Named, 0, b.portrait};
        vboss_pic_.fade_to(kWhite, 0, 24);
        // The clear-flag gates (0x74ab88 + 4d + c): any of the three difficulty bytes.
        auto gate = [&](int off) {
            return m.clear_flags && (m.clear_flags[off] || m.clear_flags[off + 1] || m.clear_flags[off + 2]);
        };
        static constexpr const char* kFmt[6] = {"%3d", "%3d", "%4d", "%2d", "%3d", "%4d"};
        static constexpr int kGate[6] = {4, 12, 0, -1, 16, 20};   // 0x74ab8c, 94, 88, -, 98, 9c
        for (int k = 0; k < 6; ++k)
            vstat_[0][k] = (kGate[k] < 0 || gate(kGate[k])) ? right_text_fmt(kFmt[k], b.stats[k]) : "???";
        return;
    }
    vfirst_ = g;
    vbg_.tex = menu(tset_ == 0 ? 43 : 41);
    for (int k = 0; k < 2; ++k) {
        const TopicEntry& en = topic_entry_at(tset_, g + k);
        vtext_[k][0] = text_of(en.id, 0);
        vtext_[k][1] = text_of(en.id, 1);
        vport_[k].tex = {TexRef::Named, 0, en.portrait};
        vport_[k].fade_to(kWhite, 0, 24);
        const int id = en.id;
        const Enemy* E = ((id >= 0x0e && id <= 0x53) || (id >= 0xa6 && id <= 0xb9)) && m.enemies && m.gi
                             ? m.enemies->get(id) : nullptr;
        if (!E) { for (std::string& st : vstat_[k]) st = "---"; continue; }
        const EnemyStats es = enemy_stats(*m.gi, *E);
        const int L = E->level;
        const float gold = std::floor(stat_k(E->k_unk_0xd8) *
                                      (L <= 30 ? 4.0f * std::pow(5.0f, float(L / 15)) : 10.0f * float(L) - 200.0f));
        const int kills = m.topic_kills ? m.topic_kills[id] : 0;
        vstat_[k][0] = kills >= 10 ? right_text_fmt("%3d", int(es.max_hp)) : "???";
        vstat_[k][1] = kills >= 20 ? right_text_fmt("%3d", int(es.atk)) : "???";
        vstat_[k][2] = kills >= 40 ? right_text_fmt("%4d", int(es.exp)) : "???";
        vstat_[k][3] = right_text_fmt("%2d", L);
        vstat_[k][4] = kills >= 30 ? right_text_fmt("%3d", int(es.def)) : "???";
        vstat_[k][5] = kills >= 50 ? right_text_fmt("%4d", int(gold)) : "???";
    }
}

// FUN_005bdfd0
void PauseBook::step_card(const BookModel& m, int d) {
    const int n = tset_ == 0 ? 1 : 3;
    gcur_ += d;
    if (gcur_ < 0) { gpage_ = (gpage_ - 1 + n) % n; gcur_ = topic_count(gpage_) - 1; }
    else if (gcur_ >= topic_count(gpage_)) { gpage_ = (gpage_ + 1) % n; gcur_ = 0; }
    (void)m;
}

// FUN_005bf890
void PauseBook::update_viewer(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 0x15 ? in0 : none;
    bool busy = tween(vbg_) | tween(vglow_) | tween(vbtn_[0]) | tween(vbtn_[1]);
    busy |= tween(vport_[0]) | tween(vport_[1]) | tween(vboss_pic_);
    switch (vstate_) {
        case 0:
            if (sub_ == 0x15) {
                vstate_ = 1;
                vbg_.fade_to(kWhite, 0, 16);
                vbtn_[0].fade_to(kWhite, 0, 16); vbtn_[1].fade_to(kWhite, 0, 16);
                build_card(m);
            }
            break;
        case 1: {
            if (in.cancel) { ev.sounds.push_back(1); sub_ = 0x14; build_viewer(); break; }
            const int cur = (vcur_ + 2 + in.hstep) % 2;
            if (cur != vcur_) {
                vcur_ = cur;
                ev.sounds.push_back(3);
                for (int b = 0; b < 2; ++b) {
                    const int i = b == 0 ? 0 : 2;
                    const bool lit = b == vcur_;
                    vbtn_[b].set_uv(kTBtnU[i], lit ? 0.4375f : 0.0f, kTBtnU1[i], lit ? 0.875f : 0.4375f);
                }
                const int i = vcur_ == 0 ? 0 : 2;
                vglow_.set_rect(kTBtnX[i] * 1.6f, 672 * 1.6f, 136 * 1.6f, 56 * 1.6f);
            }
            if (!in.confirm) break;
            int step = 2;
            if (vboss_) step = 1;
            else if (vcur_ == 1 && (gcur_ & 1)) {
                const int g = topic_base() + gcur_ + 1;
                if (tset_ != 0 && g < 92 && topic_entry_at(tset_, g).boss) step = 1;
            }
            step_card(m, vcur_ == 0 ? -step : step);
            ev.sounds.push_back(0);
            build_card(m);
            break;
        }
        default: break;
    }
}

// FUN_005c0f50
void PauseBook::build_manual(const BookModel& m) {
    if (m.manual_on && m.manual_on[0] == 0) { m.manual_on[0] = m.manual_on[1] = m.manual_on[2] = 1; }
    mstate_ = 0;
    mbook_ = make(menu(45), 102.4f, 60, 819.2f, 624, 0, 0, 0.8125f, 0.615234f);
    mbook_.set_colour(kDim, 0);
    for (int i = 0; i < 13; ++i) {
        const float u0 = 64.0f * float(i) / 1024.0f, u1 = 64.0f * float(i + 1) / 1024.0f;
        mtab_[i] = make(menu(46), 96.0f + 64.0f * float(i), 684.8f, 64, 64, u0, 0, u1, 0.5f);
        mtab_[i].set_colour(0, 0);
        mlabel_[i] = make(menu(46), 96.0f + 64.0f * float(i), 684.8f, 64, 19.2f, u0, 0.5625f, u1, 0.734375f);
        mlabel_[i].set_colour(0, 0);
    }
    mglow_.pulse_specular_off();
    mglow_ = make(menu(30), mtab_[mcur_].x, 684.8f, 64, 64, 0, 0, 0.703125f, 0.6796875f);
    mglow_.set_colour(0, 0);
    mglow_.pulse_specular_on(8);
    mpage_ = make({TexRef::Named, 0, kManualPages[std::clamp(m.character, 0, 3)][mshown_]},
                  102.4f, 60, 1008, 1014.4f, 0, 0, 1, 1);
    mpage_.set_colour(kDim, 0);
    mpage_.fade_to(kWhite, 0, 16);                           // FUN_005c1d20(1)
}

// FUN_005c1600(1, 1)
void PauseBook::manual_targets(const BookModel& m) {
    mbook_.fade_to(kWhite, 0, 16);
    mpage_.fade_to(kWhite, 0, 16);
    mglow_.fade_to(kGlow, 0, 16);
    for (int i = 0; i < 13; ++i) {
        const bool on = m.manual_on && m.manual_on[i];
        mtab_[i].fade_to(i == mshown_ && on ? kWhite : kDim, 0, 16);
        mlabel_[i].fade_to(!on ? 0u : i == mshown_ ? kWhite : kDim, 0, 16);
    }
}

// FUN_005c17a0
void PauseBook::update_manual(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 0x16 ? in0 : none;
    tween(mbook_); tween(mglow_); tween(mpage_);
    for (int i = 0; i < 13; ++i) { tween(mtab_[i]); tween(mlabel_[i]); }
    switch (mstate_) {
        case 0:
            if (sub_ == 0x16) { mstate_ = 1; manual_targets(m); }
            break;
        case 1: {
            if (in.cancel) { ev.sounds.push_back(1); sub_ = 0x12; build_manual(m); break; }
            const int cur = (in.hstep + mcur_ + 13) % 13;
            if (cur != mcur_) {
                mcur_ = cur;
                ev.sounds.push_back(3);
                mglow_.set_rect(mtab_[cur].x, 684.8f, 64, 64);
            }
            if (in.confirm && mcur_ != mshown_) {
                ev.sounds.push_back(0);
                mshown_ = mcur_;
                mpage_.tex = {TexRef::Named, 0, kManualPages[std::clamp(m.character, 0, 3)][mshown_]};
                manual_targets(m);
            }
            break;
        }
        default: break;
    }
}

// FUN_005a5d50 case 4
void PauseBook::draw_topic(DrawList& out, const BookModel& m) const {
    if (sub_ < 0x13) {
        for (const MenuSprite& b : tbar_) cleria::draw(out, b);
        if (tstate_ == 1) cleria::draw(out, tglow_);
        return;
    }
    if (sub_ < 0x15) {
        cleria::draw(out, tbook_);
        for (const MenuSprite& c : tcell_) cleria::draw(out, c);
        if (gstate_ == 1) { cleria::draw(out, gcursor_); cleria::draw(out, gglow_); }
        for (const MenuSprite& b : tbtn_) cleria::draw(out, b);
        if (bstate_ == 1) cleria::draw(out, bglow_);
        return;
    }
    if (sub_ == 0x16) {                                      // FUN_005c15a0
        cleria::draw(out, mbook_);
        if (m.manual_on && m.manual_on[mshown_]) cleria::draw(out, mpage_);
        for (int i = 0; i < 13; ++i) { cleria::draw(out, mtab_[i]); cleria::draw(out, mlabel_[i]); }
        cleria::draw(out, mglow_);
        return;
    }
    // FUN_005bf000: the datacard viewer.
    auto right = [&](const std::string& s, float x_right, float y) {
        out.push_back(text_cmd(DrawCmd::TextMiddle, s, x_right - text_width_middle(s), y, kWhite));
    };
    auto block = [&](const std::string& s, float x, float y) {
        if (!s.empty()) out.push_back(text_cmd(DrawCmd::TextGdi, s, x, y, 0xFFC0C0C0, 5));
    };
    cleria::draw(out, vbg_);
    cleria::draw(out, vbtn_[0]);
    cleria::draw(out, vbtn_[1]);
    const float y1 = (198.0f + (tset_ == 0 ? -24.0f : 24.0f)) * 1.6f, y2 = (293.0f + (tset_ == 0 ? -24.0f : 24.0f)) * 1.6f;
    if (vboss_) {
        const int slot = vfirst_ - topic_base();
        const bool locked = topic_entry(slot) >= 0 && !topic_seen(m, slot);
        right(right_text_fmt("%2d", vfirst_ + 1), 902, 92.8f);
        if (locked) return;
        cleria::draw(out, vboss_pic_);
        block(vtext_[0][0], 550.4f, 240);
        block(vtext_[0][1], 550.4f, 392);
        static constexpr float kSX[6] = {649.2f, 764.4f, 899.2f, 649.2f, 764.4f, 899.2f};
        static constexpr float kSY[6] = {176, 176, 176, 214.4f, 214.4f, 214.4f};
        static constexpr int kOrder[6] = {0, 1, 2, 3, 4, 5};   // HP STR EXP / LV DEF GOLD
        for (int k = 0; k < 6; ++k) right(vstat_[0][kOrder[k]], kSX[k], kSY[k]);
        cleria::draw(out, vglow_);
        return;
    }
    const int first = gcur_ - gcur_ % 2;
    const int last_page = tset_ == 0 ? 0 : 2;
    for (int k = 0; k < 2; ++k) {
        const int slot = first + k;
        if (topic_entry_at(tset_, vfirst_ + k).boss) continue;
        if (gpage_ == last_page && gcur_ + k > topic_count(gpage_)) continue;
        const float col = k == 0 ? 0.0f : 422.0f;
        const bool in_range = topic_entry(slot) >= 0;
        if (in_range && !topic_seen(m, slot)) {
            right(right_text_fmt("%2d", vfirst_ + k + 1), 473.6f + col, 92.8f);
            continue;
        }
        const float tx = k == 0 ? 128.0f : 550.4f;
        block(vtext_[k][0], tx, y1);
        block(vtext_[k][1], tx, y2);
        cleria::draw(out, vport_[k]);
        right(right_text_fmt("%2d", vfirst_ + k + 1), 473.6f + col, 92.8f);
        if (tset_ != 0) {
            right(vstat_[k][0], 227.2f + col, 291.2f);
            right(vstat_[k][1], 342.4f + col, 291.2f);
            right(vstat_[k][2], 467.2f + col, 291.2f);
            right(vstat_[k][3], 227.2f + col, 329.6f);
            right(vstat_[k][4], 342.4f + col, 329.6f);
            right(vstat_[k][5], 467.2f + col, 329.6f);
        }
    }
    cleria::draw(out, vglow_);
}

// ==== the warp map (§6) =======================================================================

// FUN_005ab0d0, the map part
void PauseBook::build_map() {
    constexpr float W = 2048, H = 1024;
    mtower_ = make(menu(34), 0, 0, 1024, 768, 0, 0, 0.5f, 0.75f);
    mtower_.set_colour(0, 0x808080);
    mpillar_diffuse_ = 0; mpillar_specular_ = 0x808080;
    mplaque_ = make(menu(34), 16, 16, 288, 144, 1552 / W, 80 / H, 1840 / W, 224 / H);
    mcrystal_ = make(menu(34), 0, 0, 80, 80, 1968 / W, 0, 1, 80 / H);
    for (int i = 0; i < 22; ++i) {
        mplate_[i] = make(menu(34), kMapRecs[i].x, kMapRecs[i].y, 240, 40, 0, 0, 1, 1);
        mplate_[i].fade_to(kWhite, 0, 24);
    }
    mbar_.pulse_specular_off();
    mbar_ = make(menu(34), kMapRecs[mapcur_].x, kMapRecs[mapcur_].y, 240, 40, 1552 / W, 20 / H, 1792 / W, 60 / H);
    mbar_.pulse_specular_on(8);
}

// FUN_005abd10 state 3
void PauseBook::update_map(const BookInput& in, BookModel& m, BookEvents& ev) {
    constexpr float W = 2048, H = 1024;
    bool busy = tween(mtower_) | tween(mcrystal_) | tween(mbar_);
    // the pillars fade with the tower (their own csprites; the host draws them in pixels)
    {
        MenuSprite p; p.diffuse = mpillar_diffuse_; p.specular = mpillar_specular_;
        p.diffuse_t = mtower_.diffuse_t; p.specular_t = mtower_.specular_t; p.col_speed = mtower_.col_speed;
        busy |= tween(p);
        mpillar_diffuse_ = p.diffuse; mpillar_specular_ = p.specular;
    }
    pan_ += 0.1f;
    if (pan_ > 2048.0f) pan_ = 0;
    if (in.cancel && !busy) {
        pstate_ = 1;
        ev.sounds.push_back(1);
        sub_ = 2;
        build_popup();
        build_map();
        return;
    }
    const int h = kMapRecs[mapcur_].link[1 + in.hstep];
    int cur = kMapRecs[std::clamp(h, 0, 21)].link[4 + in.vstep];
    // The mouse [EXPORT FUN_005abd10 st 3]: no wheel -> hover the 22 markers on motion; a wheel tick
    // -> (sign + 22 + idx) % 22 (up = +1) and the warp to the marker's bottom-right, mode 2.
    if (in.wheel == 0) {
        if (in.moved)
            for (int i = 0; i < 22; ++i)
                if (hit(mplate_[i], in)) { cur = i; break; }
    } else if (cur >= 0 && cur <= 21) {
        cur = ((in.wheel > 0 ? 1 : -1) + 22 + cur) % 22;
        ev.warp = true;
        ev.warp_x = int(kMapRecs[cur].x + 240.0f); ev.warp_y = int(kMapRecs[cur].y + 40.0f); ev.warp_mode = 2;
    }
    if (cur < 0) cur = 21;
    if (cur > 21) cur = 0;
    mcrystal_.set_rect(kMapRecs[cur].x2 - 16, kMapRecs[cur].y2 - 64, 80, 80);
    if (cur != mapcur_) {
        mapcur_ = cur;
        ev.sounds.push_back(3);
        mbar_.set_rect(kMapRecs[cur].x, kMapRecs[cur].y, 240, 40);
    }
    if (in.confirm || (in.click && hit(mplate_[mapcur_], in))) {
        if (cur < 24 && m.warp_on && m.warp_on[cur]) {
            m.flags[137] = cur;
            ev.sounds.push_back(0);
            if (m.start_script) m.start_script("@UseWing");
            else ev.script = "@UseWing";
            close_next_ = true;
        } else {
            ev.sounds.push_back(2);
        }
    }
    (void)W; (void)H;
}

void PauseBook::draw_map(DrawList& out, const BookModel& m) const {
    constexpr float W = 2048, H = 1024;
    if ((mtower_.diffuse >> 24) == 0) return;
    DrawCmd p;
    p.kind = DrawCmd::Pillars;
    p.tex = menu(50);
    p.diffuse = mpillar_diffuse_; p.specular = mpillar_specular_;
    out.push_back(p);
    cleria::draw(out, mtower_);
    // FUN_005acf90: the clouds, a 5x5 mesh on MAP.dds (render state 0x4041, WRAP).
    static constexpr float kCX[25] = {99, 492, 831, 1124, 1388, 60, 357, 627, 870, 1088, 36, 282, 510, 717, 906,
                                      24, 234, 432, 615, 783, 16, 195, 366, 532, 680};
    static constexpr float kCY[25] = {-508, -284, -86, 88, 238, -74, 73, 208, 331, 440, 168, 280, 378, 478, 564,
                                      328, 415, 496, 576, 648, 448, 522, 588, 654, 714};
    static constexpr std::uint32_t kCC[25] = {
        0x00ffffff, 0x00ffffff, 0x00a07b64, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0xa0a07b64, 0x80ffffff, 0x44a07b64,
        0x80ffffff, 0x00000000, 0xffa07b64, 0x80ffffff, 0x00ffffff, 0x00ffffff, 0x80a07b64, 0xffa07b64, 0x00ffffff,
        0xffffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff};
    static constexpr float kCU[5] = {658, 705.5f, 753, 800.5f, 848};
    DrawCmd mesh;
    mesh.kind = DrawCmd::Mesh;
    mesh.tex = menu(34);
    for (int i = 0; i < 25; ++i)
        mesh.verts.push_back({kCX[i], kCY[i], (pan_ + kCU[i % 5]) / W, (818.0f + 47.5f * float(i / 5)) / H, kCC[i]});
    mesh.indices.assign(kCloudIdx, kCloudIdx + 96);
    out.push_back(std::move(mesh));
    cleria::draw(out, mplaque_);
    if (pstate_ < 3) return;
    MenuSprite crystal = mcrystal_;
    if (m.character == 3) crystal.set_uv(1968 / W, 80 / H, 1, 160 / H);
    cleria::draw(out, crystal);
    for (int i = 0; i < 22; ++i) {
        MenuSprite pl = mplate_[i];
        const bool on = m.warp_on && m.warp_on[i];
        if (on && i < 13) pl.set_uv(1296 / W, (40.0f * float(i) - 1) / H, 1536 / W, (40.0f * float(i) + 39) / H);
        else if (on) pl.set_uv(1040 / W, (40.0f * float(i - 13) + 159) / H, 1280 / W, (40.0f * float(i - 13) + 199) / H);
        else pl.set_uv(1040 / W, 0, 1280 / W, 40 / H);
        if (i == 0 && on && m.character == 3) pl.set_uv(1040 / W, 40 / H, 1280 / W, 80 / H);
        cleria::draw(out, pl);
    }
    cleria::draw(out, mbar_);
}


// ==== Character Select, book page 8 (TITLE_DRAW.md §5-§6) ====================================

namespace {

constexpr std::uint32_t kCsDim = 0xFF808080;
const char* const kCsName[4] = {"[Yunica]", "[Hugo]", "[Toal]", "[Adol]"};
const char* const kCsBlurb[4] = {"Axe-wielding fighter. Mid speed, mid range.",
                                 "Energy-firing mage. Slow speed, long range.",
                                 "Berserker. Fast, close range. Experts only!",
                                 "Fiery-haired swordsman. Abilities unknown."};
const char* const kCsPortrait[4] = {"CHRSELECT_PORTRAIT1.DDS", "CHRSELECT_PORTRAIT2.DDS", "CHRSELECT_PORTRAIT3.DDS",
                                    "CHRSELECT_PORTRAIT0.DDS"};
const char* const kCsChibi[4] = {"CHRSELECT_CHR1.DDS", "CHRSELECT_CHR2.DDS", "CHRSELECT_CHR3.DDS", "CHRSELECT_CHR0.DDS"};
TexRef cs_tex(const char* n) { return {TexRef::Named, 0, n}; }

}  // namespace

bool PauseBook::cs_locked(const BookModel& m, int bar) const {
    const CharSelectGates& g = m.charsel;
    if (g.mode == 1) {                                       // Time Attack: only cleared characters
        const bool cleared[4] = {g.clear_yunica, g.clear_hugo, g.clear_toal, true};
        return !cleared[std::clamp(bar, 0, 3)];
    }
    return csn_ == 3 && bar == 2 && !(g.clear_yunica || g.clear_hugo || g.open_toal);
}

// FUN_005c2110
void PauseBook::build_charsel(const BookModel& m) {
    const CharSelectGates& g = m.charsel;
    csn_ = (g.nya && g.clear_yunica && g.clear_hugo && g.clear_toal && (g.adol_felghana || g.adol_napishtim) &&
            (g.mode == 1 || g.mode == 2)) ? 4 : 3;
    static constexpr float kY3[3] = {128, 272, 416}, kY4[4] = {80, 208, 336, 464};
    csstate_ = 0;
    cssel_ = 0;
    cspanel_ = make({}, 0, 0, 1024, 768, 0, 0, 1, 1, 0x4040);
    cspanel_.anchor_x = 0;
    cspanel_.set_colour(0, 0);
    for (int i = 0; i < csn_; ++i) {
        const float y = csn_ == 3 ? kY3[i] : kY4[i];
        csbar_[i] = make(cs_tex("CHRSELECT.DDS"), 60, y, 896, 128, 0, 144.0f / 512, 0.875f, 272.0f / 512);
        csbar_[i].set_colour(0x00FFFFFF, 0);
        csface_[i] = make(cs_tex("CHRSELECT.DDS"), 646, y + 16, 256, 100, 256.0f * float(i) / 1024, 288.0f / 512,
                          256.0f * float(i + 1) / 1024, 388.0f / 512);
        csface_[i].set_colour(0x00FFFFFF, 0);
        csport_[i] = make(cs_tex(kCsPortrait[i]), 0, 0, 1024, 1024, 0, 0, 1, 1);
        csport_[i].set_colour(0, 0);
    }
    cshi_.pulse_specular_off();
    cshi_ = make(cs_tex("CHRSELECT.DDS"), 60, csbar_[0].y, 896, 128, 0, 16.0f / 512, 0.875f, 144.0f / 512, 0x24081);
    cshi_.pulse_specular_on(8);
    csport_[0].set_rect(-160, 0, 1024, 1024);
    csport_[0].set_target(0, 0, 16);
    csstrip_ = make(cs_tex("CHRSELECT.DDS"), 0, 688, 512, 80, 0, 432.0f / 512, 0.5f, 1);
    csstrip_.set_colour(0x00FFFFFF, 0);
    csbanner_ = make(cs_tex("CHRSELECT.DDS"), 274, 0, 464, 80, 560.0f / 1024, 432.0f / 512, 1, 1);
    csbanner_.set_colour(0x00FFFFFF, 0);
    cstext_.set_colour(0x00FFFFFF, 0);
    cs_pop_chibi();
    cs_colours(m, true);
}

// FUN_005c1e70: the chibi pops in from a point at its centre.
void PauseBook::cs_pop_chibi() {
    cschibi_ = make(cs_tex(kCsChibi[std::clamp(cssel_, 0, 3)]), 856, 644, 0, 0, 0, 0, 0.25f, 0.5f);
    cschibi_.tx = 792; cschibi_.ty = 580; cschibi_.tw = 128; cschibi_.th = 128;
    cschibi_.pos_speed = 6.4f; cschibi_.size_speed = 12.8f;
    cschibi_.set_colour(0, 0);
    cschibi_.fade_to(kWhite, 0, 2.0f);
}

// FUN_005c3320(lit, set): colour targets at colour speed 2.
void PauseBook::cs_colours(const BookModel& m, bool lit) {
    const std::uint32_t C = lit ? kWhite : kCsDim;
    for (MenuSprite* s : {&csstrip_, &csbanner_, &cschibi_, &cshi_}) s->fade_to(C, 0, 2.0f);
    for (int i = 0; i < csn_; ++i) {
        csport_[i].fade_to(i == cssel_ ? C : 0x00FFFFFFu, 0, 2.0f);
        const bool locked = cs_locked(m, i);
        csbar_[i].fade_to(locked ? 0x80404040u : C, 0, 2.0f);
        csface_[i].fade_to(locked ? 0x40808080u : C, 0, 2.0f);
    }
    cstext_.fade_to(lit ? kWhite : 0x00FFFFFFu, 0, 2.0f);
    cspanel_.fade_to(lit ? 0u : 0x80000000u, 0, 2.0f);
}

// The Type Select / Outfit Select / Difficulty popups (go_wnddialog at (512, 256), no tick).
void PauseBook::cs_popup(const BookModel& m, int which) {
    const CharSelectGates& g = m.charsel;
    std::vector<std::string> items;
    cspop_codes_.clear();
    auto add = [&](const char* t, int code) { items.push_back(t); cspop_codes_.push_back(code); };
    std::string title;
    if (which == 0) {                                        // Type Select
        title = "Type Select";
        if (cssel_ == 3) {
            if (g.adol_felghana) add("Felghana", 0);
            if (g.adol_napishtim) add("Napishtim", 1);
        } else { add("Normal", 0); add("Extra", 1); }
        add("Cancel", -1);
    } else if (which == 1) {                                 // Outfit Select
        title = "Outfit Select";
        if (g.adol_felghana) add("Normal", 0);
        if (g.adol_napishtim) add("School", 1);
        add("Cancel", -1);
    } else {                                                 // Difficulty Level
        title = "Difficulty Level";
        // FUN_005c4950 [EXPORT; the strings: BYTES 0x68d4ec..0x68d4fc]: in a New Game (mode 0) a level the chosen
        // character (1..3, not Adol) has cleared reads "<level> (Clear)".
        static const char* const kLevel[5][2] = {{"Very Easy", "Very Easy (Clear)"}, {"Easy", "Easy (Clear)"},
            {"Normal", "Normal (Clear)"}, {"Hard", "Hard (Clear)"}, {"Nightmare", "Nightmare (Clear)"}};
        auto level = [&](int t) {
            add(kLevel[t][g.mode == 0 && cssel_ >= 0 && cssel_ < 3 && g.tier[std::size_t(t)][std::size_t(cssel_)]], 10 + t);
        };
        if (g.mode == 0 && g.nya) level(0);
        level(1);
        level(2);
        level(3);
        if (g.nya) level(4);
        add("Cancel", -1);
    }
    const std::vector<std::string> labels(items.size());
    cspop_.menu(512, 256, title, items, labels, m.metrics, false, false);
    cspop_rows_ = items;
    cspop_.set_cancel(int(items.size()));
    if (which == 2) for (std::size_t k = 0; k < cspop_codes_.size(); ++k) if (cspop_codes_[k] == 12) cspop_.set_cursor(int(k) + 1);
    cspop_.arm();
    cspop_kind_ = which;
    cspop_done_ = false;
    cshi_.pulse_specular_off();
    cs_colours(m, false);
}

// FUN_005c3790 (+ the dispatcher's difficulty step)
void PauseBook::update_charsel(const BookInput& in0, BookModel& m, BookEvents& ev) {
    const BookInput none{};
    const BookInput& in = sub_ == 0x17 ? in0 : none;
    for (MenuSprite* s : {&cspanel_, &cshi_, &cschibi_, &csstrip_, &csbanner_, &cstext_}) tween(*s);
    for (int i = 0; i < csn_; ++i) { tween(csbar_[i]); tween(csface_[i]); tween(csport_[i]); }
    cschibi_c_ = (cschibi_c_ + 15) % 800;                    // 8 cells of a 4x2 sheet
    const int f = cschibi_c_ / 100, col = f & 3, row = f >> 2;
    cschibi_.set_uv(128.0f * float(col) / 512, 128.0f * float(row) / 256, (128.0f * float(col) + 128) / 512,
                    (128.0f * float(row) + 128) / 256);
    // The popup.
    if (cspop_.alive()) {
        WndInput wi;
        wi.confirm_pressed = in0.confirm; wi.cancel_pressed = in0.cancel; wi.vstep = in0.vstep;
        wi.confirm_held = in0.confirm; wi.cancel_held = in0.cancel;
        wi.click_pressed = in0.click; wi.mouse_moved = in0.moved; wi.mouse_mode = in0.mouse_mode;
        wi.mx = in0.mx; wi.my = in0.my; wi.display = in0.display;
        cspop_.update(wi, ev.sounds);
        if (cspop_.accepted() && !cspop_done_) {
            cspop_done_ = true;
            const int k = cspop_.chosen_index() - 1;
            const int code = k >= 0 && k < int(cspop_codes_.size()) ? cspop_codes_[std::size_t(k)] : -1;
            if (cspop_kind_ == 2) {
                if (code < 0) { cs_cancelled_ = true; }
                else {
                    cs_result_.difficulty = code == 13 ? 1 : code == 14 ? 2 : 0;
                    cs_result_.easy = code == 10 || code == 11;
                    cs_result_.very_easy = code == 10;
                    cs_cancelled_ = false;
                }
            } else if (code < 0) {
                csstate_ = 1; cshi_.pulse_specular_on(8); cs_colours(m, true);
            } else {
                if (cspop_kind_ == 0) cs_result_.type = code; else cs_result_.outfit = code;
                if (cspop_kind_ == 0 && cssel_ == 0 && m.charsel.yunica_outfit) cs_next_ = 1;
                else cs_next_ = 2;
            }
        }
    } else if (cspop_done_) {
        cspop_done_ = false;
        if (cspop_kind_ == 2) {                              // the window is gone: 0x75aed4 ? 0x17 : -1
            if (cs_cancelled_) { sub_ = 0x17; csstate_ = 1; cshi_.pulse_specular_on(8); cs_colours(m, true); }
            else { ev.new_game = true; ev.new_game_info = cs_result_; sub_ = -1; }
        } else if (cs_next_ >= 0) {
            const int n = cs_next_;
            cs_next_ = -1;
            if (n == 2) sub_ = 0x18;
            cs_popup(m, n);
        }
    }
    switch (csstate_) {
        case 0:
            if (sub_ == 0x17) { csstate_ = 1; cs_colours(m, true); }
            break;
        case 1: {
            if (in.cancel) { sub_ = -1; ev.sounds.push_back(1); break; }
            const CharSelectGates& g = m.charsel;
            const int mm = (csn_ == 3 && !(g.clear_yunica || g.clear_hugo || g.open_toal)) ? 2 : csn_;
            // The mouse [EXPORT FUN_005c3790]: no wheel -> hover the bars on motion; a wheel tick ->
            // (n + cur - sign) % n (up = the upper bar). [H] the bars past CleriaCore's row limit `mm`
            // (Toal still locked) do not take the hover.
            int s = cssel_;
            bool hovered = false;
            if (in.wheel == 0) {
                if (in.moved)
                    for (int i = 0; i < mm; ++i)
                        if (hit(csbar_[i], in)) { s = i; hovered = true; break; }
            } else {
                s = (mm + s - (in.wheel > 0 ? 1 : -1)) % mm;
            }
            s = (s + in.vstep + mm) % mm;
            if (s != cssel_) {
                cssel_ = s;
                ev.sounds.push_back(3);
                cshi_.set_rect(60, csbar_[s].y, 896, 128);
                csport_[s].set_rect(-160, 0, 1024, 1024);
                csport_[s].set_target(0, 0, 16);
                cs_pop_chibi();
                cs_colours(m, true);
                // A change not from the hover (keys, wheel) warps the pointer to the bar's bottom-right
                // inset by 32, mode 2, with NO mouse-mode gate [BYTES 0x5c3a6d..0x5c3e80].
                if (!hovered) {
                    ev.warp = true; ev.warp_mode = 2;
                    ev.warp_x = int(csbar_[s].x + 896.0f - 32.0f); ev.warp_y = int(csbar_[s].y + 128.0f - 32.0f);
                }
            }
            if (in.confirm || (in.click && hit(csbar_[cssel_], in))) {
                ev.sounds.push_back(0);
                static constexpr int kChar[4] = {1, 2, 3, 0};
                cs_result_ = {};
                cs_result_.character = kChar[cssel_];
                const bool type = (cssel_ == 0 && g.extra_yunica) || (cssel_ == 1 && g.extra_hugo) ||
                                  (cssel_ == 2 && g.extra_toal) || (cssel_ == 3 && (g.adol_felghana || g.adol_napishtim));
                csstate_ = 2;
                if (type) cs_popup(m, 0);
                else { sub_ = 0x18; cs_popup(m, 2); }
            }
            break;
        }
        default: break;
    }
}

// FUN_005a5d50 page 8
void PauseBook::draw_charsel(DrawList& out) const {
    cleria::draw(out, cspanel_);
    for (int i = 0; i < csn_; ++i) { cleria::draw(out, csbar_[i]); cleria::draw(out, csface_[i]); }
    cleria::draw(out, cshi_);
    for (int i = 0; i < csn_; ++i) cleria::draw(out, csport_[i]);
    cleria::draw(out, cschibi_);
    cleria::draw(out, csstrip_);
    const std::uint32_t ta = cstext_.diffuse >> 24;
    if (ta != 0) {
        const std::uint32_t col = (ta << 24) | 0xC0C0C0;
        out.push_back(text_cmd(DrawCmd::TextGdi, kCsName[std::clamp(cssel_, 0, 3)], 48, 704, col, 4));
        out.push_back(text_cmd(DrawCmd::TextGdi, kCsBlurb[std::clamp(cssel_, 0, 3)], 208, 704, col, 4));
    }
    cleria::draw(out, csbanner_);
    cspop_.draw(out);
}

}  // namespace cleria
