// Headless test for the save codec (build/menu_re/SAVELOAD.md §4): encode -> raw ->
// decode round-trips the full logical state bit-exactly, the raw bytes match what
// tools/save_decode.py expects (magic "YsO", keys at +0x10/+0xc0, add-cipher), and a
// truncated/garbage buffer is rejected. If CLERIA_REAL_SAVE points at a real
// yso_NN.bin, it is decoded and its character/difficulty printed (read-compat proof).
#include "cleria/save.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace cleria;

int main() {
    int fails = 0;
    SaveState s;
    // A representative mid-game state: Yunica, Normal, some story + inventory.
    s.flags[150] = 1; s.flags[149] = 1; s.flags[144] = 2;
    for (int i = 0; i < 0x80; ++i) s.flags[i] = -1;
    s.flags[0] = 1; s.flags[6] = 1; s.flags[119] = 1;
    s.flags[217] = (1 << 3) | (1 << 9);          // story bitset
    s.flags[235] = 1;                            // search-party routing flag
    s.item_arr[6] = 1;
    s.hp_max = 78; s.hp_cur = 55.5f; s.exp = 1234.0f; s.level = 9; s.sp = 4321;
    s.scene = "s_1004";
    s.pos_x = -12.25f; s.pos_z = 44.5f; s.yaw = 180.0f;

    const SaveRecord rec = to_record(s, "Water Prison 1F", "07/23/2026", "13:37:00");
    const std::vector<std::uint8_t> raw = rec.encode(0xDEADBEEFu, 0x1234ABCDu);

    bool ok = raw.size() == SaveRecord::kSize &&
              std::memcmp(raw.data(), "YsO\0", 4) == 0;     // magic = LE 0x004f7359
    // The scrambled payload must NOT equal the plain record (cipher actually ran).
    ok = ok && std::memcmp(raw.data() + 0xD0, rec.b.data() + 0xD0, 64) != 0;

    auto dec = SaveRecord::decode(raw);
    if (!dec) { std::printf("[save] decode(encode(x)) FAILED to parse\n"); return 1; }
    const SaveState t = from_record(*dec);
    ok = ok && std::memcmp(t.flags, s.flags, sizeof s.flags) == 0 &&
         std::memcmp(t.item_arr, s.item_arr, sizeof s.item_arr) == 0 &&
         t.hp_max == s.hp_max && t.hp_cur == s.hp_cur && t.exp == s.exp &&
         t.level == s.level && t.sp == s.sp && t.scene == s.scene &&
         t.pos_x == s.pos_x && t.pos_z == s.pos_z && t.yaw == s.yaw;
    const SaveMeta m = meta_of(*dec);
    ok = ok && m.character == 1 && m.difficulty == 2 && m.area == "Water Prison 1F" &&
         m.date == "07/23/2026" && m.time == "13:37:00";
    std::printf("[save] round-trip + header meta %s\n", ok ? "OK" : "FAIL");
    fails += !ok;

    // Rejection: wrong size, wrong magic.
    std::vector<std::uint8_t> bad(raw.begin(), raw.end() - 1);
    bool rej = !SaveRecord::decode(bad);
    bad = raw; bad[0] ^= 0xFF;
    rej = rej && !SaveRecord::decode(bad);
    std::printf("[save] corrupt-input rejection %s\n", rej ? "OK" : "FAIL");
    fails += !rej;

    // Optional: dump the encoded record for external cross-checks (save_decode.py).
    if (const char* p = std::getenv("CLERIA_SAVE_OUT")) {
        std::ofstream o(p, std::ios::binary);
        o.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
    }

    // Optional: prove read-compat against a REAL save (not in the repo — Falcom data).
    if (const char* p = std::getenv("CLERIA_REAL_SAVE")) {
        std::ifstream f(p, std::ios::binary);
        std::vector<std::uint8_t> real{std::istreambuf_iterator<char>(f), {}};
        auto r = SaveRecord::decode(real);
        if (r) {
            const SaveMeta rm = meta_of(*r);
            const SaveState rs = from_record(*r);
            std::printf("[save] REAL save: char=%d diff=%d area='%s' date=%s scene='%s' "
                        "lv=%d hp=%.0f/%.0f sp=%d\n",
                        rm.character, rm.difficulty, rm.area.c_str(), rm.date.c_str(),
                        rs.scene.c_str(), rs.level, rs.hp_cur, rs.hp_max, rs.sp);
        } else {
            std::printf("[save] REAL save FAILED to decode\n");
            ++fails;
        }
    }
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
