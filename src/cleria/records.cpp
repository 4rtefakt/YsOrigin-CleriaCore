#include "cleria/records.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace cleria::records {
namespace {

// FUN_005ee870: reflected CRC-32, poly 0xEDB88320, init / final ~ (zlib's) [BYTES 0x5ee870..0x5ee951].
std::uint32_t crc32(const std::uint8_t* p, std::size_t len) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
void scramble(std::uint8_t* p, std::size_t len, std::uint32_t key, bool write) {
    for (std::size_t i = 0; i < len; ++i) {
        key *= 0x3D09u;
        const std::uint8_t k = std::uint8_t(key >> 16);
        p[i] = std::uint8_t(write ? p[i] + k : p[i] - k);
    }
}
void put32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p, &v, 4); }
std::uint32_t get32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }

}  // namespace

std::vector<std::uint8_t> encode(std::span<const std::uint8_t> payload, std::uint32_t key, std::uint32_t noise_a,
                                 std::uint32_t noise_b) {
    std::vector<std::uint8_t> f(kHeader + payload.size(), 0);
    put32(&f[0x00], 0x004F7359u);
    put32(&f[0x04], 0x0000000Du);
    put32(&f[0x08], noise_a);
    put32(&f[0x0C], noise_b);
    static constexpr std::uint8_t kTag[6] = {0x8B, 0x10, 0xCF, 0x78, 0x18, 0x56};
    std::memcpy(&f[0x10], kTag, 6);
    put32(&f[0x20], key);
    put32(&f[0x24], crc32(payload.data(), payload.size()));
    std::copy(payload.begin(), payload.end(), f.begin() + kHeader);
    scramble(&f[0x24], f.size() - 0x24, key, true);
    return f;
}

std::optional<std::vector<std::uint8_t>> decode(std::span<const std::uint8_t> file, std::size_t payload_size) {
    if (file.size() != kHeader + payload_size) return std::nullopt;
    std::vector<std::uint8_t> t(file.begin() + 0x24, file.end());
    scramble(t.data(), t.size(), get32(&file[0x20]), false);
    if (crc32(t.data() + 4, payload_size) != get32(t.data())) return std::nullopt;
    return std::vector<std::uint8_t>(t.begin() + 4, t.end());
}

std::uint32_t make_key(std::uint32_t seed) {
    std::uint32_t s = seed ? seed : 0x9E3779B9u;
    for (int n = 0; n < 64; ++n) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;                   // a local xorshift32, not the game's stream
        int bits = 0;
        for (std::uint32_t v = s; v; v &= v - 1) ++bits;
        if (bits >= 11 && bits <= 21) return s;
    }
    return 0x5A5A1234u;                                            // 14 bits set
}

ClearData ClearData::from_payload(std::span<const std::uint8_t> p) {
    ClearData d;
    auto b = [&](std::size_t off) { return p[off]; };              // the dword's low byte
    for (std::size_t c = 0; c < 3; ++c) d.clear[c] = b(c * 4);
    for (std::size_t t = 0; t < 5; ++t)
        for (std::size_t c = 0; c < 3; ++c) d.tier[t][c] = b(0x0C + (t * 3 + c) * 4);
    d.count = b(0x48); d.nya = b(0x4C); d.c105 = b(0x50);
    d.unused54 = get32(&p[0x54]);
    for (std::size_t i = 0; i < kShopCount; ++i) d.shop[i] = get32(&p[0x58 + i * 4]);
    for (std::size_t i = 0; i < 4; ++i) d.unused88[i] = get32(&p[0x88 + i * 4]);
    return d;
}

std::array<std::uint8_t, ClearData::kPayload> ClearData::payload() const {
    std::array<std::uint8_t, kPayload> p{};
    for (std::size_t c = 0; c < 3; ++c) p[c * 4] = clear[c];
    for (std::size_t t = 0; t < 5; ++t)
        for (std::size_t c = 0; c < 3; ++c) p[0x0C + (t * 3 + c) * 4] = tier[t][c];
    p[0x48] = count; p[0x4C] = nya; p[0x50] = c105;
    put32(&p[0x54], unused54);
    for (std::size_t i = 0; i < kShopCount; ++i) put32(&p[0x58 + i * 4], shop[i]);
    for (std::size_t i = 0; i < 4; ++i) put32(&p[0x88 + i * 4], unused88[i]);
    return p;
}

int ClearData::tier_count() const {
    int n = 0;
    for (const auto& row : tier) for (std::uint8_t v : row) n += v != 0;
    return n;
}

std::vector<std::uint8_t> encode_clear(const ClearData& d, std::uint32_t key) {
    const auto p = d.payload();
    return encode(p, key);
}

std::optional<ClearData> decode_clear(std::span<const std::uint8_t> file) {
    const auto p = decode(file, ClearData::kPayload);
    if (!p) return std::nullopt;
    return ClearData::from_payload(*p);
}

std::optional<ClearData> load_clear(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    const std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
    return decode_clear(b);
}

bool save_clear(const std::filesystem::path& file, const ClearData& d, std::uint32_t key) {
    const std::vector<std::uint8_t> b = encode_clear(d, key);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(f);
}

std::optional<std::uint32_t> load_arena_sp(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    const std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
    const auto p = decode(b, 4);
    if (!p) return std::nullopt;
    return std::uint32_t((*p)[0]) | std::uint32_t((*p)[1]) << 8 | std::uint32_t((*p)[2]) << 16 | std::uint32_t((*p)[3]) << 24;
}

bool save_arena_sp(const std::filesystem::path& file, std::uint32_t sp, std::uint32_t key) {
    const std::uint8_t p[4] = {std::uint8_t(sp), std::uint8_t(sp >> 8), std::uint8_t(sp >> 16), std::uint8_t(sp >> 24)};
    const std::vector<std::uint8_t> b = encode(p, key);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(f);
}

void apply_clear(ClearData& d, int character, bool extra, bool easy, int hard, bool very_easy) {
    if (character < 1 || character > 3) return;
    const std::size_t col = std::size_t(character - 1);
    std::uint8_t v = std::max<std::uint8_t>(d.clear[col], 1);      // cmp byte, 1 / cmovb
    if (extra) v = ClearData::kExtra[col];
    d.clear[col] = v;
    int row = -1;                                                  // 0 Very Easy .. 4 Nightmare
    if (!easy) { if (hard >= 0 && hard <= 2) row = 2 + hard; }
    else row = very_easy ? 0 : 1;
    if (row >= 0) d.tier[std::size_t(row)][col] = 1;
}

int sp_bonus(const ClearData& d, int hard) {
    const int sp = std::min(d.tier_count() * 10000, 150000);
    const float f = hard == 2 ? 0.01f : 1.0f;                      // 0x6bd084 / 0x6bd438
    return int(std::floor(float(sp) * f));
}

}  // namespace cleria::records
