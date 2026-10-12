// Headless test for the save codec (build/menu_re/SAVELOAD.md §4): encode -> raw ->
// decode round-trips the full logical state bit-exactly, the raw bytes match what
// tools/save_decode.py expects (magic "YsO", keys at +0x10/+0xc0, add-cipher), and a
// truncated/garbage buffer is rejected. If CLERIA_REAL_SAVE points at a real
// yso_NN.bin, it is decoded and its character/difficulty printed (read-compat proof).
#include "cleria/records.hpp"
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

        // Save profiles (LUA_API.md §6.1): the folder name a mod's text becomes, and where it lands.
        bool pf = save_profile_name("AP_12345_Yunica-1.b") == "AP_12345_Yunica-1.b";
        pf = pf && save_profile_name("../x") == ".._x" && save_profile_name("a/b") == "a_b" && save_profile_name("a\\b") == "a_b";
        pf = pf && save_profile_name("C:\\Windows") == "C__Windows" && save_profile_name("/etc") == "_etc";
        pf = pf && save_profile_name("").empty() && save_profile_name(".").empty() && save_profile_name("..").empty();
        pf = pf && save_profile_name("....").empty() && save_profile_name("a.") == "a_" && save_profile_name(" ") == "_";
        pf = pf && save_profile_name(std::string(300, 'a')) == std::string(64, 'a');
        pf = pf && save_profile_name(std::string(70, '.')).empty();              // dots only, also once cut to 64
        pf = pf && save_profile_name("s\xC3\xA9" "ed\xE2\x98\x83") == "s__ed___";  // UTF-8: every byte
        pf = pf && save_profile_name(std::string("a\0b\n", 4)) == "a_b_";
        for (const char* h : {"../x", "a/b", "a\\b", "..\\..\\x", "x/../../y", "\xE2\x98\x83", "CON:", "a\tb"}) {
            const std::string n = save_profile_name(h);
            pf = pf && !n.empty() && n.size() <= 64 && n.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos;
            const fs::path d = save_profile_dir(dir, n);
            pf = pf && d.parent_path() == dir / "profiles" && d.filename().string() == n;   // one plain level down
        }
        pf = pf && save_profile_dir(dir, "") == dir && save_profile_dir(dir, "AP_1") == dir / "profiles" / "AP_1";
        std::printf("[save] profile names: hostile text ('../x', 'a/b', '..', 300 chars, UTF-8) stays one folder under profiles/ %s\n", pf ? "OK" : "FAIL");
        fails += !pf;
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
    // ---- the record container and the clear data (build/mode_re/CLEAR_DATA.md; src/cleria/records.hpp) ----
    {
        auto ok = [&](bool c, const char* what) { std::printf("[records] %s %s\n", what, c ? "OK" : "FAIL"); fails += !c; };
        records::ClearData d;
        d.clear = {1, 0, 0x89};
        d.tier[2][0] = 1; d.tier[4][2] = 1;
        d.count = 4; d.nya = 1; d.shop[records::ClearData::kToalExtra] = 1; d.shop[records::ClearData::kYunicaOutfit] = 1;
        const std::vector<std::uint8_t> f = records::encode_clear(d, 0x1234ABCDu);
        const std::uint8_t head[8] = {0x59, 0x73, 0x4F, 0x00, 0x0D, 0x00, 0x00, 0x00};
        const std::uint8_t tag[6] = {0x8B, 0x10, 0xCF, 0x78, 0x18, 0x56};
        ok(f.size() == 0xC0 && std::memcmp(f.data(), head, 8) == 0 && std::memcmp(f.data() + 0x10, tag, 6) == 0 &&
           f[0x20] == 0xCD && f[0x23] == 0x12, "record.ta2 image: 0xC0 bytes, \"YsO\\0\" 0x0D, the tag at +0x10, the key at +0x20");
        const auto back = records::decode_clear(f);
        ok(back && back->clear == d.clear && back->tier == d.tier && back->count == 4 && back->nya == 1 && back->shop == d.shop,
           "clear data round-trips through the container");
        const auto p = d.payload();
        ok(p[0x08] == 0x89 && p[0x09] == 0 && p[0x24] == 1 && p[0x44] == 1 && p[0x48] == 4 && p[0x4C] == 1 && p[0x68] == 1 && p[0x84] == 1,
           "payload offsets: Toal's clear +0x08, Normal x Yunica +0x24, Nightmare x Toal +0x44, the shop dwords +0x58..+0x84");
        std::vector<std::uint8_t> bad = f;
        bad[0x40] ^= 1;
        ok(!records::decode_clear(bad), "a flipped payload byte fails the CRC");
        bad = f; bad.pop_back();
        ok(!records::decode_clear(bad), "a wrong length is refused");
        const std::uint32_t k = records::make_key(1);
        int bits = 0;
        for (std::uint32_t v = k; v; v &= v - 1) ++bits;
        ok(k != 0 && bits >= 11 && bits <= 21, "make_key: non-zero, 11..21 bits set");

        // EventXX(4) (CLEAR_DATA.md §3.2)
        records::ClearData c;
        records::apply_clear(c, 3, false, false, 1, false);         // Toal, Hard
        ok(c.clear[2] == 1 && c.tier[3][2] == 1 && c.tier_count() == 1 && !c.clear[0], "EventXX(4): Toal on Hard -> clear 1, the Hard row");
        records::apply_clear(c, 3, true, false, 2, false);          // Toal Extra, Nightmare
        ok(c.clear[2] == 0x89 && c.tier[4][2] == 1, "EventXX(4): the Extra version stores 0x89; Nightmare row");
        records::apply_clear(c, 3, false, false, 0, false);
        ok(c.clear[2] == 0x89 && c.tier[2][2] == 1, "EventXX(4): a later normal clear keeps the Extra value (max); Normal = row 0x74ab94");
        records::apply_clear(c, 1, true, true, 0, false);           // Yunica Extra, Easy
        records::apply_clear(c, 2, true, true, 2, true);            // Hugo Extra, Very Easy (flag 144 is not read on the easy side)
        ok(c.clear[0] == 0xD5 && c.tier[1][0] == 1 && c.clear[1] == 0x8E && c.tier[0][1] == 1 && c.tier[4][1] == 0,
           "EventXX(4): Easy / Very Easy rows from flags 143 / 203; 0xD5 / 0x8E");
        records::ClearData n;
        records::apply_clear(n, 1, false, false, 3, false);
        records::apply_clear(n, 0, false, false, 0, false);
        ok(n.clear[0] == 1 && n.tier_count() == 0, "EventXX(4): flag 144 > 2 writes no tier byte; character 0 writes nothing");
        // EventXX(13) (BONUS_MODES.md §1)
        ok(records::sp_bonus(c, 0) == 50000 && records::sp_bonus(c, 2) == 500 && records::sp_bonus(n, 0) == 0,
           "EventXX(13): 10,000 SP per cleared cell (5 -> 50,000), a hundredth on Nightmare");
        for (auto& row : c.tier) row = {1, 1, 1};
        ok(records::sp_bonus(c, 1) == 150000, "EventXX(13): fifteen cells = the 150,000 cap");
        {   // record.ar2: one dword
            const std::filesystem::path f = std::filesystem::temp_directory_path() / "cleria_savetest_record.ar2";
            ok(records::save_arena_sp(f, 2811, records::make_key(7)) && records::load_arena_sp(f) == 2811u &&
               std::filesystem::file_size(f) == 0x2C, "record.ar2: the Arena SP wallet round-trips in a 0x2C-byte file");
            std::error_code ec;
            std::filesystem::remove(f, ec);
        }

        {   // record.ta / .ar: the best-time tables (CLEAR_DATA.md §4)
            records::RecordTable ta(records::RecordTable::kTimeAttackRows), ar(records::RecordTable::kArenaRows);
            ok(ta.bytes.size() == 0xB40 && ar.bytes.size() == 0x540, "record tables: 16 x 15 x 12 = 0xB40 and 16 x 7 x 12 = 0x540 bytes");
            static const std::uint8_t kEmpty[12] = {0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0xFF, 0};
            ok(std::equal(kEmpty, kEmpty + 12, ta.bytes.begin() + 12 * 239), "record table: the empty entry 0 / FFFFFFFF / FF 00 FF 00");
            // Yunica (1), Hard (d 2), stage 3: entry (1 * 4 + 2) * 15 + 3 = 93; 3600 frames = 60,000 ms.
            ok(ta.submit(1, 2, 3, 3600) && ta.ms(1, 2, 3) == 60000 && ta.mark(1, 2, 3) == 1 && ta.entry(1, 2, 3) == 93 * 12,
               "record table: a first time is kept (frames * 1000 / 60), mark 1, entry (char * 4 + d) * 15 + stage");
            ok(!ta.submit(1, 2, 3, 3600) && !ta.submit(1, 2, 3, 3601) && ta.submit(1, 2, 3, 3599) && ta.ms(1, 2, 3) == 59983,
               "record table: only a smaller time replaces it");
            ta.clear_marks(13);
            ok(ta.mark(1, 2, 3) == 0 && ta.mark(0, 0, 12) == 0 && ta.mark(0, 0, 13) == 0xFF && ta.ms(1, 2, 3) == 59983,
               "record table: clear_marks(13) zeroes rows 0..12 of every block, times stay");
            ok(ar.submit(3, 1, 0, 10120) && ar.entry(3, 1, 0) == 91 * 12 && ar.ms(3, 1, 0) == 168666, "arena table: entry (char * 4 + d) * 7 + stage");
            const std::filesystem::path f = std::filesystem::temp_directory_path() / "cleria_savetest_record.ta";
            const bool wrote = records::save_table(f, ta, records::make_key(77));
            const auto back = records::load_table(f, records::RecordTable::kTimeAttackRows);
            ok(wrote && back && back->bytes == ta.bytes && std::filesystem::file_size(f) == 0xB68 &&
               !records::load_table(f, records::RecordTable::kArenaRows), "record.ta: round-trips in a 0xB68-byte file; the wrong shape is refused");
            std::error_code rm;
            std::filesystem::remove(f, rm);
        }

        // The real retail files, READ-ONLY: CLERIA_REAL_RECORDS = their folder, else Steam's remote folder of app
        // 207350 under any user id. All seven must decode (exact length, CRC) and re-encode byte for byte.
        namespace fs = std::filesystem;
        fs::path dir;
        std::error_code ec;
        if (const char* e = std::getenv("CLERIA_REAL_RECORDS")) dir = e;
        else if (const char* pf = std::getenv("ProgramFiles(x86)")) {
            const fs::path ud = fs::path(pf) / "Steam" / "userdata";
            if (fs::is_directory(ud, ec))
                for (const auto& u : fs::directory_iterator(ud, ec))
                    if (fs::exists(u.path() / "207350" / "remote" / "record.ta2", ec)) { dir = u.path() / "207350" / "remote"; break; }
        }
        if (dir.empty() || !fs::exists(dir / "record.ta2", ec)) {
            std::printf("[records] no real record files found (CLERIA_REAL_RECORDS): the retail-file checks are SKIPPED\n");
        } else {
            static const struct { const char* name; std::size_t payload; } kFiles[7] = {
                {"record.ta2", 0x98}, {"record.ar2", 0x04}, {"record.ta", 0xB40}, {"record.tae", 0xB40},
                {"record.ar", 0x540}, {"record.are", 0x540}, {"record.tp", 0x1458}};
            int good = 0;
            for (const auto& rf : kFiles) {
                std::ifstream in(dir / rf.name, std::ios::binary);
                const std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(in), {}};
                const auto pl = records::decode(raw, rf.payload);
                if (!pl) { std::printf("[records] REAL %s did not decode (%zu bytes)\n", rf.name, raw.size()); continue; }
                std::uint32_t key, na, nb;
                std::memcpy(&key, raw.data() + 0x20, 4); std::memcpy(&na, raw.data() + 8, 4); std::memcpy(&nb, raw.data() + 12, 4);
                good += records::encode(*pl, key, na, nb) == raw;
            }
            ok(good == 7, "REAL retail record files: all seven decode (length + CRC) and re-encode byte-exact");
            if (const auto sp = records::load_arena_sp(dir / "record.ar2")) std::printf("[records] REAL record.ar2: Arena SP %u\n", *sp);
            else ok(false, "REAL record.ar2 loads");
            for (const char* n : {"record.ta", "record.tae", "record.ar", "record.are"}) {
                const auto t = records::load_table(dir / n, n[7] == 't' ? records::RecordTable::kTimeAttackRows : records::RecordTable::kArenaRows);
                if (!t) { ok(false, "REAL record table loads"); continue; }
                int set = 0;
                for (int c = 0; c < 4; ++c) for (int d = 0; d < 4; ++d) for (int r = 0; r < t->rows; ++r)
                    if (const std::uint32_t ms = t->ms(c, d, r)) {
                        ++set;
                        std::printf("[records] REAL %s: character %d difficulty %d row %d = %u ms (mark %u)\n", n, c, d, r, ms, t->mark(c, d, r));
                    }
                if (!set) std::printf("[records] REAL %s: no time set\n", n);
            }
            std::ifstream in(dir / "record.ta2", std::ios::binary);
            const std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(in), {}};
            if (const auto cd = records::decode_clear(raw)) {
                std::printf("[records] REAL record.ta2: clear %02x %02x %02x, tiers %d, shop", cd->clear[0], cd->clear[1], cd->clear[2],
                            cd->tier_count());
                for (std::uint32_t v : cd->shop) std::printf(" %u", v);
                std::printf("\n");
                const auto pl = records::decode(raw, records::ClearData::kPayload);
                const auto again = cd->payload();
                ok(pl && std::equal(again.begin(), again.end(), pl->begin()), "REAL record.ta2: ClearData reproduces its payload byte for byte");
            }
        }
    }
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
