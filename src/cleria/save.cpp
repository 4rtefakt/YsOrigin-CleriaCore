#include "cleria/save.hpp"

#include <cstring>

namespace cleria {
namespace {

// The rolling byte cipher (SAVELOAD.md §4.1): per-span seed s; each byte steps
// s = s*0x3d09 and applies (s>>16)&0xff — ADD on write, SUBTRACT on load
// (FUN_005e5dc0 is the load direction; validated by tools/save_decode.py).
void scramble(std::uint8_t* p, std::size_t off, std::size_t len, std::uint32_t key, bool write) {
    std::uint32_t s = key;
    for (std::size_t i = off; i < off + len; ++i) {
        s *= 0x3D09u;
        const std::uint8_t k = std::uint8_t(s >> 16);
        p[i] = std::uint8_t(write ? p[i] + k : p[i] - k);
    }
}

std::size_t va_off(std::uint32_t va) { return std::size_t(va - SaveRecord::kBase); }

// FUN_005ee870(ecx=ptr, edx=len): table-driven reflected CRC-32, poly 0xEDB88320,
// init/xorout 0xFFFFFFFF — i.e. zlib crc32 [C-DIS, 0x5ee870..0x5ee951]. Verified on
// 51/51 real saves (build/audit/menus.md S3).
std::uint32_t crc32(const std::uint8_t* p, std::size_t len) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

}  // namespace

std::int32_t SaveRecord::i32(std::uint32_t va) const {
    std::int32_t v; std::memcpy(&v, b.data() + va_off(va), 4); return v;
}
float SaveRecord::f32(std::uint32_t va) const {
    float v; std::memcpy(&v, b.data() + va_off(va), 4); return v;
}
std::string SaveRecord::str(std::uint32_t va, std::size_t max) const {
    return hdr_str(va_off(va), max);
}
void SaveRecord::set_i32(std::uint32_t va, std::int32_t v) {
    std::memcpy(b.data() + va_off(va), &v, 4);
}
void SaveRecord::set_f32(std::uint32_t va, float v) {
    std::memcpy(b.data() + va_off(va), &v, 4);
}
void SaveRecord::set_str(std::uint32_t va, const std::string& s, std::size_t max) {
    set_hdr_str(va_off(va), s, max);
}
std::uint32_t SaveRecord::hdr_u32(std::size_t off) const {
    std::uint32_t v; std::memcpy(&v, b.data() + off, 4); return v;
}
void SaveRecord::set_hdr_u32(std::size_t off, std::uint32_t v) {
    std::memcpy(b.data() + off, &v, 4);
}
std::string SaveRecord::hdr_str(std::size_t off, std::size_t max) const {
    std::size_t n = 0;
    while (n < max && off + n < kSize && b[off + n]) ++n;
    return std::string(reinterpret_cast<const char*>(b.data() + off), n);
}
void SaveRecord::set_hdr_str(std::size_t off, const std::string& s, std::size_t max) {
    const std::size_t n = s.size() < max ? s.size() : max;
    std::memcpy(b.data() + off, s.data(), n);
    for (std::size_t i = n; i < max && off + i < kSize; ++i) b[off + i] = 0;
}

std::optional<SaveRecord> SaveRecord::decode(std::span<const std::uint8_t> raw) {
    if (raw.size() != kSize) return std::nullopt;
    SaveRecord r;
    std::memcpy(r.b.data(), raw.data(), kSize);
    if (r.hdr_u32(0x00) != kMagic || r.hdr_u32(0x04) != 1 || r.hdr_u32(0x08) != kSize)
        return std::nullopt;
    // key A descrambles the +0x30 header span; key B lives INSIDE that span, so
    // strictly in this order (SAVELOAD.md §4.1 / save_decode.py).
    scramble(r.b.data(), 0x30, 0xA0, r.hdr_u32(0x10), /*write*/ false);
    scramble(r.b.data(), 0xD0, kSize - 0xD0, r.hdr_u32(0xC0), /*write*/ false);
    return r;
}

std::vector<std::uint8_t> SaveRecord::encode(std::uint32_t key_a, std::uint32_t key_b) const {
    std::vector<std::uint8_t> out(b.begin(), b.end());
    // Writer order (FUN_0042e9a0 tail): key B, scramble the payload, CRC B over the
    // SCRAMBLED payload -> +0xc4 (+0xc8 zeroed); then key A, scramble +0x30..+0xd0
    // (which covers key B and CRC B), CRC A over the scrambled +0x30..+0x60 -> +0x0c.
    // The loader FUN_0042f5e0 checks both and reports "Data is corrupt" on a mismatch.
    std::memcpy(out.data() + 0xC0, &key_b, 4);
    scramble(out.data(), 0xD0, kSize - 0xD0, key_b, /*write*/ true);
    const std::uint32_t crc_b = crc32(out.data() + 0xD0, kSize - 0xD0), zero = 0;
    std::memcpy(out.data() + 0xC4, &crc_b, 4);
    std::memcpy(out.data() + 0xC8, &zero, 4);
    std::memcpy(out.data() + 0x10, &key_a, 4);
    scramble(out.data(), 0x30, 0xA0, key_a, /*write*/ true);
    const std::uint32_t crc_a = crc32(out.data() + 0x30, 0x30);
    std::memcpy(out.data() + 0x0C, &crc_a, 4);
    return out;
}

SaveRecord to_record(const SaveState& s, const std::string& area_line,
                     const std::string& date, const std::string& time) {
    SaveRecord r;
    // --- payload: the game-state block, by VA -------------------------------
    for (int i = 0; i < 0x200; ++i) r.set_i32(0x0076B91C + 4 * i, s.flags[i]);
    for (int i = 0; i < 54; ++i) r.set_i32(0x0076A654 + 4 * i, s.item_arr[i]);
    r.set_f32(0x0076A72C, s.hp_max);
    r.set_f32(0x0076A730, s.hp_cur);
    r.set_f32(0x0076A748, s.exp);
    r.set_f32(0x0076A734, s.mp_max);
    r.set_f32(0x0076A738, s.mp_cur);
    r.set_i32(0x0076A75C, s.sp);
    r.set_i32(0x0076A760, s.level);
    r.set_str(0x0076A3B4, s.scene, 0x20);
    r.set_f32(0x0076C13C, s.pos_x);
    r.set_f32(0x0076C140, s.pos_y);
    r.set_f32(0x0076C144, s.pos_z);
    r.set_f32(0x0076C148, s.yaw);
    r.set_i32(0x0076A628, s.accessory);
    r.set_i32(0x0076A62C, s.held);
    r.set_i32(0x0076A630, s.magic);
    for (int c = 0; c < 4; ++c) r.set_i32(0x0076A634 + 8 * c, s.tier[c]);
    r.set_f32(0x0076B8FC, s.hp_bonus);
    r.set_f32(0x0076B904, s.atk_buff);
    r.set_f32(0x0076B908, s.def_buff);
    r.set_f32(0x0076B90C, s.luck_base);
    r.set_i32(0x0076C19C, s.timer_atk);
    r.set_i32(0x0076C1A0, s.timer_def);
    r.set_f32(0x0076B900, s.mp_buff);
    r.set_i32(0x0076C1A4, s.timer_mp);
    r.set_f32(0x0076C1B8, s.o2);
    r.set_i32(0x0076C1BC, s.o2_alpha);
    for (int i = 0; i < 5; ++i) r.set_i32(0x0076C15C + 4 * i, s.ail[i]);
    r.set_f32(0x0076C1B4, s.boost);
    for (int i = 0; i < 5; ++i) r.set_i32(0x0076C218 + 4 * i, s.version[i]);
    std::memcpy(r.b.data() + (0x0076B8D8 - SaveRecord::kBase), s.manual_on, sizeof s.manual_on);
    std::memcpy(r.b.data() + (0x0076C11C - SaveRecord::kBase), s.warp_on, sizeof s.warp_on);
    // --- header stamps (SAVELOAD.md §4.1) ------------------------------------
    r.set_hdr_u32(0x00, SaveRecord::kMagic);
    r.set_hdr_u32(0x04, 1);
    r.set_hdr_u32(0x08, std::uint32_t(SaveRecord::kSize));
    // +0x0c / +0xc4 checksums are stamped by encode() (they cover scrambled bytes).
    // The quick copies the slot list reads (SAVELOAD.md §4.1; the float ones truncated like cvttss2si).
    // Verified against 20 retail saves: +0x34 level, +0x38 / +0x3c / +0x40 / +0x44 HP, MP, max HP, max MP,
    // +0x48 EXP, +0x4c SP, +0x58 / +0x5c / +0x60 flags 143 / 203 / 201 [DATA].
    r.set_hdr_u32(0x34, std::uint32_t(s.level));
    r.set_hdr_u32(0x38, std::uint32_t(std::int32_t(s.hp_cur)));
    r.set_hdr_u32(0x3C, std::uint32_t(std::int32_t(s.mp_cur)));
    r.set_hdr_u32(0x40, std::uint32_t(std::int32_t(s.hp_max)));
    r.set_hdr_u32(0x44, std::uint32_t(std::int32_t(s.mp_max)));
    r.set_hdr_u32(0x48, std::uint32_t(std::int32_t(s.exp)));
    r.set_hdr_u32(0x4C, std::uint32_t(s.sp));
    r.set_hdr_u32(0x50, std::uint32_t(s.difficulty() + 1));  // 1-based; 0 = empty slot
    r.set_hdr_u32(0x58, std::uint32_t(s.flags[143]));
    r.set_hdr_u32(0x5C, std::uint32_t(s.flags[203]));
    r.set_hdr_u32(0x60, std::uint32_t(s.flags[201]));
    // +0x64 = 0x76bc38 = flag 199, the play-time step count (FUN_004281c0 0x428274 counts it every
    // real, menu-free step; the HUD shows it as count * 1000 / 60 ms) [BYTES].
    r.set_hdr_u32(0x64, std::uint32_t(s.flags[199]));
    r.set_hdr_u32(0x68, std::uint32_t(s.character()));
    r.set_hdr_str(0x7C, date, 11);
    r.set_hdr_str(0x87, time, 9);
    r.set_hdr_str(0x90, area_line, 0x1F);
    return r;
}

SaveState from_record(const SaveRecord& r) {
    SaveState s;
    for (int i = 0; i < 0x200; ++i) s.flags[i] = r.i32(0x0076B91C + 4 * i);
    for (int i = 0; i < 54; ++i) s.item_arr[i] = r.i32(0x0076A654 + 4 * i);
    s.hp_max = r.f32(0x0076A72C);
    s.hp_cur = r.f32(0x0076A730);
    s.mp_max = r.f32(0x0076A734);
    s.mp_cur = r.f32(0x0076A738);
    s.exp = r.f32(0x0076A748);
    s.sp = r.i32(0x0076A75C);
    s.level = r.i32(0x0076A760);
    s.scene = r.str(0x0076A3B4, 0x20);
    s.pos_x = r.f32(0x0076C13C);
    s.pos_y = r.f32(0x0076C140);
    s.pos_z = r.f32(0x0076C144);
    s.yaw = r.f32(0x0076C148);
    s.accessory = r.i32(0x0076A628);
    s.held = r.i32(0x0076A62C);
    s.magic = r.i32(0x0076A630);
    for (int c = 0; c < 4; ++c) s.tier[c] = r.i32(0x0076A634 + 8 * c);
    s.hp_bonus = r.f32(0x0076B8FC);
    s.atk_buff = r.f32(0x0076B904);
    s.def_buff = r.f32(0x0076B908);
    s.luck_base = r.f32(0x0076B90C);
    s.timer_atk = r.i32(0x0076C19C);
    s.timer_def = r.i32(0x0076C1A0);
    s.mp_buff = r.f32(0x0076B900);
    s.timer_mp = r.i32(0x0076C1A4);
    s.o2 = r.f32(0x0076C1B8);
    s.o2_alpha = r.i32(0x0076C1BC);
    for (int i = 0; i < 5; ++i) s.ail[i] = r.i32(0x0076C15C + 4 * i);
    s.boost = r.f32(0x0076C1B4);
    for (int i = 0; i < 5; ++i) s.version[i] = r.i32(0x0076C218 + 4 * i);
    std::memcpy(s.manual_on, r.b.data() + (0x0076B8D8 - SaveRecord::kBase), sizeof s.manual_on);
    std::memcpy(s.warp_on, r.b.data() + (0x0076C11C - SaveRecord::kBase), sizeof s.warp_on);
    return s;
}

SaveMeta meta_of(const SaveRecord& r) {
    SaveMeta m;
    m.character = int(r.hdr_u32(0x68));
    m.difficulty = int(r.hdr_u32(0x50)) - 1;
    m.date = r.hdr_str(0x7C, 11);
    m.time = r.hdr_str(0x87, 9);
    m.area = r.hdr_str(0x90, 0x1F);
    return m;
}

}  // namespace cleria
