// cleria-dueltest: the story duels' script / engine side (build/event_re/DUELS.md): XSO op 0x111
// FireBullet's operand decode (FUN_00571500 -> FUN_00593550), the shipped S_3080 FireBulletEvent /
// FireBulletEvent2 shots flown through the GoXx bullet, the losable-duel death (FUN_0046f810,
// flag 133), and the duel scripts' flag hand-offs on the shipped S_1080 / S_3080 scripts.
// Usage: cleria-dueltest <assets>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "cleria/bullet.hpp"
#include "cleria/duel.hpp"
#include "cleria/ef2.hpp"
#include "cleria/ef2_sim.hpp"
#include "cleria/xso.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[duel] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

static std::vector<std::uint8_t> rd(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

// A synthetic XSR builder with typed operand descriptors (the cleria-xso13test one).
struct Asm {
    std::vector<std::int32_t> code;
    struct A { int t; std::int32_t v; };
    static A I(std::int32_t v) { return {0, v}; }
    static A F(float f) { std::int32_t w; std::memcpy(&w, &f, 4); return {1, w}; }
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
        const std::uint32_t words = std::uint32_t(code.size()), cnt = 0;
        std::memcpy(b.data() + 0x1C, &words, 4);
        std::memcpy(b.data() + 0x20, &cnt, 4);
        const auto* cp = reinterpret_cast<const std::uint8_t*>(code.data());
        b.insert(b.end(), cp, cp + code.size() * 4);
        return b;
    }
};

