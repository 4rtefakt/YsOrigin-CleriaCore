// The Bonus Shop (build/mode_re/BONUS_MODES.md §4): GoWndShop2 in its context 0, opened from the title's
// bonus list (0x575f20 -> FUN_00443240(0) -> FUN_005c5800). A backdrop (Back01, Feena, the ShopPillar strips),
// two text lines, and go_wnddialog windows: the list built by FUN_005c9d20, and one popup per row callback
// (the buy handlers 0x5c7b50.. / FUN_005c7780). Twelve items paid in Arena SP (0x74c128, record.ar2); a
// purchase sets its dword in the clear data (0x74abf8..0x74ac24, record.ta2).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/menusprite.hpp"
#include "cleria/records.hpp"
#include "cleria/wnddialog.hpp"

namespace cleria {

// The price of a shop item [BYTES: the `cmp [0x74c128], price` of each buy handler 0x5c7b82..0x5c9a72 and the
// `sub [0x74c128], price` of each apply stub 0x5c7891..0x5c7b01 — the two agree for all twelve].
std::uint32_t shop_price(records::ClearData::Shop item);
// The item's name, as the confirm window's caption has it [EXPORT strings 0x6b4350..0x6b4e38].
const char* shop_name(records::ClearData::Shop item);

// One row of the list, as FUN_005c9d20 builds it [EXPORT 005c9d20.c:157-614].
struct ShopRow {
    enum Kind { Buy, Unlocked, Cancel } kind = Cancel;   // the buy handler / FUN_005c7780 / 0x5c9d10
    records::ClearData::Shop item{};                      // Buy: what it sells
    std::string label;
};
// The rows for this clear data: the arena slot (the cheapest unbought stage; stage 7 only with Nightmare x 3, or
// Hard, Normal and Easy x 3 each), Adol (Felghana), the three Extras once Felghana is bought, Adol (Napishtim)
// once the three are, the School Outfit under its gate, Cancel.
std::vector<ShopRow> shop_rows(const records::ClearData& d);

struct ShopEvents {
    std::vector<int> sounds;
    bool bought = false;                  // an apply stub ran: FUN_00438de0 (record.ar2) is the host's
    records::ClearData::Shop item{};
    bool closed = false;                  // sub-screen id -1: back to the title
};

class BonusShop {
  public:
    void open();                                          // FUN_005c5800(0)
    bool is_open() const { return open_; }
    // FUN_005c5b20 -> FUN_005c9d20, and the windows' own updates. A purchase subtracts the price from `arena_sp`
    // and sets the item's dword in `d`.
    void update(const WndInput& in, records::ClearData& d, std::uint32_t& arena_sp, const RichMetrics& m, ShopEvents& ev);
    void draw(DrawList& out) const;                       // FUN_005c5fc0 (the bonus shop's branch), then the windows
    const std::vector<ShopRow>& rows() const { return rows_; }

  private:
    bool open_ = false;
    int state_ = 0;                       // 0x7447a8: 0 build the list, 1 the list / a popup is up
    int signal_ = 0;                      // 0x74e260: 1 rebuild, 2 close
    MenuSprite text_;                     // 0x74559c / 0x7456c8: the two text lines' shared fade
    std::string sp_text_;                 // "Current [Arena SP]: %d" as last built (0x745774)
    WndDialog list_, pop_;
    std::vector<ShopRow> rows_;
    bool list_ran_ = false, pop_ran_ = false;
    int pop_kind_ = 0;                    // 1 confirm (item pop_item_), 2 an OK box
    records::ClearData::Shop pop_item_{};
};

}  // namespace cleria
