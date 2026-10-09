// Headless test for the save codec (build/menu_re/SAVELOAD.md §4): encode -> raw ->
// decode round-trips the full logical state bit-exactly, the raw bytes match what
// tools/save_decode.py expects (magic "YsO", keys at +0x10/+0xc0, add-cipher), and a
// truncated/garbage buffer is rejected. If CLERIA_REAL_SAVE points at a real
// yso_NN.bin, it is decoded and its character/difficulty printed (read-compat proof).
#include "cleria/save.hpp"
#include "cleria/sidecar.hpp"
#include <cstdio>
#include <filesystem>
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
    s.pos_x = -12.25f; s.pos_y = 2.0f; s.pos_z = 44.5f; s.yaw = 180.0f;
    s.o2 = 1234.5f; s.o2_alpha = 255;           // O2 0x76c1b8 + its gauge 0x76c1bc (in the block)
    s.ail[0] = 0x21; s.ail[1] = 700; s.ail[4] = 1800;   // the ailment mask + timers, g_flags 528..532
    s.boost = 12486.0f;                         // the Boost gauge 0x76c1b4

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
         t.pos_x == s.pos_x && t.pos_y == s.pos_y && t.pos_z == s.pos_z && t.yaw == s.yaw &&
         dec->f32(0x0076C140) == 2.0f &&         // the snapshot y FUN_00443d70 stores
         t.o2 == 1234.5f && t.o2_alpha == 255 && dec->f32(0x0076C1B8) == 1234.5f && dec->i32(0x0076C1BC) == 255 &&
         std::memcmp(t.ail, s.ail, sizeof s.ail) == 0 && dec->i32(0x0076B91C + 4 * 528) == 0x21 &&
         dec->i32(0x0076B91C + 4 * 532) == 1800 && t.boost == 12486.0f && dec->f32(0x0076C1B4) == 12486.0f;
    const SaveMeta m = meta_of(*dec);
    ok = ok && m.character == 1 && m.difficulty == 2 && m.area == "Water Prison 1F" &&
         m.date == "07/23/2026" && m.time == "13:37:00";
    std::printf("[save] round-trip + header meta %s\n", ok ? "OK" : "FAIL");
    fails += !ok;

    // The slot list's quick copies (SAVELOAD.md §4.1, checked against 20 retail saves): +0x34 level
    // (the list's "Lv"), +0x38..+0x44 HP / MP / max HP / max MP, +0x48 EXP, +0x4c SP, +0x58/5c/60
    // flags 143 / 203 / 201; and the skill gauge round-trips through the payload.
    {
        SaveState q = s;
        q.mp_max = 3000.0f; q.mp_cur = 1234.9f;
        q.flags[143] = 1; q.flags[203] = 0; q.flags[201] = 7;
        const auto rq = SaveRecord::decode(to_record(q, "x", "01/01/2026", "00:00:00").encode(1u, 2u));
        bool hq = rq && rq->hdr_u32(0x34) == 9 && rq->hdr_u32(0x38) == 55 && rq->hdr_u32(0x3C) == 1234 &&
                  rq->hdr_u32(0x40) == 78 && rq->hdr_u32(0x44) == 3000 && rq->hdr_u32(0x48) == 1234 &&
                  rq->hdr_u32(0x4C) == 4321 && rq->hdr_u32(0x58) == 1 && rq->hdr_u32(0x5C) == 0 &&
                  rq->hdr_u32(0x60) == 7;
        if (rq) { const SaveState tq = from_record(*rq); hq = hq && tq.mp_max == 3000.0f && tq.mp_cur == 1234.9f; }
        std::printf("[save] slot-list header copies (level %u) %s\n", rq ? rq->hdr_u32(0x34) : 0u, hq ? "OK" : "FAIL");
        fails += !hq;
    }

    // The CleriaCore sidecar (src/cleria/sidecar.hpp, build/shell_re/CONTENT.md §2): yso_NN.cleria
    // beside the retail record, never touching its bytes; empty = no file.
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "cleria_savetest_sidecar";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path bin = dir / "yso_03.bin";
        { std::ofstream o(bin, std::ios::binary); o.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size())); }
        bool sc_ok = sidecar_path(bin) == dir / "yso_03.cleria";
        Sidecar a;
        sc_ok = sc_ok && a.empty() && a.item(0x100) == -1;
        a.mods = {"sample.chest_swap 1.0.0", "x.y 2"};
        a.set_flag("Chest_Swap.P1", 1);
        a.set_flag("rule.lantern", 1);
        a.add_item(0x100, 1);
        a.add_item(0x100, 2);
        a.add_item(0x1FF, 150);                       // capped at 99
        a.mod["x.y"]["seed"] = "ABC \"quoted\" 123";
        a.mod["x.y"]["received"] = "12";
        sc_ok = sc_ok && a.item(0x100) == 3 && a.item(0x1FF) == 99 && a.flag("chest_swap.p1") == 1;
        sc_ok = sc_ok && write_sidecar(bin, a) && fs::exists(sidecar_path(bin));
        const Sidecar b = read_sidecar(bin);
        sc_ok = sc_ok && b.mods == a.mods && b.flags == a.flags && b.items == a.items && b.mod == a.mod;
        // The retail record beside it is byte-identical (the sidecar never writes into it).
        { std::ifstream f(bin, std::ios::binary); std::vector<std::uint8_t> back{std::istreambuf_iterator<char>(f), {}};
          sc_ok = sc_ok && back == raw && SaveRecord::decode(back).has_value(); }
        std::printf("[save] sidecar round-trip (flags, new items, a mod's block, mod stamp) %s\n", sc_ok ? "OK" : "FAIL");
        fails += !sc_ok;
        // An empty sidecar removes a stale file; absent / foreign / newer files read as empty.
        bool sc_empty = write_sidecar(bin, Sidecar{}) && !fs::exists(sidecar_path(bin)) && read_sidecar(bin).empty();
        { std::ofstream o(sidecar_path(bin), std::ios::binary); o << "[cleria]\r\nVersion=99\r\n[flags]\r\nx=1\r\n"; }
        sc_empty = sc_empty && read_sidecar(bin).empty();
        { std::ofstream o(sidecar_path(bin), std::ios::binary); o << "not a sidecar"; }
        sc_empty = sc_empty && read_sidecar(bin).empty() && !Sidecar::parse("[flags]\r\nx=1\r\n");
        {   // an old file's [ap] block (the removed built-in Archipelago client's) loads harmlessly: ignored
            const auto old = Sidecar::parse("[cleria]\r\nVersion=1\r\n[flags]\r\nx=1\r\n[ap]\r\nseed=\"S\"\r\napplied=3\r\n");
            sc_empty = sc_empty && old && old->flag("x") == 1 && old->mod.empty() && old->serialize().find("[ap]") == std::string::npos;
        }
        Sidecar c;
        c.set_flag("z", 5);
        c.set_flag("z", 0);                            // 0 erases
        sc_empty = sc_empty && c.empty();
        remove_sidecar(bin);
        sc_empty = sc_empty && !fs::exists(sidecar_path(bin)) && fs::exists(bin);
        std::printf("[save] sidecar empty = no file, stale / foreign / newer files ignored %s\n", sc_empty ? "OK" : "FAIL");
        fails += !sc_empty;
        fs::remove_all(dir, ec);
    }

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
            // Write-compat: re-encoding with the file's own keys must reproduce it byte
            // for byte — CRC A/B included, so the real exe will accept our saves.
            const bool same = r->encode(r->hdr_u32(0x10), r->hdr_u32(0xC0)) == real;
            std::printf("[save] REAL save re-encode byte-exact (CRCs) %s\n", same ? "OK" : "FAIL");
            fails += !same;
        } else {
            std::printf("[save] REAL save FAILED to decode\n");
            ++fails;
        }
    }
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