struct H : XsoHost {
    std::vector<FireBulletOp> shots;
    void spawn(int, int, float, float, float, float, int) override {}
    bool is_alive(int) const override { return false; }
    void fire_bullet(const FireBulletOp& b) override { shots.push_back(b); }
};

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-dueltest <assets>\n"); return 2; }
    const std::filesystem::path root = argv[1];
    const std::string s3080 = (root / "MAP" / "S_30" / "S_3080").string() + "/";

    // 1) 0x111 on a synthetic script: every operand lands in FUN_00593550's slot; 998 is the
    //    script's own actor; the int operands 14..16 keep their bits in the float block.
    {
        Asm a;
        a.op(0x111, {Asm::I(998), Asm::I(2), Asm::I(308), Asm::F(1.0f), Asm::F(2.0f), Asm::F(3.0f), Asm::F(0.0f),
                     Asm::F(-0.5f), Asm::F(2.0f), Asm::F(0.25f), Asm::F(0.01f), Asm::F(0.002f), Asm::F(7.0f),
                     Asm::F(-3.0f), Asm::I(400), Asm::I(9), Asm::I(3), Asm::F(0.5f), Asm::F(1.0f), Asm::F(4.0f),
                     Asm::F(60.0f), Asm::F(1.0f)});
        a.raw(0x01000000);
        H h; XsoVm vm; vm.host = &h; vm.owner = 55;
        bool un = false;
        vm.on_unhandled = [&](std::uint32_t) { un = true; };
        vm.run(Xso::parse(a.bytes()));
        bool ok = !un && h.shots.size() == 1;
        if (ok) {
            const FireBulletOp& b = h.shots[0];
            std::uint32_t i14, i15, i16;
            std::memcpy(&i14, &b.f[5], 4); std::memcpy(&i15, &b.f[6], 4); std::memcpy(&i16, &b.f[7], 4);
            ok = b.actor == 55 && b.kind == 2 && b.effect == 308 && b.pos[0] == 1.0f && b.pos[1] == 2.0f &&
                 b.pos[2] == 3.0f && b.dir[0] == 0.0f && b.dir[1] == -0.5f && b.dir[2] == 2.0f && b.f[0] == 0.25f &&
                 b.f[1] == 0.01f && b.f[2] == 0.002f && b.f[3] == 7.0f && b.f[4] == -3.0f && i14 == 400 &&
                 i15 == 9 && i16 == 3 && b.f[8] == 0.5f && b.f[9] == 1.0f && b.f[10] == 4.0f && b.f[11] == 60.0f &&
                 b.f[12] == 1.0f;
            const bullet::Spawn s = bullet::block_spawn(b.kind, b.effect, {b.pos[0], b.pos[1], b.pos[2]},
                                                        {b.dir[0], b.dir[1], b.dir[2]}, b.f);
            ok = ok && s.speed == 0.25f && s.power == 7.0f && s.knock == -3.0f && s.f158 == 400 && s.f15c == 9 &&
                 s.f160 == 3 && s.f164 == 0.5f && s.f168 == 1.0f && s.f16c == 4.0f && s.f170 == 60.0f && s.f174 == 1.0f;
        }
        check(ok, "0x111: 22 operands -> FUN_00593550 (actor, kind, effect, &pos, &dir, block)");
        Asm b; b.op(0x111, {Asm::I(200), Asm::I(2), Asm::I(308)}); b.raw(0x01000000);
        H h2; XsoVm vm2; vm2.host = &h2; vm2.run(Xso::parse(b.bytes()));
        check(h2.shots.empty(), "0x111 with fewer than 22 operands fires nothing");
    }

    // 2) The shipped FireBulletEvent (Hugo's EV_Feena02_2) and FireBulletEvent2 (Toal's Battle2):
    //    six kind-2 shots of EF2 308 from (-12, -3, 33), un-normalised directions.
    const float dirs[6][2] = {{0.0f, -0.19f}, {0.1f, -0.18f}, {-0.05f, -0.15f}, {0.05f, -0.14f}, {-0.1f, -0.13f}, {0.15f, -0.17f}};
    const float speeds[6] = {0.3f, 0.3f, 0.4f, 0.4f, 0.4f, 0.5f};
    std::vector<FireBulletOp> ev1;
    for (int k = 0; k < 2; ++k) {
        const auto bytes = rd(s3080 + (k == 0 ? "FIREBULLETEVENT.XSO" : "FIREBULLETEVENT2.XSO"));
        if (bytes.size() < 4) { std::fprintf(stderr, "missing S_3080 scripts under %s\n", s3080.c_str()); return 2; }
        H h; XsoVm vm; vm.host = &h; vm.run(Xso::parse(bytes));
        bool ok = h.shots.size() == 6;
        const int actors2[6] = {197, 197, 197, 200, 197, 197};
        for (std::size_t i = 0; ok && i < 6; ++i) {
            const FireBulletOp& b = h.shots[i];
            ok = b.actor == (k == 0 ? 200 : actors2[i]) && b.kind == 2 && b.effect == 308 && near(b.pos[0], -12.0f) &&
                 near(b.pos[1], -3.0f) && near(b.pos[2], 33.0f) && near(b.dir[0], dirs[i][0]) &&
                 near(b.dir[1], dirs[i][1]) && near(b.dir[2], 1.0f) && near(b.f[0], speeds[i]) && b.f[3] == 0.0f &&
                 b.f[4] == 0.0f;
        }
        check(ok, k == 0 ? "FireBulletEvent: 6 shots from Shion (200), kind 2, EF2 308"
                         : "FireBulletEvent2: 5 from Hugo (197) + the 4th from Shion (200)");
        if (k == 0) ev1 = h.shots;
    }

    // 3) Those shots through the GoXx bullet (BULLETS.md §3.2 kind 2 -> §3.1 kind 1): the direction
    //    is not normalised at the spawn, so the first INTEGRATE makes speed = 0.3 * |dir| and dir unit;
    //    with homing period 0 the shot flies straight on.
    {
        const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / "ENEMY" / "M_840_TAMA01.EF2");
        if (!ef.ok()) { std::fprintf(stderr, "missing M_840_TAMA01.EF2\n"); return 2; }
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        bullet::Bullets bs(&fx, [&](int id) -> const Ef2* { return id == 308 ? &ef : nullptr; });
        const FireBulletOp& fb = ev1[0];
        bullet::Spawn s = bullet::block_spawn(fb.kind, fb.effect, {fb.pos[0], fb.pos[1], fb.pos[2]},
                                              {fb.dir[0], fb.dir[1], fb.dir[2]}, fb.f);
        s.owner = 200; s.owner_monster = true; s.owner_atk = 115;
        bullet::Bullet* b = bs.spawn(s);
        bool ok = b && b->fx && b->side == 1 && b->fx->atk == 115.0f && b->fx->power == 0.0f;
        bullet::Env env;
        env.player = {-13.1f, -3.0f, 68.8f};
        std::vector<bullet::Event> evs;
        if (ok) { bs.update(env, &evs); fx.step(); }
        const float len = std::sqrt(0.19f * 0.19f + 1.0f);
        ok = ok && near(b->s.speed, 0.3f * len, 1e-5f) && near(b->s.pos.z, 33.3f, 1e-4f) &&
             near(b->s.pos.y, -3.0f - 0.3f * 0.19f, 1e-4f) && near(b->s.dir.z, 1.0f / len, 1e-5f);
        const float z1 = ok ? b->s.pos.z : 0.0f;
        for (int t = 0; ok && t < 10; ++t) { bs.update(env, &evs); fx.step(); }
        ok = ok && near(b->s.pos.z - z1, 10.0f * 0.3f, 1e-3f) && near(b->s.pos.x, -12.0f, 1e-5f);
        std::printf("  shot 1 after 11 ticks: (%.3f, %.3f, %.3f) speed %.4f\n", b ? b->s.pos.x : 0.f, b ? b->s.pos.y : 0.f,
                    b ? b->s.pos.z : 0.f, b ? b->s.speed : 0.f);
        check(ok, "the scripted shot: side 1, owner ATK, power 0; speed 0.3 x |dir| then straight");
    }

    // 4) The losable duel (FUN_0046f810, flag 133).
    {
        std::int32_t f[0x200] = {0};
        f[133] = 1; f[156] = 1; f[130] = 1; f[157] = 1; f[158] = 150; f[174] = 0;
        int hp = -12, ifr = 0;
        const bool lost = duel::losable_death(f, hp, ifr);
        check(lost && f[156] == 0 && f[130] == 0 && f[157] == 0 && f[158] == 0 && f[174] == 0 && f[133] == 1 &&
                  hp == 1 && ifr == 9999,
              "133 set: the death ends the duel (156 / 130 / 157 / 158 = 0, HP 1, 9999), 174 kept");
        std::int32_t g[0x200] = {0};
        g[156] = 1; g[130] = 1;
        int hp2 = 0, ifr2 = 3;
        check(!duel::losable_death(g, hp2, ifr2) && g[156] == 1 && hp2 == 0 && ifr2 == 3,
              "133 clear: nothing written (the game over runs)");
    }

    // 5) The shipped duel scripts hand off on the flags the brains / FUN_0046f810 write: every
    //    duel parks on Wait_FlagEqualInt(156, 0); the S_3080 duels Yunica / Hugo are losable
    //    (133 = 1 around the park, cleared after), Toal's Battle2 and all of S_1080 are not.
    {
        struct Case { const char* dir; const char* file; bool losable; };
        const Case cs[] = {{"S_10/S_1080", "BATTLE_1.XSO", false}, {"S_10/S_1080", "BATTLE_HUGO.XSO", false},
                           {"S_10/S_1080", "BATTLE2.XSO", false},  {"S_30/S_3080", "BATTLE_0.XSO", true},
                           {"S_30/S_3080", "BATTLE.XSO", true},    {"S_30/S_3080", "BATTLE2.XSO", false}};
        bool ok = true;
        for (const Case& c : cs) {
            const auto bytes = rd((root / "MAP" / c.dir / c.file).string());
            if (bytes.size() < 4) { ok = false; std::printf("  missing %s/%s\n", c.dir, c.file); continue; }
            const Xso x = Xso::parse(bytes);
            bool park = false, set133 = false;
            for (const XsoInstr& in : x.disasm()) {
                if (in.cls != 2 || in.ops.size() < 2) continue;
                if (in.sub == 0xf8 && in.ops[0] == 156 && in.ops[1] == 0) park = true;
                if (in.sub == 0x64 && in.ops[0] == 133 && in.ops[1] == 1) set133 = true;
            }
            std::printf("  %s/%s: park on 156 %d, 133 = 1 %d\n", c.dir, c.file, int(park), int(set133));
            ok = ok && park && set133 == c.losable;
        }
        check(ok, "the six duel scripts park on 156 = 0; only S_3080 Battle_0 / Battle set 133");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
