// The `record.*` files (build/mode_re/CLEAR_DATA.md): one container for seven files, and the clear data
// `record.ta2` — which characters cleared the game (and with the Extra version), the fifteen cleared-tier
// bytes, the Bonus Shop's purchase dwords. Pure codec + the clear's own rules (EventXX(4), EventXX(13)); the
// host (apps/cleria-view) owns the file and its save points.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cleria::records {

// ---- the container (CLEAR_DATA.md §1; writer FUN_00436fa0 [BYTES 0x4373cb..0x437466], reader FUN_00436790) ----
//   +0x00 "YsO\0" (0x004F7359)   +0x04 0x0D   +0x08 / +0x0C two noise words   +0x10 8B 10 CF 78 18 56, zeros to +0x20
//   +0x20 key   +0x24 crc32 of the PLAIN payload   +0x28 payload
//   cipher over +0x24..end: key = key * 0x3D09; byte += key >> 16 (write), -= (read, FUN_005e5dc0).
inline constexpr std::size_t kHeader = 0x28;
// The file for `payload` under `key`. `noise_a` / `noise_b` are the words at +0x08 / +0x0C (retail: two draws of
// the engine's xorshift, masked 0xFFF / 0xFFF0FFF0; nothing reads them back [EXPORT 00436790.c]).
std::vector<std::uint8_t> encode(std::span<const std::uint8_t> payload, std::uint32_t key, std::uint32_t noise_a = 0,
                                 std::uint32_t noise_b = 0);
// The plain payload of a file of exactly kHeader + payload_size bytes whose CRC matches; nothing otherwise (the
// reader then keeps its defaults [EXPORT 00436790.c:153-165]). [H] the header words +0x00..+0x1F are not tested
// by the reader (CLEAR_DATA.md U1): they are not tested here either.
std::optional<std::vector<std::uint8_t>> decode(std::span<const std::uint8_t> file, std::size_t payload_size);
// A key for a file CleriaCore writes: non-zero, with 11..21 set bits (FUN_005ee6d0's window, CLEAR_DATA.md U2
// [H]) — from `seed`, never from the game's own RNG stream (retail draws that one; [approx]).
std::uint32_t make_key(std::uint32_t seed);

// ---- record.ta2: the clear data (CLEAR_DATA.md §3.1) ----
struct ClearData {
    static constexpr std::size_t kPayload = 0x98;                 // 0x26 dwords; the file is 0xC0 bytes
    // +0x00 / +0x04 / +0x08 (u8 0x74ab7b / 7a / 79): Yunica, Hugo, Toal — 0 no, 1 cleared, kExtra[c] = cleared
    // with the Extra version.
    std::array<std::uint8_t, 3> clear{};
    static constexpr std::uint8_t kExtra[3] = {0xD5, 0x8E, 0x89};
    // +0x0C..+0x44: cleared on Very Easy / Easy / Normal / Hard / Nightmare (rows) by Yunica / Hugo / Toal
    // (columns) — the working bytes 0x74ab88.. / 8c.. / 94.. / 98.. / 9c.. (NOT evenly spaced in memory).
    std::array<std::array<std::uint8_t, 3>, 5> tier{};
    std::uint8_t count = 0;                                        // +0x48 (0x74c107): no reader but the save
    std::uint8_t nya = 0;                                          // +0x4C (0x74c106): replaced at the title (§6)
    std::uint8_t c105 = 0;                                         // +0x50 (0x74c105): no reader but the save
    std::uint32_t unused54 = 0;                                    // +0x54: no xref
    // +0x58..+0x84: bought in the Bonus Shop — Adol (Felghana), Adol (Napishtim), Yunica / Hugo / Toal Extra,
    // Arena Stage 2..7, Yunica's School Outfit.
    enum Shop { kAdolFelghana, kAdolNapishtim, kYunicaExtra, kHugoExtra, kToalExtra, kArena2, kArena3, kArena4, kArena5,
                kArena6, kArena7, kYunicaOutfit, kShopCount };
    std::array<std::uint32_t, kShopCount> shop{};
    std::array<std::uint32_t, 4> unused88{};                       // +0x88..+0x94: no xref

    // The reader keeps each of the first 0x54 bytes' dwords as its low byte; the writer zero-extends it back
    // [EXPORT 00436790.c:167-187, 00436fa0.c:56-76].
    static ClearData from_payload(std::span<const std::uint8_t> p);   // p.size() == kPayload
    std::array<std::uint8_t, kPayload> payload() const;

    bool cleared(int col) const { return clear[std::size_t(col)] != 0; }   // col 0 Yunica, 1 Hugo, 2 Toal
    bool any_clear() const { return clear[0] || clear[1] || clear[2]; }
    bool all_clear() const { return clear[0] && clear[1] && clear[2]; }
    int tier_count() const;                                        // the non-zero bytes among the fifteen
};

// The file's whole image / its content, as FUN_00436fa0 / FUN_00436790 minus the platform's achievement repair
// (§3.4: a no-op without Steam / GOG).
std::vector<std::uint8_t> encode_clear(const ClearData& d, std::uint32_t key);
std::optional<ClearData> decode_clear(std::span<const std::uint8_t> file);
std::optional<ClearData> load_clear(const std::filesystem::path& file);
bool save_clear(const std::filesystem::path& file, const ClearData& d, std::uint32_t key);

// EventXX(4)'s writes to the clear data (FUN_0058f210 case 4 [BYTES 0x58f350..0x58f5e9], CLEAR_DATA.md §3.2):
// `character` = flag 150 (1 Yunica, 2 Hugo, 3 Toal; anything else writes nothing), `extra` = FUN_00583960 (the
// Type Select answer), `easy` = flag 143 (0x76bb58), `hard` = flag 144 (0x76bb5c: 0 Normal, 1 Hard, 2
// Nightmare), `very_easy` = flag 203 (0x76bc48). The clear byte becomes max(old, 1), or the Extra value; the
// tier byte of the run's difficulty becomes 1 (none for flag 144 > 2 off the easy side).
void apply_clear(ClearData& d, int character, bool extra, bool easy, int hard, bool very_easy);

// EventXX(13)'s amount (FUN_0058f210 case 13 [BYTES 0x58f8a5..0x58f94a], BONUS_MODES.md §1): 10,000 SP per
// cleared (difficulty, character) cell, at most 150,000; a hundredth of it on Nightmare (flag 144 == 2).
int sp_bonus(const ClearData& d, int hard);

}  // namespace cleria::records
