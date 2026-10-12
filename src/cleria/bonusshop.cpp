// The Bonus Shop: build/mode_re/BONUS_MODES.md §4. See bonusshop.hpp.
#include "cleria/bonusshop.hpp"

#include <cstdio>

namespace cleria {

using records::ClearData;

namespace {

// Indexed by ClearData::Shop. Prices [BYTES 0x5c7b82.. / 0x5c7891..]; labels and names [EXPORT strings.tsv
// 0x6b4350..0x6b69a8, the English branch of FUN_005c9d20 and of each buy handler].
struct Item { std::uint32_t price; const char* name; const char* buy; const char* unlocked; };
constexpr Item kItems[ClearData::kShopCount] = {
    {500000, "Adol (Felghana Version)", "[500,000] Adol (Felghana Version)", "[Unlocked] Adol (Felghana Version)"},
    {1000000, "Adol (Napishtim Version)", "[1,000,000] Adol (Napishtim Version)", "[Unlocked] Adol (Napishtim Version)"},
    {750000, "Yunica (Extra Version)", "[750,000] Yunica (Extra Version)", "[Unlocked] Yunica (Extra Version)"},
    {750000, "Hugo (Extra Version)", "[750,000] Hugo (Extra Version)", "[Unlocked] Hugo (Extra Version)"},
    {750000, "Toal (Extra Version)", "[750,000] Toal (Extra Version)", "[Unlocked] Toal (Extra Version)"},
    {5000, "Arena Stage 2", "[5,000] Arena Stage 2", "[Unlocked] Arena Stages"},
    {15000, "Arena Stage 3", "[15,000] Arena Stage 3", "[Unlocked] Arena Stages"},
    {30000, "Arena Stage 4", "[30,000] Arena Stage 4", "[Unlocked] Arena Stages"},
    {100000, "Arena Stage 5", "[100,000] Arena Stage 5", "[Unlocked] Arena Stages"},
    {250000, "Arena Stage 6", "[250,000] Arena Stage 6", "[Unlocked] Arena Stages"},
    {2000000, "Arena Stage 7", "[2,000,000] Arena Stage 7", "[Unlocked] Arena Stages"},
    {3000000, "Yunica School Outfit", "[3,000,000] Yunica School Outfit", "[Unlocked] Yunica School Outfit"},
};

DrawCmd quad(const char* name, float x, float y, float w, float h, float v1, std::uint32_t flags) {
    DrawCmd c;
    c.kind = DrawCmd::Quad;
    c.tex = {TexRef::Named, 0, name};
    c.x = x; c.y = y; c.w = w; c.h = h;
    c.u0 = 0; c.v0 = 0; c.u1 = 1; c.v1 = v1;
    c.flags = flags;
    c.anchor_x = 2; c.anchor_y = 0;
    return c;
}

DrawCmd text(const std::string& s, float x, float y, std::uint32_t colour) {   // FUN_005a4300(s, 0xffc0c0c0, 4)
    DrawCmd c;
    c.kind = DrawCmd::TextGdi; c.text = s; c.x = x; c.y = y; c.diffuse = colour; c.line_extra = 4;
    c.anchor_x = 2; c.anchor_y = 0;
    return c;
}

}  // namespace

std::uint32_t shop_price(ClearData::Shop item) { return kItems[item].price; }
const char* shop_name(ClearData::Shop item) { return kItems[item].name; }

std::vector<ShopRow> shop_rows(const ClearData& d) {
    std::vector<ShopRow> rows;
    auto row = [&](ClearData::Shop item, bool buy) {
        rows.push_back({buy ? ShopRow::Buy : ShopRow::Unlocked, item, buy ? kItems[item].buy : kItems[item].unlocked});
    };
    auto all = [&](int tier) { return d.tier[std::size_t(tier)][0] && d.tier[std::size_t(tier)][1] && d.tier[std::size_t(tier)][2]; };
    // The arena slot: the first unbought of stages 2..6; then stage 7, offered only with Nightmare x 3 or with
    // Hard, Normal and Easy x 3 each (else, and when all are bought, "[Unlocked] Arena Stages").
    int stage = ClearData::kArena2;
    while (stage < ClearData::kArena7 && d.shop[std::size_t(stage)]) ++stage;
    if (stage < ClearData::kArena7) row(ClearData::Shop(stage), true);
    else row(ClearData::kArena7, (all(4) || (all(3) && all(2) && all(1))) && !d.shop[ClearData::kArena7]);
    row(ClearData::kAdolFelghana, !d.shop[ClearData::kAdolFelghana]);
    if (d.shop[ClearData::kAdolFelghana])
        for (ClearData::Shop e : {ClearData::kYunicaExtra, ClearData::kHugoExtra, ClearData::kToalExtra}) row(e, !d.shop[e]);
    if (d.shop[ClearData::kYunicaExtra] && d.shop[ClearData::kHugoExtra] && d.shop[ClearData::kToalExtra])
        row(ClearData::kAdolNapishtim, !d.shop[ClearData::kAdolNapishtim]);
    // The School Outfit: Napishtim and stage 7 bought, and all fifteen tiers cleared or the three clear bytes
    // holding the Extra values 0xD5 / 0x8E / 0x89.
    if (d.shop[ClearData::kAdolNapishtim] && d.shop[ClearData::kArena7] &&
        ((all(0) && all(1) && all(2) && all(3) && all(4)) ||
         (d.clear[0] == ClearData::kExtra[0] && d.clear[1] == ClearData::kExtra[1] && d.clear[2] == ClearData::kExtra[2])))
        row(ClearData::kYunicaOutfit, !d.shop[ClearData::kYunicaOutfit]);
    rows.push_back({ShopRow::Cancel, {}, "Cancel"});
    return rows;
}

void BonusShop::open() {
    *this = BonusShop{};
    open_ = true;
    text_.set_colour(0x00FFFFFF, 0);                         // 0x7455c4 / 0x7456f0 [EXPORT 005c6f40.c:302-341]
}

void BonusShop::update(const WndInput& in, ClearData& d, std::uint32_t& arena_sp, const RichMetrics& m, ShopEvents& ev) {
    if (!open_) return;
    tween(text_);
    // A window's callback (FUN_00578960 at its accept): a row's handler opens a popup at (512, 256) with the
    // fast caption (+0x1fd4); a popup's sets 0x74e260.
    auto ok_box = [&](const char* caption) {
        pop_.menu(512, 256, caption, {"OK"}, {std::string()}, m, false, false);
        pop_.set_fast(); pop_.arm();
        pop_kind_ = 2; pop_ran_ = false;
    };
    list_.update(in, ev.sounds);
    if (list_.accepted() && !list_ran_) {
        list_ran_ = true;
        const ShopRow& r = rows_[std::size_t(list_.chosen_index() - 1)];
        if (r.kind == ShopRow::Cancel) signal_ = 2;                               // 0x5c9d10
        else if (r.kind == ShopRow::Unlocked) ok_box("This item has already been unlocked.");   // FUN_005c7780
        else if (arena_sp < shop_price(r.item)) ok_box("Insufficient SP.");       // [BYTES 0x5c7b82, 0x5c7d39..]
        else {                                                                    // [BYTES 0x5c7ba2..0x5c7d34]
            pop_.menu(512, 256, shop_name(r.item), {"OK to unlock this bonus?", "Cancel"}, {std::string(), std::string()}, m,
                      false, false);
            pop_.set_cancel(2); pop_.set_fast(); pop_.arm();
            pop_kind_ = 1; pop_item_ = r.item; pop_ran_ = false;
        }
    }
    pop_.update(in, ev.sounds);
    if (pop_.accepted() && !pop_ran_) {
        pop_ran_ = true;
        if (pop_kind_ == 1 && pop_.chosen_index() == 1) {                         // the apply stub [BYTES 0x5c7890..]
            arena_sp -= shop_price(pop_item_);
            d.shop[pop_item_] = 1;
            ev.bought = true; ev.item = pop_item_;
        }
        signal_ = 1;                                                              // 0x5c7770 and the stubs
    }
    // FUN_005c9d20's states [EXPORT 005c9d20.c:101-116, 692-702].
    if (state_ == 0) {
        text_.fade_to(0xFFFFFFFF, 0, 2.0f);                                       // FUN_005caab0(1, 1)
        state_ = 1; signal_ = 0;
        rows_ = shop_rows(d);
        std::vector<std::string> items;
        for (const ShopRow& r : rows_) items.push_back(r.label);
        list_.menu(672, 192, "Please select a bonus to unlock:", items, std::vector<std::string>(items.size()), m, false, false);
        list_.set_cancel(int(items.size()));
        list_.arm();
        list_ran_ = false;
        char buf[64];
        std::snprintf(buf, sizeof buf, "Current [Arena SP]: %u", arena_sp);
        sp_text_ = buf;
    } else if (signal_ == 1) {
        state_ = 0; signal_ = 0;
    } else if (signal_ == 2) {
        open_ = false; ev.closed = true;
        ev.sounds.push_back(1);                                                   // FUN_0043a040(1, 0x400) [BYTES 0x5ca9e4]
        signal_ = 0;
    }
}

void BonusShop::draw(DrawList& out) const {
    if (!open_) return;
    out.push_back(quad("BACK01.DDS", 0, 0, 1024, 768, 0.75f, 0x20001));            // 0x744190 [EXPORT 005c5800.c]
    DrawCmd p;                                                                    // 0x7442f4 / 0x7443a0: ShopPillar16-9 / 16-10
    p.kind = DrawCmd::Pillars;
    p.tex = {TexRef::Menu, 52, nullptr};
    out.push_back(p);
    out.push_back(quad("T_400A.DDS", -288, -116.8f, 819.2f, 819.2f, 1, 0x24041));  // 0x744248: Feena [EXPORT 005c62d0.c]
    const std::uint32_t col = (text_.diffuse & 0xFF000000u) | 0xC0C0C0;
    if (col >> 24) {
        out.push_back(text("You can use SP earned in Arena Mode to purchase special bonuses.", 48, 704, col));   // 0x74559c
        out.push_back(text(sp_text_, 608, 656, col));                             // 0x7456c8
    }
    list_.draw(out);
    pop_.draw(out);
}

}  // namespace cleria
