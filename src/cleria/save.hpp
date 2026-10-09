#pragma once
// Ys Origin save record (yso_NN.bin) — build/menu_re/SAVELOAD.md §4 [C-DIS], codec
// cross-validated against 26 real saves (tools/save_decode.py agrees byte-for-byte).
//
// A save is one 0x1FC8-byte record: an image of the live game-state block at VA
// 0x0076a2d0, with a stamped metadata header (+0x00..+0xd0) and two spans scrambled
// by the rolling byte cipher   s = s*0x3d09; byte += (s>>16)&0xff   (write adds,
// load subtracts): key A @+0x10 covers +0x30..+0xd0, key B @+0xc0 (inside the key-A
// span) covers +0xd0..end. The "magic ptr" &LAB_004f7359 is little-endian "YsO\0".
//
// Checksums (FUN_005ee870 = zlib CRC-32, verified 51/51 real saves): +0x0c covers the
// scrambled +0x30..+0x60, +0xc4 the scrambled payload. encode() stamps both, so the
// real exe accepts CleriaCore-written files; decode() does not verify them (the engine
// zeroes both after verifying on load).
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cleria {

struct SaveRecord {
    static constexpr std::size_t kSize = 0x1FC8;
    static constexpr std::uint32_t kBase = 0x0076A2D0;   // engine VA of byte 0
    static constexpr std::uint32_t kMagic = 0x004F7359;  // "YsO\0" (a code address used as tag)

    std::array<std::uint8_t, kSize> b{};

    // Typed access by ENGINE VIRTUAL ADDRESS, so every global RE'd in build/*_re
    // reads out of a record verbatim (mirrors tools/save_decode.py u32/f32).
    std::int32_t i32(std::uint32_t va) const;
    float f32(std::uint32_t va) const;
    std::string str(std::uint32_t va, std::size_t max) const;
    void set_i32(std::uint32_t va, std::int32_t v);
    void set_f32(std::uint32_t va, float v);
    void set_str(std::uint32_t va, const std::string& s, std::size_t max);

    // Raw record-offset access (the +0x00..+0xd0 header has no block VA).
    std::uint32_t hdr_u32(std::size_t off) const;
    void set_hdr_u32(std::size_t off, std::uint32_t v);
    std::string hdr_str(std::size_t off, std::size_t max) const;
    void set_hdr_str(std::size_t off, const std::string& s, std::size_t max);

    // Descramble + validate (magic/version/size). nullopt = not a YsO save.
    static std::optional<SaveRecord> decode(std::span<const std::uint8_t> raw);
    // Scramble with the given keys (stamped to +0x10/+0xc0). Any nonzero keys work;
    // the engine rerolls them every save. Checksums are left 0 (see header note).
    std::vector<std::uint8_t> encode(std::uint32_t key_a, std::uint32_t key_b) const;
};

// The logical state CleriaCore round-trips through a record. Field VAs:
//   g_flags 0x76b91c (0x200 ints, SAVELOAD/NEWGAME_FLAGS), itemArr 0x76a654 (54,
//   EQUIP.md), maxHP 0x76a72c / curHP 0x76a730 / EXP 0x76a748 / SP 0x76a75c /
//   level 0x76a760 (HUD_RE stat block), scene-name buffer 0x76a3b4 (DOORRETURN),
//   continue snapshot x/z/yaw 0x76c13c/0x76c144/0x76c148 (EVENTVM_HANDLERS_2 §…,
//   ARRIVAL_AUDIT.md: y 0x76c140 is saved too; yaw = the heading +0x314 in radians, engine basis).
struct SaveState {
    std::int32_t flags[0x200] = {0};
    std::int32_t item_arr[54] = {0};
    float hp_max = 0, hp_cur = 0, exp = 0;
    float mp_max = 0, mp_cur = 0;         // the skill gauge 0x76a734 / 0x76a738 (0 in older CleriaCore saves)
    std::int32_t level = 1, sp = 0;
    std::string scene;                    // room to re-enter on load (e.g. "s_1000")
    float pos_x = 0, pos_z = 0, yaw = 0;  // continue-snapshot position; yaw in radians (engine basis, 0 = +X)
    // The snapshot's y, 0x76c140: FUN_00443d70 stores origin.y - (+0x414 + +0x324) = the feet,
    // and the loader FUN_0042f5e0 hands (0x76c13c, 0x76c140, 0x76c144) to the "load" warp
    // [BYTES 0x42f95b..0x42f9cb]. Retail saves carry it (2.0, 4.0, 21.25, -4.0 ...).
    float pos_y = 0;
    // The inventory fields outside g_flags (build/progress_re/INVENTORY.md §0): accessory /
    // held item / magic 0x76a628/62c/630, weapon tier 0x76a634 + 8ch, the Panacea HP bonus
    // 0x76b8fc, the drop buffs 0x76b904/908, luck 0x76b90c, the buff timers 0x76c19c/1a0.
    std::int32_t accessory = -1, held = -1, magic = -1;
    std::int32_t tier[4] = {0, 0, 0, 0};
    float hp_bonus = 0, atk_buff = 0, def_buff = 0, luck_base = 0;
    std::int32_t timer_atk = 0, timer_def = 0;
    float mp_buff = 0;                    // 0x76b900, the MP Drop buff; its timer 0x76c1a4
    std::int32_t timer_mp = 0;
    // The manual pages enabled 0x76b8d8[13] (op 0x92) and the warp points 0x76c11c[24]
    // (op 0xa7) (build/menu_re/PAUSEBOOK_DRAW_TOPIC_MAP.md §5.1, §6.4).
    std::uint8_t manual_on[13] = {};
    std::uint8_t warp_on[24] = {};
    // Oxygen 0x76c1b8 (f32, max 6000) and its gauge alpha 0x76c1bc (i32): both sit inside the
    // 0x1fc8 block (+0x1ee8 / +0x1eec), so every save, the continue snapshot 0x767f50 and the
    // game over's Retry carry them (FUN_0042e9a0 / FUN_00443d70 copy the block whole). 51 of 51
    // retail saves: 6000.0 / 0. No load or scene entry writes either (globals.tsv: only the item
    // FUN_00443390 and the player tick FUN_00465170).
    float o2 = 0.0f;
    std::int32_t o2_alpha = 0;
    // The status ailments 0x76c15c..0x76c16c = g_flags 528..532 (the mask, then the poison / heavy / confuse /
    // curse timers): past the 0x200 flags above but inside the block (+0x1e8c..+0x1e9c), so a save, the continue
    // snapshot and the Retry carry them (build/mech_re/STATUS_AILMENTS.md §7). 0 in older CleriaCore saves.
    std::int32_t ail[5] = {0, 0, 0, 0, 0};
    // The Boost gauge 0x76c1b4 (f32, full 30000; block +0x1ee4): no load or scene entry writes it (globals.tsv:
    // the item, the event grant, the player tick, the add FUN_0046cc10, the drain FUN_00462c40, 0x54a890 and the
    // stage step at 0x428371), so a load and the Retry bring back the saved value. 0 in older CleriaCore saves.
    float boost = 0.0f;

    int character() const { return flags[150]; }
    int difficulty() const { return flags[144]; }  // DAT_0076bb5c; header +0x50 = this+1
};

// Build a record (payload fields + header stamps). `area_line` is the +0x90 31-char
// display line the slot list shows; date "MM/DD/YYYY", time "HH:MM:SS".
SaveRecord to_record(const SaveState& s, const std::string& area_line,
                     const std::string& date, const std::string& time);
SaveState from_record(const SaveRecord& r);

// Header-only preview for a slot list (no game-state side effects).
struct SaveMeta {
    int character = 0;    // +0x68
    int difficulty = 0;   // +0x50 - 1
    std::string area, date, time;
};
SaveMeta meta_of(const SaveRecord& r);

}  // namespace cleria
