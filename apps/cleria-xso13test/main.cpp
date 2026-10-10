// cleria-xso13test: the XSO ops of the floor 1..3 completion pass (build/event_re/FLOOR13_OPS.md),
// on synthetic scripts and on the shipped scripts that use them. Usage: cleria-xso13test <assets>
#include "cleria/xso.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace cleria;

static std::vector<std::uint8_t> rd(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

// A synthetic XSR builder with typed operand descriptors (the cleria-cotest one).
struct Asm {
    std::vector<std::int32_t> code;
    std::vector<std::string> pool;
    struct A { int t; std::int32_t v; };
    static A I(std::int32_t v) { return {0, v}; }
    static A F(float f) { std::int32_t w; std::memcpy(&w, &f, 4); return {1, w}; }
    A S(const std::string& s) {
        for (std::size_t k = 0; k < pool.size(); ++k) if (pool[k] == s) return {2, std::int32_t(k)};
        pool.push_back(s); return {2, std::int32_t(pool.size() - 1)};
    }
    void op(int sub, std::initializer_list<A> args) {
        const std::uint32_t n = std::uint32_t(args.size()), nd = (n + 7) / 8;
        code.push_back(std::int32_t((2u << 24) | (std::uint32_t(sub) << 12) | (nd << 8) | n));
        std::vector<std::uint32_t> d(nd, 0u);
        std::uint32_t i = 0;
        for (const A& a : args) { d[i >> 3] |= std::uint32_t(a.t) << ((i & 7) * 4); ++i; }
        for (std::uint32_t w : d) code.push_back(std::int32_t(w));
        for (const A& a : args) code.push_back(a.v);
    }
    void raw(std::int32_t w) { code.push_back(w); }
    std::vector<std::uint8_t> bytes() const {
        std::vector<std::uint8_t> b(0x24, 0);
        std::memcpy(b.data(), "XSR\0", 4);
        const std::uint32_t words = std::uint32_t(code.size()), cnt = std::uint32_t(pool.size());
        std::memcpy(b.data() + 0x1C, &words, 4);
        std::memcpy(b.data() + 0x20, &cnt, 4);
        const auto* cp = reinterpret_cast<const std::uint8_t*>(code.data());
        b.insert(b.end(), cp, cp + code.size() * 4);
        std::uint32_t off = 0;
        for (const std::string& s : pool) {
            const auto* op = reinterpret_cast<const std::uint8_t*>(&off);
            b.insert(b.end(), op, op + 4);
            off += std::uint32_t(s.size() + 1);
        }
        for (const std::string& s : pool) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
        return b;
    }
};

// Records every host call the new ops make.
struct H : XsoHost {
    std::string label;
    std::vector<std::string> log;
    int wallet = 0;
    void spawn(int, int, float, float, float, float, int) override {}
    bool is_alive(int) const override { return false; }
    std::string entry_label() const override { return label; }
    void chr_damage(int id, float atk, bool b, int c) override {
        char t[96]; std::snprintf(t, sizeof t, "damage %d %.1f %d %d", id, atk, int(b), c); log.push_back(t);
    }
    void chr_collision_vanish(int id, bool on) override { log.push_back("vanish " + std::to_string(id) + " " + std::to_string(on)); }
    void chr_stop(int id) override { log.push_back("stop " + std::to_string(id)); }
    void chr_no_force(int id, bool on) override { log.push_back("noforce " + std::to_string(id) + " " + std::to_string(on)); }
    void chr_ani_disable(int id, bool on) override { log.push_back("anidisable " + std::to_string(id) + " " + std::to_string(on)); }
    void pad_tilt(int i) override { log.push_back("tilt " + std::to_string(i)); }
    void se_play_pos(int se, float x, float y, float z) override {
        char t[96]; std::snprintf(t, sizeof t, "se3d %d %.2f %.2f %.2f", se, x, y, z); log.push_back(t);
    }
    void se_play_chr(int se, int id) override { log.push_back("sechr " + std::to_string(se) + " " + std::to_string(id)); }
    void se_play(int se) override { log.push_back("se " + std::to_string(se)); }
    void effect_alpha_current(const std::string& n, float a) override {
        char t[96]; std::snprintf(t, sizeof t, "fxalpha %s %.2f", n.c_str(), a); log.push_back(t);
    }
    void mesh_alpha_current(const std::string& n, float a) override {
        char t[96]; std::snprintf(t, sizeof t, "meshalpha %s %.2f", n.c_str(), a); log.push_back(t);
    }
    void mesh_attach_effect(const std::string& m, const std::string& f, int e) override {
        log.push_back("attach " + m + " '" + f + "' " + std::to_string(e));
    }
    void mesh_drop_item(const std::string& m, int item, int keep) override {
        log.push_back("drop " + m + " " + std::to_string(item) + " " + std::to_string(keep));
    }
    void set_time_scale(float s) override { char t[64]; std::snprintf(t, sizeof t, "timescale %.2f", s); log.push_back(t); }
    void weapon_level(int k, int v) override { log.push_back("weapon " + std::to_string(k) + " " + std::to_string(v)); }
    int gold() const override { return wallet; }
    void set_gold(int v) override { wallet = v; }
    void force_in_box(const std::string& b, float fx, float fy, float fz, int n, bool g) override {
        char t[128]; std::snprintf(t, sizeof t, "boxforce %s %.3f %.3f %.3f %d %d", b.c_str(), fx, fy, fz, n, int(g)); log.push_back(t);
    }
    void force_to_point(float x, float y, float z, float s, int n, float r0, float r1, bool g) override {
        char t[160]; std::snprintf(t, sizeof t, "pointforce %.2f %.2f %.2f %.3f %d %.2f %.2f %d", x, y, z, s, n, r0, r1, int(g));
        log.push_back(t);
    }
    bool has(const std::string& s) const { for (const auto& l : log) if (l == s) return true; return false; }
    int count_prefix(const std::string& p) const { int n = 0; for (const auto& l : log) n += l.rfind(p, 0) == 0; return n; }
};

struct FsLoader : ScriptLoader {
    std::string dir, common;
    std::vector<std::string> requested;
    std::shared_ptr<const Xso> load_script(const std::string& name) override {
        const bool at = !name.empty() && name[0] == '@';
        std::string up = at ? name.substr(1) : name;
        for (char& c : up) c = char(std::toupper((unsigned char)c));
        requested.push_back(up);
        auto b = rd((at ? common : dir) + up + ".XSO");
        if (b.size() < 4) return nullptr;
        try { return std::make_shared<Xso>(Xso::parse(b)); } catch (const std::exception&) { return nullptr; }
    }
};

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    fails += !ok;
}

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : "assets";
    const std::string common = root + "/MAP/S_COMMON/";

    // 1) None of the ops this pass models reaches the VM's unhandled fallthrough.
    {
        const int subs[] = {0x40, 0x7f, 0x80, 0x95, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa1, 0xac, 0xad, 0xae, 0xb2,
                            0xb5, 0xbd, 0xbe, 0xd0, 0x11f, 0x14d, 0x15c, 0x15e, 0x15f, 0x163, 0x164, 0x165, 0x16f, 0x17c};
        int bad = 0;
        for (int s : subs) {
            Asm a;
            std::initializer_list<Asm::A> ops = {Asm::I(1), Asm::I(1), Asm::I(1), Asm::I(1),
                                                 Asm::I(1), Asm::I(1), Asm::I(1), Asm::I(1)};
            a.op(s, ops);
            a.raw(0x01000000);
            const Xso x = Xso::parse(a.bytes());
            H h; XsoVm vm; vm.host = &h;
            bool un = false;
            vm.on_unhandled = [&](std::uint32_t) { un = true; };
            vm.run(x);
            if (un) { ++bad; std::printf("  0x%x still unhandled\n", s); }
        }
        check(bad == 0, "28 floor 1..3 ops are modelled (no on_unhandled)");
    }

    // 2) Reg_SetInt + Reg_IsEqualInt: the @Break_* drop guard.
    {
        Asm a;
        a.op(0x40, {Asm::I(66)});                   // acc = 66
        a.op(0x66, {Asm::I(300)});                  // flag 300 = acc
        a.op(0x40, {Asm::I(0)});
        a.op(0x3c, {Asm::I(0)});                    // acc = (0 == 0)
        a.op(0x66, {Asm::I(301)});
        a.raw(0x01000000);
        XsoVm vm; vm.run(Xso::parse(a.bytes()));
        check(vm.g_flags[300] == 66 && vm.g_flags[301] == 1, "Reg_SetInt loads acc; Reg_IsEqualInt compares it");
    }

    // 3) Reg_EntryLabel: case-insensitive compare with the door label, 0 on a match.
    {
        Asm a;
        a.op(0x80, {a.S("S_3015A")}); a.op(0x66, {Asm::I(300)});
        a.op(0x80, {a.S("S_3015B")}); a.op(0x66, {Asm::I(301)});
        a.raw(0x01000000);
        const Xso x = Xso::parse(a.bytes());
        H h; h.label = "s_3015a"; XsoVm vm; vm.host = &h; vm.run(x);
        check(vm.g_flags[300] == 0 && vm.g_flags[301] != 0, "Reg_EntryLabel = _stricmp(label, 0x76a474)");
    }

    // 4) The shipped S_3015 root: Yunica's first-visit hint (EV_3015_Yunica_1) only when she came in
    //    by the S_3009 door (label S_3015A), not by the S_3008 one (S_3015B).
    {
        auto run_root = [&](const std::string& label) {
            FsLoader ld; ld.dir = root + "/MAP/S_30/S_3015/"; ld.common = common;
            H h; h.label = label;
            ScriptManager mgr; mgr.loader = &ld; mgr.vm.host = &h;
            mgr.vm.new_game(1);
            mgr.vm.g_flags[251] = 1;                // the precondition the root tests before the label
            mgr.start_root(std::make_shared<Xso>(Xso::parse(rd(ld.dir + "S_3015.XSO"))), "S_3015");
            for (int t = 0; t < 3; ++t) mgr.tick_all();
            for (const auto& r : ld.requested) if (r == "EV_3015_YUNICA_1") return true;
            return false;
        };
        const bool a = run_root("S_3015A"), b = run_root("S_3015B");
        std::printf("  S_3015 via S_3015A -> EV_3015_Yunica_1 %d; via S_3015B -> %d\n", int(a), int(b));
        check(a && !b, "S_3015 picks its hint event by the entry door");
    }

    // 5) @Break_30 on the shipped script: on land SE 114 (under water SE 779 + three effects 61 on
    //    the mesh), and its drops roll.
    //    Before Reg_SetInt was modelled the guard read a stale acc and skipped every drop.
    {
        const Xso brk = Xso::parse(rd(common + "BREAK_30.XSO"));
        int drops = 0, runs = 0, attach_ok = 0;
        XsoVm vm;
        for (int i = 0; i < 200; ++i) {
            H h; vm.host = &h; vm.vars = {}; vm.vars.s[0] = "break_01"; vm.vars.i[0] = 66; vm.vars.i[1] = 67;
            vm.run(brk);
            ++runs;
            drops += h.count_prefix("drop break_01 ");
            attach_ok += h.count_prefix("attach ") == 0 && h.has("se 114");
        }
        H h0; vm.host = &h0; vm.vars = {}; vm.vars.s[0] = "break_01";
        int zero_drops = 0;
        for (int i = 0; i < 50; ++i) { h0.log.clear(); vm.run(brk); zero_drops += h0.count_prefix("drop "); }
        // Under water (IsUnderWaterWorld): SE 779 and the three effects 61 instead of SE 114.
        struct HW : H { bool is_underwater_world() const override { return true; } } hw;
        vm.host = &hw; vm.vars = {}; vm.vars.s[0] = "break_01"; vm.run(brk);
        const bool wet = hw.count_prefix("attach break_01 '' 61") == 3 && hw.has("se 779") && !hw.has("se 114");
        std::printf("  BREAK_30: %d runs, %d drops (2 rolls at 50%%), land SE %d/%d, drops with no ids %d, water %d\n",
                    runs, drops, attach_ok, runs, zero_drops, int(wet));
        check(attach_ok == runs && drops > 120 && drops < 280 && zero_drops == 0 && wet,
              "@Break_30 rolls its two drops at 50%; under water it attaches 3x effect 61");
    }

    // 6) AllowMultiInstance: two @Break_30 copies live together; a plain script stays single.
    {
        FsLoader ld; ld.dir = root + "/MAP/S_10/S_1001/"; ld.common = common;
        H h; ScriptManager mgr; mgr.loader = &ld; mgr.vm.host = &h;
        XsoVm::Vars v1, v2; v1.s[0] = "a"; v2.s[0] = "b";
        const bool s1 = mgr.start("@break_30", v1, 13), s2 = mgr.start("@break_30", v2, 13);
        int live = 0; for (const auto& i : mgr.live) live += !i.finished;
        const bool t1 = mgr.start("@Touch_Toge", {}, 13);      // no waits: ends in its first burst
        std::printf("  break_30 x2 started %d/%d live %d; touch_toge %d\n", int(s1), int(s2), live, int(t1));
        check(s1 && s2 && live == 2, "AllowMultiInstance(1) lets a second copy start");
    }

    // 7) Chr_Damage from the shipped touch scripts: Touch_Toge reads "$I0" as the float atk.
    {
        H h; XsoVm vm; vm.host = &h; vm.vars.i[0] = 120;
        vm.run(Xso::parse(rd(common + "TOUCH_TOGE.XSO")));
        vm.run(Xso::parse(rd(root + "/MAP/S_10/S_1001/TOUCH_KEKKAI.XSO")));
        check(h.has("damage 998 120.0 0 0") && h.has("damage 998 50.0 0 0"), "Chr_Damage(998, $I0 / 50)");
    }

    // 8) SetWeaponLevel from the shipped WEAPON_LEVEL_UP: tier 0 -> 1 for all four slots.
    {
        H h; XsoVm vm; vm.host = &h;
        vm.run(Xso::parse(rd(common + "WEAPON_LEVEL_UP.XSO")));
        check(h.has("weapon 0 1") && h.has("weapon 3 1") && vm.g_flags[148] == 1, "SetWeaponLevel 0 -> 1");
    }

    // 9) The pushes from the shipped S_2014 / S_1013 scripts.
    {
        H h; XsoVm vm; vm.host = &h;
        const Xso yoko = Xso::parse(rd(root + "/MAP/S_20/S_2014/EVTWINDYOKO.XSO"));
        vm.co_begin(yoko);
        vm.tick();
        for (const auto& l : h.log) std::printf("  %s\n", l.c_str());
        H h2; XsoVm vm2; vm2.host = &h2;
        vm2.g_flags[165] = 1;                       // the pit pulls once flag 165 is set (else it loops idle)
        const Xso ari = Xso::parse(rd(root + "/MAP/S_10/S_1013/EVTWINDARIZIGO.XSO"));
        vm2.co_begin(ari);
        vm2.tick();
        for (const auto& l : h2.log) std::printf("  %s\n", l.c_str());
        check(h.has("boxforce STREAM_1 0.075 0.000 0.000 4 0") && h.has("boxforce STREAM_2 -0.075 0.000 0.000 4 0") &&
                  h2.count_prefix("pointforce 0.00 0.00 8.00 ") == 1,
              "ChrAllInEventBox_SetForce / ChrAll_SetPointForce operands");
    }

    // 10) The host-forwarded rest, on a synthetic script.
    {
        Asm a;
        a.op(0x14d, {Asm::I(196), Asm::I(1)});
        a.op(0x165, {Asm::I(198), Asm::I(0)});
        a.op(0x163, {Asm::I(998), Asm::I(1)});
        a.op(0x164, {Asm::I(197), Asm::I(1)});
        a.op(0xd0, {Asm::I(5)});
        a.op(0xbd, {Asm::I(600), Asm::F(1.5f), Asm::F(2.0f), Asm::F(-3.0f)});
        a.op(0xbe, {Asm::I(697), Asm::I(203)});
        a.op(0x11f, {a.S("eft_x"), Asm::F(0.3f)});
        a.op(0x16f, {a.S("plate"), Asm::F(0.0f)});
        a.op(0x95, {Asm::F(0.5f)});
        a.op(0xb2, {Asm::I(20)});
        a.raw(0x01000000);
        H h; h.wallet = 999990; XsoVm vm; vm.host = &h; vm.run(Xso::parse(a.bytes()));
        H h2; h2.wallet = 5;
        Asm b; b.op(0xb2, {Asm::I(-50)}); b.raw(0x01000000);
        XsoVm vm2; vm2.host = &h2; vm2.run(Xso::parse(b.bytes()));
        check(h.has("vanish 196 1") && h.has("stop 198") && h.has("noforce 998 1") && h.has("anidisable 197 1") && h.has("tilt 5") && h.has("se3d 600 1.50 2.00 -3.00") &&
                  h.has("sechr 697 203") && h.has("fxalpha eft_x 0.30") && h.has("meshalpha plate 0.00") &&
                  h.has("timescale 0.50") && h.wallet == 999999 && h2.wallet == 0,
              "CollisionVanish / Stop / PadTilt / Se_Play3D* / SetAlphaCurrent / SetTimeScale / AddPlayerSP clamp");
    }

    // 11) Flag_Clr is 0x7a in the static grant scan (0x77 is Flag_AndFlagShl).
    {
        Asm a; a.op(0x7a, {Asm::I(200)}); a.raw(0x01000000);
        const auto g = Xso::parse(a.bytes()).grants();
        check(g.size() == 1 && g[0].op == "zero" && g[0].index == 200, "0x7a Flag_Clr = zero");
    }

    // 12) XsoVm::run's skip_cycles ends a script in the state the full run does: every script of S_4001 (its
    //     four ROLL_EDGE movers loop forever), as the three characters. CLERIA_XSO_CYCLE_ALL=1: every MAP
    //     script (2225 files, 242 of them endless as Yunica; half a minute — all equal, 2026-10-10).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(root) / "MAP" / (std::getenv("CLERIA_XSO_CYCLE_ALL") ? "" : "S_40/S_4001");
        auto as_set = [](std::vector<std::string> v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); return v; };
        int files = 0, endless = 0, bad = 0;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (!e.is_regular_file() || ext != ".XSO") continue;
            Xso x;
            try { x = Xso::parse(rd(e.path().string())); } catch (const std::exception&) { continue; }
            ++files;
            for (int ch = 1; ch <= 3; ++ch) {
                XsoVm a, b;
                a.g_flags[150] = b.g_flags[150] = ch;
                const int na = a.run(x), nb = b.run(x, 0, 2'000'000, true);
                if (ch == 1 && na == 2'000'000) ++endless;
                auto place = [](const XsoVm& v) {
                    std::vector<std::string> o;
                    for (const auto& [n, p] : v.obj_place) o.push_back(n + ":" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z));
                    for (const auto& [n, s] : v.obj_sync) o.push_back(n + "<" + s.from + char('0' + s.x) + char('0' + s.y) + char('0' + s.z));
                    for (const auto& l : v.obj_locate) o.push_back(l.name + "@" + l.ymo + std::to_string(l.x) + "," + std::to_string(l.y) + "," + std::to_string(l.z));
                    std::sort(o.begin(), o.end()); o.erase(std::unique(o.begin(), o.end()), o.end());
                    return o;
                };
                const bool same = na == nb && a.acc == b.acc && std::memcmp(a.g_flags, b.g_flags, sizeof a.g_flags) == 0 &&
                                  std::memcmp(a.item_arr, b.item_arr, sizeof a.item_arr) == 0 && a.locals == b.locals &&
                                  std::memcmp(a.rs, b.rs, sizeof a.rs) == 0 && a.obj_state == b.obj_state &&
                                  a.obj_visible == b.obj_visible && a.obj_collide == b.obj_collide &&
                                  as_set(a.obj_deleted) == as_set(b.obj_deleted) && as_set(a.obj_opened) == as_set(b.obj_opened) &&
                                  place(a) == place(b);
                if (!same) { ++bad; std::printf("  skip_cycles differs: %s (char %d, %d / %d steps)\n", e.path().filename().string().c_str(), ch, na, nb); }
            }
        }
        std::printf("  skip_cycles: %d scripts, %d endless\n", files, endless);
        check(files > 0 && endless > 0 && bad == 0, "skip_cycles ends every script in the full run's state");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
