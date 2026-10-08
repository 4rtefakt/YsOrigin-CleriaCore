// M870 Zava, the floor-6 boss's actor (cast 0xad, AI index 80) — handler FUN_0053af10, brain FUN_0053ab20,
// phase 1 FUN_00537160, phase 2 FUN_00538820, death FUN_0053a760, ride FUN_0053aa90 and their helpers.
// Spec: build/boss_re/ZAVA_AB.md ("AB §n": slices A and B) and ZAVA_CG.md slice C ("CG §n"); the interface and the
// reason she is an EnemyBrain with a side link are in src/cleria/boss06_zava.hpp.
//
// Phase 1 she floats to points near the player (action 1), hovers 30 frames (4) and then calls lightning (8), fires
// 2..3 volleys of 3 (Nightmare 7) shots (6) or re-positions; she dodges Hugo's Eyes and Yunica's / Toal's skills
// with a dash (7); the first damaging hit while airborne (the fourth on Nightmare) knocks her down for 120 frames
// (10 / 11) while the heads chase. Below 70 % HP phase 2: she flies a spline onto a head (action 0), rides it
// behind a 4-life shield, is thrown off, re-positions (7), then has the other head stamp the floor (8) or, below
// 25 % HP, runs the big attack (20): fog, suction, smoke rings, the blast at frame 360.
//
// The raw AI block carries her fields at the engine's offsets (AB §0.1). Outside it: Z+0x50 = the actor's ORIGIN
// (Actor::pos + height), +0x60 & 0x80 = EnemyAi::hittable (and Actor::kind), +0x318 = floor_y, +0x338.. = mover.k1,
// +0x388 = vert.grounded, +0x398 = flyer, +0x399 = guard399, +0x3fc = EnemyAi::dead_t, +0x4dc / +0x4e0 = alpha_to /
// alpha_step, +0x68 / +0x78 / +0x7c = hurt_radius / hurt_half_h / hurt_shape (ZAVA_E2 §2's reading), +0x7c0 (the
// mounted head) = 0 none, 1 head a, 2 head b, +0x7a8 = Boss06Zava::hits, +0x88c = an effect handle.
//
// [H] every effect spawn is taken to succeed (the engine's `if fx` tests pass); the handles are module ints.
// [not modelled] FUN_0045f380 after the action player while flying (AB G1); the byte Z+0x64 = 0x2f; the stack
// overrun of Eyes() with more than 4 Eyes (AB §0); the culled path's KeepOut / tail when enemy_ai_tick's own cull
// test returns first; RNG words drawn inside the effect / bullet / damage code; the engine's null-mesh dereferences
// (a missing head makes every head access a no-op here).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/boss06_zava.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

using boss06::kBodyEfc;

constexpr float kPi = 3.14159274f;                                 // 0x6bd6ac
const Vec3 kC0{-34.0f, 6.0f, 29.0f};                               // CG §0: the arena centre
const Vec3 kC1{-34.0f, 6.0f + 0.01f, 29.0f};                       // C0.y + 0.01 (0x6bd084)

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 nrm(const Vec3& a) {                                          // FUN_0042cc50: zero length -> (0, 0, 0)
    const float l = std::sqrt(a.y * a.y + a.x * a.x + a.z * a.z);
    return l == 0.0f ? Vec3{} : mul(a, 1.0f / l);
}
float len_xz2(const Vec3& v) { return v.x * v.x + v.z * v.z; }     // FUN_005354a0
bool in_arena(const Vec3& v) {                                     // FUN_005354f0 (a NaN passes)
    return !(-50.0f > v.x) && !(v.x > -18.0f) && !(19.0f > v.z) && !(v.z > 39.0f);
}
Vec3 origin(const Actor& Z) { return {Z.pos.x, Z.pos.y + Z.height, Z.pos.z}; }   // Z+0x50
void set_origin(Actor& Z, const Vec3& v) { Z.pos = {v.x, v.y - Z.height, v.z}; }

struct Ctx {
    EnemyAi& a; const AiEnv& e; Actor& Z; Boss06Link& L; AiOut& out;
    Boss06* H; const Boss06Input& in; Boss06Output& bo; Boss06ZavaOut& zo;

    // ---- small readers ----
    Vec3 zp() const { return origin(Z); }
    int flag(int n, int fb) const { return e.world ? e.world->flag(n, fb) : fb; }
    void set_flag(int n, int v) { bo.flags.push_back({n, v}); if (n == 156 && v == 0) L.f156_cleared = true; }
    std::uint32_t R(std::uint32_t n) { return (e.rng() >> 17) % n; }
    int diff_level() const {                                       // FUN_00535280
        if (e.diff_very_easy) return 0;
        if (e.diff_easy) return 1;
        return e.diff_index == 1 ? 3 : e.diff_index == 2 ? 4 : 2;
    }
    std::uint32_t t87() const { return std::uint32_t(a.i(0x87c)); }
    std::uint32_t& hits() { return L.z->hits; }                    // Z+0x7a8
    Vec3 rand_lerp(const Vec3& lo, const Vec3& hi) {               // FUN_00535320: 3 draws, x, y, z
        Vec3 o;
        o.x = lo.x + float(R(100)) * (hi.x - lo.x) / 100.0f;
        o.y = lo.y + float(R(100)) * (hi.y - lo.y) / 100.0f;
        o.z = lo.z + float(R(100)) * (hi.z - lo.z) / 100.0f;
        return o;
    }
    Vec3 p_pos() const { return in.player ? in.player_pos : Vec3{}; }   // FUN_00535780

    // ---- the heads (a = 0, b = 1) ----
    Boss06Head& hb(int i) { return H->head(i); }
    void sync_z() {                                                // what the heads read of her
        L.z->present = true; L.z->pos = zp(); L.z->hp = Z.hp; L.z->max_hp = Z.max_hp;
    }
    void abort_move(int i) { sync_z(); H->abort_move(i, in, *L.z, bo); }   // FUN_0053c400
    void start_move(int i, int m) { H->start_move(i, m, bo); }             // FUN_0053c390
    Vec3 body_efc(int i) const {
        const ef2sim::M4& m = in.head[std::size_t(i)].nodes[std::size_t(kBodyEfc)];
        return {m.m[3][0], m.m[3][1], m.m[3][2]};
    }
    int mount() const { return a.i(0x7c0) - 1; }                   // Z+0x7c0 as a head index, -1 = none

    // ---- world requests ----
    void se(int id, const Vec3& p) { bo.sounds.push_back({id, p}); }       // FUN_0043a080
    void kill_id(int id) { bo.kill_ids.push_back(id); }                    // FUN_00451730
    void kill_slot(int id) {                                               // `if (*slot) { Kill(id); *slot = 0 }`
        if (a.i(0x88c) != 0) { kill_id(id); a.i(0x88c) = 0; }
    }
    Boss06Effect& fx(Boss06Call call, int id, const Vec3& p) {             // rot 0, scale 1, src 0, side 1, cls -1
        Boss06Effect f; f.head = -1; f.call = call; f.id = id; f.pos = p;
        f.handle = L.next_handle++;
        bo.effects.push_back(f);
        return bo.effects.back();
    }
    void attach(Boss06Effect& f, Boss06Parent parent, int mode) {          // FUN_00451c20(fx, parent, 0, 0, 1, mode)
        f.parent = parent; f.at_mode = mode;
    }

    // ---- ASO sections ----
    void force(int s) { out.section = s; out.section_force = true; L.section = s; L.requested = true; }   // ForceSection
    void gated(int s) {                                            // FUN_00446550(s, 0)
        if (!ai::request(L.section, s, false, e)) return;
        out.section = s; out.section_force = L.requested;          // gated here against this frame's earlier request
        L.requested = true;
    }
    void pose(int sec, bool forced) {                              // FUN_00536880 (AB §A.6; CG §0 "Anim(sec, soft)")
        if (a.i(0x7bc) != sec) {
            if (forced) force(sec); else gated(sec);
            if (sec == 300) se(397, zp());
            else if (sec == 301) se(392, zp());
        }
        a.i(0x7bc) = sec;
    }
    void go(int action) { a.i(0x728) = action; a.i(0x72c) = 0; a.i(0x87c) = 0; }

    // ---- helpers ----
    std::vector<Vec3> eyes() const {                               // FUN_00535620 (AB §0)
        std::vector<Vec3> v;
        if (!e.world) return v;
        const std::size_t cap = flag(211, 0) != 0 ? 0u : flag(210, 0) != 0 ? 6u : (flag(543, 0) & 0x20) ? 4u : 2u;
        bool full = false;
        e.world->for_each([&](Actor& c) {
            if (full || c.cast_id != 0xb0 || c.ai.i(0x7c8) == 0) return;
            v.push_back(origin(c));
            if (v.size() == cap) full = true;
        });
        return v;
    }
    // FUN_00536940: no listed point closer than r (3-D) to p.
    bool clear(const std::vector<Vec3>& list, std::size_t n, float r, const Vec3& p,
               const std::array<std::optional<Vec3>, 3>* fx3) const {
        auto close_to = [&](const Vec3& q) {
            const Vec3 d = q - p;
            return r > std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        };
        for (std::size_t i = 0; i < n && i < list.size(); ++i) if (close_to(list[i])) return false;
        if (fx3) for (const auto& f : *fx3) if (f && close_to(*f)) return false;
        return true;
    }
    Vec3 dodge_point(const Vec3& centre) {                         // FUN_00536a30 (AB §A.7)
        const bool hugo = e.character == 2;
        std::vector<Vec3> list;
        std::size_t n = 0;
        if (hugo) { list = eyes(); n = std::min<std::size_t>(list.size(), 4); }
        Vec3 best = centre;
        const Vec3 hi{15.0f, 0, 15.0f}, lo{-15.0f, 0, -15.0f};
        for (int i = 0; i < 100; ++i) {
            const Vec3 d = nrm(rand_lerp(lo, hi));
            const float r = float(R(100)) * 3.0f / 100.0f + 5.0f;
            const Vec3 p = add(centre, mul(d, r));
            if (!in_arena(p)) continue;
            best = p;
            if (!hugo) break;
            if (clear(list, n, 5.0f, p, &L.zin.hugo_fx)) break;
        }
        return best;
    }
    bool heads_in_arena() {                                        // FUN_005367c0: b, then a
        if (!H) return true;
        return in_arena(hb(1).pos) && in_arena(hb(0).pos);
    }
    bool knock_test() {                                            // FUN_00536d10 (AB §A.9; inlined at AB §B.1)
        const int f = a.i(0x7a0);
        if (f & 0x14) return false;
        if (f & 8) return true;
        if (a.i(0x7c8) != 0 || Z.guard399 || Z.vert.grounded || !e.took_damage) return false;
        if (a.i(0x8a8) == 0) return true;
        a.i(0x8a8) -= 1;
        return a.i(0x8a8) == 0;
    }
    void getup_shove() {                                           // FX2 311 m_190_attack01 [0x536f8f, 0x5392d6]
        const Vec3 p = zp();
        Boss06Effect& f = fx(Boss06Call::kFx2, 311, {p.x, p.y - Z.height + 0.05f, p.z});
        f.scale = {1.5f, 1.5f, 1.5f}; f.stat = true; f.power = 0.0f; f.knock = 6.0f; f.rtype = 2;
        a.i(0x88c) = f.handle;                                     // phase 2 keeps the handle; AB §A.8 does not
    }

    bool knockdown();                                              // FUN_00536d60
    void spline_build(const Vec3 pts[4]);                          // FUN_00536170
    Vec3 spline_eval(float u) const;                               // FUN_00536580
    void pull_in(int i);                                           // FUN_0053a5e0
    void death();                                                  // FUN_0053a760
    void keep_out();
    void init(const EnemyAiParams& p);
    void brain(const EnemyAiParams& p);                            // FUN_0053ab20
    void phase1();                                                 // FUN_00537160
    void phase2();                                                 // FUN_00538820
    void strike(const Vec3& T);
};

// FUN_00536d60 (AB §A.8): the knock-down, on the caller's step Z+0x72c.
bool Ctx::knockdown() {
    switch (a.i(0x72c)) {
        case 0: {                                                  // 0x536d8c
            kill_slot(586);                                        // the cast circle
            se(398, zp());                                         // zako\z_zaba08 (fall)
            Z.flyer = false;
            a.i(0x7a0) = (a.i(0x7a0) & int(0xffffffbe)) | 0x10;
            a.f(0x814) = 0; a.i(0x8a8) = 0; a.i(0x708) = 1; a.i(0x710) = 1;
            const Vec3 p = zp();
            a.f(0x894) = p.x; a.f(0x89c) = p.z; a.f(0x898) = 6.0f;
            a.i(0x72c) += 1;
            a.f(0x834) = 0; a.f(0x83c) = 0.05f;
            [[fallthrough]];
        }
        case 1:                                                    // 0x536e33
            if (!Z.vert.grounded) return false;
            se(399, zp());                                         // zako\z_zaba09 (thud)
            force(9);
            a.i(0x72c) += 1; a.i(0x87c) = 0;
            [[fallthrough]];
        case 2:                                                    // 0x536eb7
            if (t87() < std::uint32_t(int(a.f(0x7ac) * 120.0f))) return false;
            a.i(0x7a0) &= int(0xffffffef);
            a.i(0x7c8) = 60;
            {
                const int keep = a.i(0x88c);
                getup_shove();
                a.i(0x88c) = keep;                                 // AB §A.8: the handle is not stored
            }
            se(167, zp());                                         // player\p3_slashex00a
            return true;
        default: return true;
    }
}

// FUN_00536170 (CG §C.9): a natural cubic spline through 4 knots; `count` is not read, n = 3.
void Ctx::spline_build(const Vec3 pts[4]) {
    auto& A = L.sa; auto& B = L.sb; auto& C = L.sc; auto& D = L.sd;
    const int n = 3;
    L.sn = n;
    for (int i = 0; i <= n; ++i) A[std::size_t(i)] = pts[i];
    C[3] = {}; C[0] = C[3];
    auto comp = [](Vec3& v, int k) -> float& { return k == 0 ? v.x : k == 1 ? v.y : v.z; };
    for (std::size_t i = 1; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            comp(C[i], k) = 3.0f * (comp(A[i - 1], k) - (comp(A[i], k) + comp(A[i], k)) + comp(A[i + 1], k));
    Vec3 w[4] = {};
    for (std::size_t i = 1; i < 3; ++i)
        for (int k = 0; k < 3; ++k) {
            const float m = 4.0f - comp(w[i - 1], k);
            comp(C[i], k) = (comp(C[i], k) - comp(C[i - 1], k)) / m;
            comp(w[i], k) = float(1.0 / double(m));
        }
    for (std::size_t i = 2; i >= 1; --i)
        for (int k = 0; k < 3; ++k) comp(C[i], k) = comp(C[i], k) - comp(C[i + 1], k) * comp(w[i], k);
    D[3] = {}; B[3] = D[3];
    for (std::size_t i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) {
            comp(D[i], k) = (comp(C[i + 1], k) - comp(C[i], k)) * 0.333333343f;
            comp(B[i], k) = (comp(A[i + 1], k) - comp(A[i], k)) - comp(C[i], k) - comp(D[i], k);
        }
}

// FUN_00536580.
Vec3 Ctx::spline_eval(float u) const {
    float s = float(L.sn) * u;
    int k = int(std::floor(s));                                    // FUN_004217f0 [H: floor]
    if (k < 0) k = 0; else if (k >= L.sn) k = L.sn - 1;
    s -= float(k);
    const std::size_t i = std::size_t(k);
    auto ev = [&](float d, float c, float b, float a0) { return ((d * s + c) * s + b) * s + a0; };
    return {ev(L.sd[i].x, L.sc[i].x, L.sb[i].x, L.sa[i].x), ev(L.sd[i].y, L.sc[i].y, L.sb[i].y, L.sa[i].y),
            ev(L.sd[i].z, L.sc[i].z, L.sb[i].z, L.sa[i].z)};
}

// FUN_0053a5e0 (AB §A.4): pull a head back into the arena, 0.5 a step toward (-34, 6, 29), at most 100 steps.
void Ctx::pull_in(int i) {
    Vec3& p = hb(i).pos;
    if (in_arena(p)) return;
    const Vec3 saved = p;
    const Vec3 s = mul(nrm({-34.0f - p.x, 6.0f - p.y, 29.0f - p.z}), 0.5f);
    for (int k = 0; k < 100; ++k) {
        p = add(p, s);
        if (in_arena(p)) return;
    }
    p = saved;
}

// FUN_0053a760 (AB §A.3): the death, on Z+0x3fc. It advances +0x87c itself.
void Ctx::death() {
    switch (a.dead_t) {
        case 0: {                                                  // 0x53a7de
            bo.rumble.push_back(5);
            if (in.player) zo.player_unlock = true;
            set_flag(134, 0);
            zo.gauge_zero = true;
            zo.gauge_hide = true;                                  // dword [0x6d5fd8] = 0
            if (e.world) e.world->hide_boss_gauge();
            set_flag(544, 0); set_flag(545, 0); set_flag(546, 0);  // [0x76c19c] / [0x76c1a0] / [0x76c1a4]
            a.mode() = 0; a.i(0x708) = 1; a.f(0x814) = 0;
            Z.mover.k1 = {}; Z.mover.vx = 0; Z.mover.vz = 0;       // +0x338..0x340, +0x32c, +0x334
            Z.mover.dx = 0; Z.mover.dz = 0;                        // +0xa3c, +0xa44
            a.hit_cut = true;                                      // Z+0x118 = 0
            if (L.section != 5) force(5);
            kill_id(586); kill_id(270);
            a.i(0x87c) = 0;
            zo.boss_cam.push_back(6);
            zo.f6d6078 = true;
            zo.radial = std::pair<float, float>{1.0f, -0.025f};
            if (H) {
                for (int i = 0; i < 2; ++i) {                      // (hbA, meshA), then (hbB, meshB)
                    if (hb(i).pattern == 0xff) continue;
                    hb(i).collide = false;                         // mesh+0x60 &= 0xffffff7f
                    pull_in(i);
                    abort_move(i);
                    hb(i).queued = 0xff;                           // the death pattern
                }
            }
            if (in.player)
                zo.player_heading = float(std::atan2(double(-a.f(0x73c)), double(-a.f(0x734))));
            a.dead_t += 1;
            [[fallthrough]];
        }
        case 1:                                                    // 0x53aa43
            a.i(0x87c) += 1;
            if (t87() < 80u) return;
            zo.boss_cam.push_back(7);
            a.dead_t += 1;
            [[fallthrough]];
        case 2:                                                    // 0x53aa5f
            if (H && hb(0).step < 0x80) return;
            if (H && hb(1).step < 0x80) return;
            set_flag(156, 0); set_flag(130, 0);
            return;
        default: return;
    }
}

// The keep-out restore inlined at 0x53b25b and 0x53b361 (AB §A.1), prev = Z+0x808..0x810.
void Ctx::keep_out() {
    const auto& inside = L.zin.keep_out;
    if (!inside) return;
    const Vec3 pos = zp(), prev = a.v(0x808);
    if (!inside(pos)) return;
    const Vec3 t1{(pos.x - prev.x) + prev.x, prev.y, prev.z};
    if (!inside(t1)) { set_origin(Z, t1); return; }
    const Vec3 t2{prev.x, prev.y, prev.z + (pos.z - prev.z)};
    set_origin(Z, inside(t2) ? prev : t2);
}

// INIT, Z+0x6f0 == 0 (AB §A.2, 0x53accb..0x53aef6).
void Ctx::init(const EnemyAiParams& p) {
    ai::init(a, p, e);                                             // FUN_004782f0; +0x814..0x853 = ENEMYINFO[77]+0x90..
    a.i(0x710) = -1; a.phase() = 1; a.i(0x79c) = 0;
    Z.flags12c = (Z.flags12c & 0xffffffbfu) | 0x280000u;
    Z.kind = 0x88; a.hittable = true;                              // Z+0x60; [not modelled] Z+0x64 = 0x2f
    a.f(0x820) = 1.0f; a.f(0x81c) = 1.0f;
    if (L.zin.eex5) L.zin.eex5(Z);                                 // FUN_00443b80(5, &Z+0x94)
    Z.def = 252.0f; Z.max_hp = 6000.0f; Z.hp = 6000.0f; Z.atk = 227.0f;
    if (e.diff_index == 2) { Z.max_hp = 7500.0f; Z.hp = 7500.0f; Z.def = 262.0f; }
    a.f(0x7ac) = diff_level() <= 1 ? 2.0f : 1.0f;
    const int n = flag(196, 0);
    const bool rush = flag(193, 0) != 0;
    if (e.diff_very_easy) {
        if (!rush) {
            Z.atk -= float(std::pow(double(float(n)), 1.25));
            Z.def -= float(std::pow(double(float(n)), 1.25));
        }
    } else if (e.diff_easy && !rush) {
        const int k = n < 20 ? (n >> 1) : (n - 10);
        Z.atk -= float(k); Z.def -= float(k);
    }
    if (1.0f >= Z.atk) Z.atk = 1.0f;
    if (1.0f >= Z.def) Z.def = 1.0f;
}

// FUN_0053ab20 (AB §A.2).
void Ctx::brain(const EnemyAiParams& p) {
    if (flag(130, 1) == 0) { zo.plain_tick = true; return; }       // FUN_0045f250(Z)
    switch (a.phase()) {
        case 0: init(p); return;
        case 1:
            if (flag(156, 1) != 0) a.phase() = 2; else ai::stop(a, 0);
            return;
        case 2: break;
        default: return;
    }
    switch (a.i(0x79c)) {
        case 0:                                                    // 0x53ab78: the fight start
            if (flag(156, 1) == 0) break;
            zo.boss_cam.push_back(1);                              // every frame until the start succeeds
            if (!H) break;
            a.i(0x7c4) = 120; a.i(0x79c) = 1; a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x87c) = 0;
            hb(0).queued = 1; hb(1).queued = 1;                    // pattern 1 (roam)
            hb(0).free = 1; hb(1).free = 1;
            break;
        case 1: phase1(); break;
        case 2: phase2(); break;
        default: break;
    }
    // TIMERS (0x53ac2c)
    if (a.i(0x7c4) != 0) a.i(0x7c4) -= 1;
    if (a.i(0x7c8) != 0) a.i(0x7c8) -= 1;
    if (a.f(0x880) != Z.hp) {                                      // ucomiss / jnp: a NaN writes
        a.f(0x880) = Z.hp;
        out.gauge_hp = Z.hp; out.gauge_len = 8000.0f;              // FUN_0043a460(..., max Z+0x94, lenScale 8000.0)
    }
}

// STRIKE (AB §B.7, 0x5384e3 and 0x538592).
void Ctx::strike(const Vec3& T) {
    Boss06Effect& f = fx(Boss06Call::kFx2, 420, T);                // bos\bs_06_z_kaminari
    f.stat = true; f.power = 10.0f; f.knock = 1.0f; f.rtype = 0;
    f.flag201 = true;
    se(393, T);                                                    // zako\z_zaba03, at the strike point
}

// FUN_00537160 (AB §B).
void Ctx::phase1() {
    const Vec3 zpos = zp();
    const Vec3 pp = p_pos();
    const Vec3 T{pp.x, 6.0f, pp.z};                                // [ebp-0x20]: the player with y forced
    Vec3 dirP = nrm(pp - zpos);                                    // FUN_005358a0
    const float dist2 = (pp.x - zpos.x) * (pp.x - zpos.x) + (pp.z - zpos.z) * (pp.z - zpos.z);
    const int psec = in.player ? in.player_section : 0;            // P+0x994
    const int ch = e.character;                                    // flag150

    // ---- B.1 (1): the knock-down test
    if (knock_test()) { go(10); a.i(0x7a0) |= 0x10; }              // 0x5372d8
    const int f = a.i(0x7a0);
    // ---- (2) phase 1 -> phase 2 (0x537303)
    if (!(f & 0x10) && Z.max_hp * 0.7f > Z.hp && H) {
        a.i(0x79c) = 2;
        a.i(0x87c) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;            // TAIL
        return;
    }
    // ---- (3) the dodge (0x537373)
    if (!e.diff_very_easy && !e.diff_easy && Z.flyer && !(a.i(0x7a0) & 0x10) && (a.i(0x7a0) & 0x100)) {
        if (ch == 2) {                                             // Hugo: an Eye within 3.0 (3-D)
            for (const Vec3& q : eyes()) {
                const Vec3 d = q - zpos;
                if (std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) < 3.0f) go(7);
            }
        } else if (16.0f > dist2) {                                // the player within 4.0 (xz)
            if ((ch == 1 && (psec == 0x10f || psec == 0x10c)) || (ch == 3 && psec == 0x111)) go(7);
        }
    }
    // ---- (4) the push-out from the heads (0x537458)
    if ((a.i(0x7a0) & 0x40) && !(a.i(0x7a0) & 0x10) && H) {
        const Boss06Head& B = hb(1); const Boss06Head& A = hb(0);
        {
            const float d2 = (B.pos.x - zpos.x) * (B.pos.x - zpos.x) + (B.pos.z - zpos.z) * (B.pos.z - zpos.z);
            const float r = B.radius;
            if ((r + 1.0f) * (r + 1.0f) > d2 && B.pos.y + r > zpos.y)
                Z.mover.k1 = mul(nrm({zpos.x - B.pos.x, 0.0f, zpos.z - B.pos.z}), 0.05f);
        }
        {
            const float d2 = (A.pos.x - zpos.x) * (A.pos.x - zpos.x) + (A.pos.z - zpos.z) * (A.pos.z - zpos.z);
            // SIC: the height test reads head B (eax = meshB, ecx = hbB).
            if ((A.radius + 1.0f) * (A.radius + 1.0f) > d2 && B.radius + B.pos.y > zpos.y)
                Z.mover.k1 = mul(nrm({zpos.x - A.pos.x, 0.0f, zpos.z - A.pos.z}), 0.05f);
        }
    }
    // ---- (5) 0x53760c
    Z.floor_y = 6.0f;                                              // Z+0x318, every frame

    auto pick_near = [&](bool avoid) {                             // the candidate loop of actions 1 / 6
        std::vector<Vec3> list; std::size_t n = 0;
        const bool hugo = avoid && ch == 2;
        if (hugo) { list = eyes(); n = std::min<std::size_t>(list.size(), 4); }
        const Vec3 lo{-9.0f, 0, -9.0f}, hi{9.0f, 0, 9.0f};
        Vec3 c{};
        for (int i = 0; i < 100; ++i) {
            const Vec3 v = rand_lerp(lo, hi);
            c = {T.x + v.x, v.y + 6.0f, T.z + v.z};
            const float dx = T.x - c.x, dz = T.z - c.z;
            if (dx * dx + dz * dz > 9.0f) {
                if (!avoid) { if (in_arena(c)) break; }
                else if (clear(list, n, 7.0f, c, hugo ? &L.zin.hugo_fx : nullptr) && in_arena(c)) break;
            }
        }
        a.set_v(0x894, c);                                         // the last candidate, accepted or not
    };

    switch (a.i(0x728)) {
        case 0:                                                    // B.2: falls into action 1
            go(1);
            [[fallthrough]];
        case 1:                                                    // B.3: float to a point near the player
            switch (a.i(0x72c)) {
                case 0: {                                          // 0x5376d7
                    pose(300, true);
                    Z.vert.grounded = false; Z.flyer = true;
                    a.i(0x8a8) = e.diff_index == 2 ? 4 : 1;        // hits to knock her down
                    const std::uint32_t r0 = R(100);               // DRAW 1, x 0.0
                    a.f(0x834) = float(r0) * 0.0f / 100.0f + 7.0f;
                    a.f(0x83c) = 0.005f; a.f(0x838) = 0.1f; a.f(0x818) = 0.12f;
                    a.f(0x82c) = 0.06f; a.f(0x830) = 0.02f; a.f(0x814) = 0.05f; a.i(0x720) = 5;
                    if (t87() > 240u) {
                        const std::uint32_t r1 = R(100);           // DRAW 2
                        a.f(0x894) = r1 < 50u ? -41.0f : -27.0f;
                        a.f(0x898) = 6.1f; a.f(0x89c) = 30.0f;
                    } else {
                        pick_near(true);
                    }
                    a.i(0x7a0) |= 0x141;
                    a.i(0x72c) += 1;
                    [[fallthrough]];
                }
                case 1: {                                          // 0x5379eb
                    a.f(0x814) += 0.01f;
                    a.i(0x708) = 4; a.i(0x710) = 0x800;
                    const float px = float(std::cos(double(a.heading))) * 3.0f + zpos.x;
                    const float pz = float(std::sin(double(a.heading))) * 3.0f + zpos.z;
                    if (!in_arena({px, zpos.y, pz})) { a.i(0x72c) = 0; return; }   // re-pick next frame
                    const float dx = a.f(0x894) - zpos.x, dz = a.f(0x89c) - zpos.z;
                    if (dx * dx + dz * dz > 4.0f) return;
                    go(4);
                    return;
                }
                default: return;
            }
        case 4:                                                    // B.4: hover and choose
            a.i(0x708) = 0; a.i(0x710) = 2;
            if (t87() < std::uint32_t(int(a.f(0x7ac) * 30.0f))) return;
            if ((a.i(0x700) == 0 && dist2 > 64.0f) || a.i(0x7c4) == 0) { go(8); return; }
            a.i(0x72c) = 0; a.i(0x87c) = 0;
            a.i(0x728) = dist2 > 4.0f ? 6 : 1;
            return;
        case 6:                                                    // B.5: the magic volleys
            a.i(0x708) = 4; a.i(0x710) = 0x800; a.i(0x7a0) &= int(0xfffffeff);
            switch (a.i(0x72c)) {
                case 0:                                            // 0x537c5b
                    pick_near(false);
                    if (a.i(0x7bc) != 0x12f) gated(0x12f);
                    a.i(0x7bc) = 0x12f;
                    a.i(0x72c) = 1; a.i(0x87c) = 0;
                    [[fallthrough]];
                case 1: {                                          // 0x537d5a
                    const std::uint32_t t = t87();
                    if (t < 8u) return;
                    if (t > 60u) a.i(0x72c) = 2;                   // this frame can still fire
                    if (std::uint32_t(a.i(0x8a0)) % 20u != 0) return;
                    se(391, zpos);                                 // zako\z_zaba01
                    static constexpr float kNm[7] = {-45.0f, -30.0f, -15.0f, 0.0f, 15.0f, 30.0f, 45.0f};
                    static constexpr float kStd[3] = {-30.0f, 0.0f, 30.0f};
                    const bool nm = e.diff_index == 2;
                    const float* ang = nm ? kNm : kStd;
                    for (int k = 0; k < (nm ? 7 : 3); ++k) {
                        // D3DXMatrixRotationY + D3DXVec3TransformCoord on a row vector [H: AB G8], then normalize.
                        const float r = ang[k] * 0.017453292f;
                        const float c = std::cos(r), s = std::sin(r);
                        const Vec3 d = nrm({dirP.x * c + dirP.z * s, dirP.y, -dirP.x * s + dirP.z * c});
                        AiBullet b;                                // FUN_00593550(Z, 2, 310, &pos, &d, f0..f12, 0)
                        b.kind = 2; b.effect = 310;                // enemy\m_hanyoutama01
                        b.pos = {zpos.x + d.x + d.x, d.y + zpos.y, zpos.z + d.z + d.z};
                        b.dir = d;
                        b.speed = 0.1f; b.accel = 0.003f; b.gravity = 0.0f;
                        b.power = 2.0f; b.knock = 0.0f;            // f3 / f4; f5..f12 = 0: no trail, no homing
                        out.bullets.push_back(b);
                    }
                    return;
                }
                case 2: go(1); return;                             // 0x537c32
                default: return;
            }
        case 7:                                                    // B.6: the dodge dash
            a.i(0x7a0) &= int(0xfffffefe); a.i(0x710) = 0x800;
            switch (a.i(0x72c)) {
                case 0:                                            // 0x538075
                    a.set_v(0x894, dodge_point(zpos));
                    a.i(0x72c) += 1;
                    a.f(0x818) = 0.3f; a.f(0x814) = 0.2f;
                    [[fallthrough]];
                case 1: {                                          // 0x5380c5
                    a.f(0x814) = std::min(a.f(0x814) + 0.01f, 1.0f);
                    a.i(0x708) = 4;
                    const float c = float(std::cos(double(a.heading))), s = float(std::sin(double(a.heading)));
                    const Vec3 probe{zpos.x + (c + c), zpos.y + 0.0f, zpos.z + (s + s)};
                    const float dx = a.f(0x894) - zpos.x, dz = a.f(0x89c) - zpos.z;
                    if (in_arena(probe) && dx * dx + dz * dz >= 0.25f) return;
                    go(1);
                    return;
                }
                default: return;
            }
        case 8:                                                    // B.7: lightning
            a.i(0x708) = 0; a.i(0x710) = 2; a.i(0x7a0) &= int(0xfffffeff);
            switch (a.i(0x72c)) {
                case 0: {                                          // 0x538226
                    if (a.i(0x7bc) != 0x12d) { gated(0x12d); se(392, zpos); }   // zako\z_zaba02
                    a.i(0x7bc) = 0x12d;
                    // The cast circle: an inlined GoEffect creation of id 586 (no stat block, +0x1dc |= 0x108).
                    Boss06Effect& c = fx(Boss06Call::kInline, 586, zpos);
                    c.power = 0.0f; c.knock = 1.0f; c.or_1dc = 0x108;
                    attach(c, Boss06Parent::kZava, 2);
                    a.i(0x88c) = c.handle;
                    a.i(0x72c) += 1;
                    a.f(0x834) = 5.0f; a.f(0x83c) = 0.008f; a.i(0x87c) = 0;
                    [[fallthrough]];
                }
                case 1:                                            // 0x538461
                    if (t87() < 30u) return;
                    kill_slot(586);
                    if (a.i(0x7bc) != 0x132) force(0x132);
                    a.i(0x7bc) = 0x132;
                    strike(T);
                    a.i(0x72c) += 1;
                    [[fallthrough]];
                case 2:                                            // 0x538570
                    if (e.diff_index == 2 && (t87() == 60u || t87() == 90u || t87() == 120u)) strike(T);
                    if (t87() < 120u) return;
                    a.i(0x700) = int(R(301)) + 120;                // 1 draw
                    a.i(0x7c4) = 600;
                    go(1);
                    return;
                default: return;
            }
        case 10:                                                   // B.8: falls into 11
            go(11);
            [[fallthrough]];
        case 11:                                                   // 0x5386cb
            if (t87() > 60u && diff_level() > 1 && H) {            // Normal, Hard, Nightmare: the heads chase
                for (int i : {1, 0}) {                             // b, then a
                    if (hb(i).pattern == 3) continue;
                    if (hb(i).move != 3 && hb(i).move != 4) abort_move(i);
                    H->set_pattern(i, 3);
                }
            }
            if (!knockdown()) return;
            if (H) { hb(1).queued = 1; hb(0).queued = 1; }         // pattern 1 (roam)
            a.i(0x700) = int(R(301)) + 120;                        // 1 draw
            a.i(0x87c) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;        // TAIL 0x5387b5
            return;
        default: return;                                           // 2, 3, 5, 9, > 11
    }
}

// FUN_00538820 (CG §C).
void Ctx::phase2() {
    if (!H) return;                                                // phase 2 is entered with both heads present
    // ---- C.1 prologue
    if (knock_test()) { go(10); a.i(0x7a0) |= 0x10; }              // 0x538935
    if ((a.i(0x7a0) & 0x40) && !(a.i(0x7a0) & 0x10)) {             // hover-height avoidance: b, then a
        const Vec3 zpos = zp();
        for (int i : {1, 0}) {
            const Boss06Head& h = hb(i);
            const float d = len_xz2(h.pos - zpos);
            float v;
            if ((h.radius + 4.0f) * (h.radius + 4.0f) > d) v = h.pos.y + 6.0f; else v = 6.0f;
            a.f(0x834) = v;
            if (a.f(0x7dc) > v) a.f(0x834) = a.f(0x7dc);
        }
    }
    const auto warp_setup = [&](bool act12) {                      // action 7 / 12 step 0
        a.set_v(0x894, dodge_point(kC0));                          // FUN_00536a30((-34, 6, 29))
        pose(300, false);
        if (act12) Z.flags12c |= 0x40u;
        Z.vert.grounded = false; Z.flyer = true;
        a.f(0x818) = 0.12f; a.f(0x82c) = 0.06f; a.f(0x830) = 0.02f;
        a.f(0x834) = 6.0f; a.f(0x7dc) = 6.0f; a.f(0x83c) = 0.05f; a.f(0x838) = 0.1f;
        if (act12) { Z.floor_y = 6.0f; a.i(0x8a8) = 4; a.i(0x7a0) |= 0x41; }
        else { a.i(0x7a0) = (a.i(0x7a0) | 0x41) & ~4; }
        a.i(0x72c) = 1;
        if (!act12) a.i(0x8a8) = 4;
        a.f(0x814) = 0.5f;
    };
    const auto at_waypoint = [&](float r2) { return !(len_xz2(a.v(0x894) - zp()) > r2); };

    switch (a.i(0x728)) {
        case 0: {                                                  // C.2: the mount cycle
            const int step = a.i(0x72c);
            if (std::uint32_t(step) > 6u) return;
            switch (step) {
                case 0: {                                          // 0x538b4a
                    const Vec3 zpos = zp();
                    if (a.i(0x7c0) == 0) {                         // first cycle: the closer head, a on a tie
                        const Vec3 vb = zpos - hb(1).pos, va = zpos - hb(0).pos;
                        const float dB = vb.x * vb.x + vb.y * vb.y + vb.z * vb.z;
                        const float dA = va.x * va.x + va.y * va.y + va.z * va.z;
                        a.i(0x7c0) = dA > dB ? 2 : 1;
                        zo.boss_cam.push_back(3);                  // 0x538bd8
                    } else {
                        a.i(0x7c0) = (1 - mount()) + 1;            // Other(Z+0x7c0): later cycles alternate
                    }
                    const int h = mount(), o = 1 - h;
                    Boss06Effect& f = fx(Boss06Call::kFx2, 426, zpos);   // bos\bs_06_z_baria [0x538c47]
                    f.stat = true; f.power = -10.0f; f.knock = 3.0f; f.rtype = 2;
                    attach(f, Boss06Parent::kZava, 2);
                    a.i(0x88c) = f.handle;
                    se(167, zpos);
                    abort_move(h); start_move(h, 9);               // move 9 = face Zava
                    abort_move(o); start_move(o, 0);
                    pose(300, true);
                    Z.flags12c |= 0x1040u; Z.guard399 = true; Z.def = 999.0f;
                    a.i(0x7a0) = (a.i(0x7a0) | 0x24) & ~0x41;
                    a.f(0x834) = 0; a.f(0x83c) = 0; a.f(0x838) = 0; Z.floor_y = 6.0f;
                    a.i(0x72c) = 1; a.f(0x814) = 0; a.i(0x87c) = 0;
                    [[fallthrough]];
                }
                case 1: {                                          // 0x538d81
                    if (t87() < 60u) return;
                    Z.alpha_to = 0.0f; Z.alpha_step = 0.1f;        // Z+0x4dc / Z+0x4e0
                    a.i(0x7a0) &= ~1;
                    const Vec3 zpos = zp();
                    const int h = mount();
                    const Vec3 pts[4] = {zpos, {zpos.x + 0.0f, zpos.y + 7.0f, zpos.z + 0.0f},
                                         {hb(h).pos.x + 2.0f, hb(h).pos.y + 7.0f, hb(h).pos.z + 0.0f}, body_efc(h)};
                    spline_build(pts);
                    se(394, zpos);                                 // zako\z_zaba04
                    a.i(0x72c) = 2; a.i(0x87c) = 0;
                    [[fallthrough]];
                }
                case 2: {                                          // 0x538ec5: the flight onto the head
                    const float t = float(double(t87()));
                    const float ev = (float(std::cos(double(t / 120.0f * kPi + kPi))) + 1.0f) * 0.5f + 0.0f;
                    const float u = std::min(1.0f, ev);
                    Z.mover.k1 = {}; Z.mover.dx = 0; Z.mover.dz = 0;   // +0x338.., +0xa3c..
                    set_origin(Z, spline_eval(u));
                    Z.flags12c |= 1u;
                    if (std::uint32_t(a.i(0x8a0)) % 3u == 0) {     // the trail: overwrites the handle [0x538fd6]
                        Boss06Effect& f = fx(Boss06Call::kFx2, 423, zp());   // bos\bs_06_z_kiseki
                        f.power = 0.0f; f.knock = 1.0f;
                        a.i(0x88c) = f.handle;
                    }
                    if (1.0f > u) return;
                    Z.alpha_to = 0.0f; Z.alpha_step = 0.01f;
                    kill_slot(426);                                // tested on the handle slot, killed by id
                    a.i(0x7a0) |= 2;                               // RIDING
                    const int h = mount(), o = 1 - h;
                    abort_move(h); start_move(h, 10);              // move 10 = the barrier roar
                    hb(h).queued = 2;                              // the ridden head hunts
                    hb(o).queued = 1;                              // the other roams
                    zo.boss_cam.push_back(4);                      // 0x539080
                    out.set_durable = 1; a.i(0x72c) = 3;           // byte Z+0x9d8 = 1
                    [[fallthrough]];
                }
                case 3: {                                          // 0x539092
                    const int h = mount();
                    if (h < 0 || hb(h).move == 10) return;         // waits for move 10 to finish
                    hb(h).shield = 1; hb(h).phase2 = 1;            // word hbH+0x80 = 0x0101: the RIDDEN head only
                    hb(h).rearm = 60; hb(h).lives = 4;
                    Z.flags12c |= 1u;
                    Z.hurt_shape = 1; Z.hurt_radius = 2.0f; Z.hurt_half_h = 2.0f;   // Z+0x7c / +0x68 / +0x78
                    Z.def = 262.08f; Z.guard399 = false; a.i(0x7a0) &= ~0x20;
                    H->spawn_orbiters(h, in, bo);                  // FUN_0053eab0(ecx H)
                    a.i(0x7a4) += 1; hits() = 0; a.i(0x72c) = 4;
                    [[fallthrough]];
                }
                case 4: {                                          // 0x539128: riding
                    a.i(0x708) = 1; a.i(0x710) = -1;
                    if (a.i(0x7a0) & 2) {
                        const int h = mount();
                        if (h >= 0 && hb(h).shield != 0 && hb(h).move != 12 && e.took_damage) hits() += 1;   // 0x539185
                        return;
                    }
                    hits() = 0; a.i(0x72c) = 5; a.i(0x87c) = 0;    // thrown off
                    [[fallthrough]];
                }
                case 5:                                            // 0x5391b8: on the ground after the dismount
                    a.i(0x708) = 1; a.i(0x710) = -1; a.i(0x7a0) &= ~1;
                    if (e.took_damage) hits() += 1;                // 0x5391dc
                    if (a.f(0x7ac) * 120.0f + 120.0f > float(double(t87()))) return;
                    a.i(0x7c8) = 60; pose(306, true);
                    getup_shove();                                 // FX2 311, the handle kept in Z+0x88c [0x5392d6]
                    se(167, zp()); a.i(0x72c) = 6; a.i(0x87c) = 0;
                    return;
                case 6:                                            // 0x539301
                    if (t87() < 30u) return;
                    kill_slot(426);                                // the handle is the 311's, the id killed 426
                    go(7);
                    return;
                default: return;
            }
        }
        case 7:                                                    // C.3: reposition, then choose
            switch (a.i(0x72c)) {
                case 0:                                            // 0x539365
                    warp_setup(false);
                    [[fallthrough]];
                case 1:                                            // 0x53943c
                    a.i(0x708) = 4; a.i(0x710) = 0x800;
                    if (!at_waypoint(4.0f)) return;
                    if (!heads_in_arena()) { go(12); return; }
                    a.i(0x72c) = 0; a.i(0x87c) = 0;
                    a.i(0x728) = Z.max_hp * 0.25f > Z.hp ? 20 : 8; // HP < 25 %: the big attack
                    return;
                default: return;
            }
        case 8: {                                                  // C.4: one head stamps the floor
            const int o = 1 - mount();                             // Other(Z+0x7c0): the head she did not ride last
            a.i(0x708) = 0; a.i(0x710) = 2;
            switch (a.i(0x72c)) {
                case 0: {
                    pose(301, false);                              // + SE 392
                    a.f(0x834) = 8.0f; a.f(0x7dc) = 8.0f;
                    static constexpr int kHits[5] = {1, 2, 4, 4, 6};
                    a.i(0x8a8) = kHits[diff_level()];
                    Boss06Effect& f = fx(Boss06Call::kFx, 586, zp());   // ivent\iv_eisyou_enemy_01 [0x53960c]
                    f.power = 0.0f; f.knock = 1.0f;
                    attach(f, Boss06Parent::kZava, 1);
                    a.i(0x88c) = f.handle;
                    abort_move(o); start_move(o, 11);              // move 11: ice / fire floor
                    hb(1 - o).queued = 2;                          // the other head hunts
                    a.i(0x72c) = 1; a.i(0x87c) = 0;
                    [[fallthrough]];
                }
                case 1:
                    if (t87() >= 120u) {
                        Z.vert.grounded = false; Z.flyer = true;
                        a.f(0x834) = 4.0f; a.f(0x7dc) = 4.0f; a.f(0x83c) = 0.01f;
                    }
                    break;
                default: break;
            }
            if (t87() < 600u) return;
            kill_slot(586);
            abort_move(o);
            go(12);
            return;
        }
        case 10:                                                   // C.5 (entered from the knock-down test)
            for (int i : {1, 0})
                if (hb(i).move == 11 && hb(i).step < 0x80) abort_move(i);
            kill_slot(586);
            a.i(0x728) = 11;
            [[fallthrough]];
        case 11:
            if (!knockdown()) return;
            go(12);
            [[fallthrough]];
        case 12:
            switch (a.i(0x72c)) {
                case 0:                                            // 0x5397df
                    warp_setup(true);
                    [[fallthrough]];
                case 1:                                            // 0x5398bc
                    a.i(0x708) = 4; a.i(0x710) = 0x800;
                    if (!at_waypoint(4.0f)) return;
                    a.i(0x72c) = 2;
                    [[fallthrough]];
                case 2: {                                          // 0x539921
                    a.i(0x708) = 0; a.i(0x710) = 2;
                    if (hb(1).move == 11 || hb(0).move == 11) return;
                    const int h = mount();
                    if (h >= 0 && hb(h).move != 0) return;         // the designated head must be idle
                    go(0);                                         // the next mount (the other head)
                    return;
                }
                default: return;
            }
        case 20: {                                                 // C.6: the big attack
            const int step = a.i(0x72c);
            if (std::uint32_t(step) > 7u) return;
            switch (step) {
                case 0: {                                          // 0x53999a
                    auto busy = [&](int i) { return hb(i).move == 11 || hb(i).move == 17; };
                    if (busy(1) || busy(0)) { go(7); return; }     // 0x53932f: bounce — re-warps and comes back
                    hb(1).busy = 1; hb(0).busy = 1;
                    pose(300, false);
                    Z.def = 999.0f; out.set_durable = 1;
                    Z.flyer = true; Z.guard399 = true;             // word Z+0x398 = 0x0101
                    Z.vert.grounded = false;
                    a.f(0x818) = 0.12f; a.f(0x7dc) = 6.0f; a.f(0x834) = 6.0f; a.f(0x83c) = 0.05f; a.f(0x838) = 0.1f;
                    Z.flags12c |= 0x40u; a.i(0x7a0) = (a.i(0x7a0) | 0x20) & ~1; a.f(0x814) = 0.5f;
                    Boss06Effect& f = fx(Boss06Call::kFx, 426, zp());   // no stat block: this barrier cannot hurt
                    f.power = 0.0f; f.knock = 1.0f;
                    attach(f, Boss06Parent::kZava, 1);
                    a.i(0x88c) = f.handle;
                    a.i(0x72c) = 1; a.set_v(0x894, {-34.0f, 6.0f, 32.5f});
                    [[fallthrough]];
                }
                case 1:                                            // 0x539b8f
                    a.i(0x708) = 4; a.i(0x710) = 0x800;
                    if (!at_waypoint(1.0f)) return;
                    pose(301, false); a.i(0x710) = 2;
                    abort_move(1); abort_move(0); start_move(1, 17); start_move(0, 17);   // b first
                    a.i(0x72c) = 2; a.i(0x87c) = 0;
                    [[fallthrough]];
                case 2: {                                          // 0x539c45
                    if (t87() < 120u) return;
                    Boss06Effect& g = fx(Boss06Call::kFx2, 533, kC1);   // bos\bs_06_zentai_02 [0x539cd1]
                    g.power = 0.0f; g.knock = 1.0f; g.flag203 = true;
                    L.fx533 = g.handle;                            // [0x7dde9c]
                    L.fx388 = 0;                                   // [0x7ddea0]
                    bo.shake = std::pair<int, Vec3>{1000, {0.05f, 0.05f, 0.05f}};
                    zo.fog = std::pair<int, float>{2, 0.01f};      // if fogCount > 2
                    se(308, kC0);                                  // boss\b_06_18
                    a.f(0x7b0) = 60.0f; a.i(0x7b4) = 0; a.i(0x72c) += 2; a.i(0x87c) = 0;
                    [[fallthrough]];                               // step 4, the same frame (step 3 is nothing)
                }
                case 4: {                                          // 0x539d95
                    const std::uint32_t t = t87();
                    // (1) the eruption, once
                    if (t > 100u && L.fx388 == 0) {
                        bo.shake = std::pair<int, Vec3>{1000, {0.1f, 0.1f, 0.1f}};
                        bo.rumble.push_back(3); se(309, kC0);      // boss\b_06_19
                        Boss06Effect& f = fx(Boss06Call::kFx2, 388, kC1);   // bos\bs_06_zentai_01 [0x539e75]
                        f.stat = true; f.power = 20.0f; f.knock = 0.0f; f.rtype = 3;
                        L.fx388 = f.handle;
                    }
                    if (in.player) {
                        // (2) suction toward the centre: the player's velocity is OVERWRITTEN
                        const Vec3 P = in.player_pos;
                        const float k = std::min(1.0f, float(double(t)) / 300.0f);
                        const Vec3 d = nrm({-34.0f - P.x, 6.0f - P.y, 29.0f - P.z});
                        const float s = in.player_grounded ? 0.3f : 0.1f;
                        bo.player_vel = Vec3{d.x * s * k, d.y * s * k, d.z * s * k};
                        const int lvl = diff_level();
                        if (t > 60u) {
                            // (3) Nightmare only: a puff at the player's previous sampled position
                            if (diff_level() == 4 && lvl < 5) {
                                const std::uint32_t per = std::uint32_t(int(a.f(0x7b0)));   // cvttss2si
                                if (per != 0 && t % per == 0) {
                                    static constexpr std::uint8_t kLo[5] = {60, 60, 40, 40, 40};
                                    static constexpr std::uint8_t kHi[5] = {120, 90, 60, 60, 60};
                                    const int lo = kLo[lvl], hi = kHi[lvl];
                                    a.f(0x7b0) = float(int(R(std::uint32_t(hi - lo + 1))) + lo);   // 0x53a03f
                                    const Vec3 Q{a.f(0x7e4), 6.01f, a.f(0x7ec)};
                                    if (len_xz2(Q - kC1) > 1.0f) {
                                        Boss06Effect& f = fx(Boss06Call::kFx2, 350, Q);   // bos\bs_02_rollingsmoke
                                        f.stat = true; f.power = 2.0f; f.knock = 2.0f; f.rtype = 2;
                                        se(301, Q);                // boss\b_06_11
                                    }
                                    a.set_v(0x7e4, P);
                                }
                            }
                            // (4) the expanding ring
                            if (t % 20u == 0) {
                                static constexpr std::uint8_t kRing[5] = {0, 4, 8, 10, 12};
                                const int n = kRing[lvl];
                                for (int i = 0; i < n; ++i) {
                                    const double ang = double(float(i) * (360.0f / float(n)) * 0.0174532924f);
                                    const float r = float(double(t)) / 20.0f;
                                    const Vec3 pos{float(std::cos(ang)) * r - 34.0f, 6.1f, float(std::sin(ang)) * r + 29.0f};
                                    Boss06Effect& f = fx(Boss06Call::kFx2, 350, pos);   // 0x53a2b7
                                    f.stat = true; f.power = 2.0f; f.knock = 2.0f; f.rtype = 2;
                                }
                            }
                        }
                    }
                    // (5) the blast
                    if (t < 360u) return;
                    if (t == 360u && in.player) {
                        const Vec3 P = in.player_pos;
                        if (len_xz2(P - kC1) < 4.0f && P.y < kC1.y + 3.0f)
                            zo.strike_atk = 267.0f;                // FUN_004fd350(Z, P, &zero16), STR 267 for the call
                    }
                    bo.zoom = std::pair<float, float>{0.5f, -0.005f};
                    bo.shake = std::pair<int, Vec3>{75, {0.2f, 0.8f, 0.2f}};
                    se(310, kC0); bo.rumble.push_back(5);          // boss\b_06_20
                    a.i(0x72c) = 5; a.i(0x87c) = 0;
                    return;                                        // step 5 with t87 = 0 < 60 returns
                }
                case 5:                                            // 0x53a3f5
                    if (t87() < 60u) return;
                    a.i(0x72c) = 6; a.i(0x87c) = 0;
                    [[fallthrough]];
                case 6:                                            // 0x53a413
                    zo.fog = std::pair<int, float>{0, 0.01f};      // if fogCount > 0
                    kill_id(350);
                    Z.flags12c &= ~0x40u; Z.def = 252.0f; out.set_durable = 0; Z.guard399 = false;
                    a.f(0x7dc) = 4.0f; a.f(0x834) = 4.0f; a.i(0x7a0) &= ~0x24;
                    pose(300, false);
                    if (a.i(0x88c) != 0) { bo.kill_handles.push_back(a.i(0x88c)); a.i(0x88c) = 0; }   // the 426 barrier
                    if (L.fx533 != 0) { bo.kill_handles.push_back(L.fx533); L.fx533 = 0; }           // the 533 column
                    kill_id(586); kill_id(426);
                    abort_move(1); abort_move(0);
                    a.i(0x72c) = 7; a.i(0x87c) = 0;
                    return;
                case 7:                                            // 0x53a513
                    if (t87() >= 360u) go(12);
                    return;
                default: return;                                   // step 3: nothing
            }
        }
        default: return;                                           // 1..6, 9, 13..19
    }
}

void ride(Actor& Z, Boss06Link& L) {                               // FUN_0053aa90 (AB §A.5)
    const EnemyAi& a = Z.ai;
    const int m = a.i(0x7c0);
    if (!(a.i(0x7a0) & 2) || m == 0 || !L.in) return;
    const ef2sim::M4& n = L.in->head[std::size_t(m - 1)].nodes[std::size_t(kBodyEfc)];
    set_origin(Z, {n.m[3][0] + 0.0f, n.m[3][1] - 0.5f, n.m[3][2] + 0.0f});
}

}  // namespace

// The handler's head (FUN_0053af10 up to the brain call 0x53b44b, AB §A.1) and the brain.
AiOut m870(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    AiOut out;
    Boss06Link* Lp = e.boss06;
    if (!Lp || !e.actor || !Lp->in || !Lp->out || !Lp->z) { out.skip_action_player = true; return out; }   // [H] inert
    Boss06Link& L = *Lp;
    L.zout = {};
    L.ai_ran = false; L.f156_cleared = false; L.requested = false;
    L.section = e.section;
    Ctx c{a, e, *e.actor, L, out, L.heads, *L.in, *L.out, L.zout};
    Actor& Z = *e.actor;
    c.sync_z();
    const float php = L.in->player_hp_mirror;                      // [0x76a730]

    if (c.flag(156, 1) == 0) { L.zout.plain_tick = true; out.skip_action_player = true; return out; }   // 0x53af3f
    // ---- the HP floor and the dying trigger (0x53af86)
    if (a.phase() == 2 && 1.0f > Z.hp) {
        Z.hp = 1.0f;
        const int f = a.i(0x7a0);
        if (!(f & 0x400) && !(f & 0x20)) {
            a.i(0x7a0) = f | 8;
            Z.kind &= 0xffffff77u; a.hittable = false;
            if (php >= 1.0f) L.zout.player_iframes = 9999;
        }
    }
    if (1.0f > php) { Z.kind &= 0xffffff7fu; a.hittable = false; }   // 0x53aff8: the player is dead
    c.sync_z();
    // ---- dying while riding: break the fusion (0x53b008)
    const int f = a.i(0x7a0);
    if ((f & 0xa) == 0xa && !(f & 0x20)) {
        if (L.heads)
            for (int i = 0; i < 2; ++i)                            // a, then b
                if (c.hb(i).phase2) { c.hb(i).move = 8; c.hb(i).step = 0; c.hb(i).timer = 0; }
        out.skip_action_player = true;                             // the brain does not run
        return out;
    }
    // ---- the death (0x53b0be)
    if ((f & 8) && !(f & 0x20) && Z.vert.grounded) {
        out.skip_action_player = true;
        auto held = [&](int i) { return c.hb(i).move == 0x11 || c.hb(i).move == 0x0b; };
        if (!L.heads || held(0) || held(1)) return out;
        c.death();                                                 // 0x53b120
        out.vm_step = true;                                        // FUN_004472e0: one ASO VM step
        if (!(a.i(0x7a0) & 0x200)) {
            a.i(0x7a0) |= 0x200;
            if (c.flag(193, 0) != 0) {
                c.set_flag(156, 0);                                // boss rush
            } else {
                if (php >= 1.0f) {
                    c.set_flag(225, 1); L.zout.byte_76a888 = true;
                    c.set_flag(544, 0); c.set_flag(545, 0); c.set_flag(546, 0);
                }
                L.zout.kill_record = true;                         // FUN_00403cd0
            }
            L.out->shake = std::pair<int, Vec3>{0, {0, 0, 0}};     // 0x53b1ae: shake off
            if (L.in->zoom_active) L.out->zoom = std::pair<float, float>{0.5f, -0.01f};   // only a running blur (AB C1)
            L.zout.award_exp = true;                               // FUN_004fa4a0(Z+0x94)
        }
        return out;
    }
    // ---- the three standard paths (0x53b218)
    const int ph = a.phase();
    if (ph <= 1 && e.culled) {                                     // (enemy_ai_tick's own cull test returns first)
        if (ph != 0) c.keep_out();
        a.set_v(0x808, c.zp());
        ai::stop(a, 1);                                            // FUN_004776e0(Z, 1)
        L.zout.frozen = true;
        out.skip_action_player = true;
        return out;
    }
    if (c.flag(131, 0) != 0) {                                     // 0x53b34c: the cutscene suspend
        if (ph != 0) c.keep_out();
        a.set_v(0x808, c.zp());
        L.zout.frozen = true;
        out.skip_action_player = true;
        return out;
    }
    c.brain(p);                                                    // 0x53b44b
    L.ai_ran = true;
    c.sync_z();
    return out;
}

void m870_post(Actor& Z, const AiEnv& e, AiOut& out) {
    Boss06Link* Lp = e.boss06;
    if (!Lp || !Lp->ai_ran || !Lp->in || !Lp->out || !Lp->z) return;
    Boss06Link& L = *Lp;
    L.ai_ran = false;
    EnemyAi& a = Z.ai;
    Ctx c{a, e, Z, L, out, L.heads, *L.in, *L.out, L.zout};
    // [not modelled] `if byte Z+0x398: FUN_0045f380(Z)` (AB G1).
    // byte Z+0x39a = 3; Z+0x360 (0x53b470): atan2's first fld is +0x734, the second +0x73c.
    if (a.i(0x7a0) & 1) L.zout.facing = 1.5707964f - float(std::atan2(double(a.f(0x734)), double(a.f(0x73c))));
    else L.zout.facing = a.heading;
    a.f(0x81c) = 1.0f; a.f(0x820) = 1.0f;                          // after the mover wrapper FUN_00478080
    if (Z.flyer) {                                                 // 0x53b4ed: flying: enforce the pose
        const int s = a.i(0x7bc);
        const int want = (s == 0x12f || s == 0x132 || s == 0x12d) ? s : 0x12c;
        if (L.section != want) c.gated(want);
    } else if (a.i(0x7bc) == 0x130 && L.section != 0x130) {
        c.gated(0x131);
    }
}

void m870_tail(Actor& Z, const AiEnv& e) {
    Boss06Link* Lp = e.boss06;
    if (!Lp || !Lp->in || !Lp->out) return;
    Boss06Link& L = *Lp;
    const bool cleared = L.f156_cleared;
    L.f156_cleared = false;
    if (cleared || (e.world ? e.world->flag(156, 1) : 1) != 1) return;   // 0x53b5a7 / 0x53b713: flag156 == 1
    ride(Z, L);                                                    // FUN_0053aa90
    const EnemyAi& a = Z.ai;
    if (Z.flyer && std::uint32_t(a.i(0x8a0)) % 20u == 0) {         // the floor ring, +0x8a0 after this frame's ++
        const Vec3 p = origin(Z);
        Boss06Effect f; f.head = -1; f.call = Boss06Call::kFx2; f.id = 270;   // enemy\m_230_taiki [0x53b654]
        f.pos = {p.x, 6.05f, p.z}; f.power = 0.0f; f.knock = 1.0f;
        f.handle = L.next_handle++;
        f.parent = Boss06Parent::kZavaFloor; f.at_mode = 2;        // her matrix with y forced to 6.05 (0x7ddcd0)
        L.out->effects.push_back(f);
    }
    Vec3 p = origin(Z);                                            // 0x53b713
    if (15.0f > p.z) p.z = 15.0f; else if (p.z > 38.7f) p.z = 38.7f;
    if (6.0f - Z.height > p.y) p.y = 6.0f - Z.height;
    set_origin(Z, p);
}

void m870_heads_events(Actor& Z, const AiEnv& e, const Boss06Output& ho, AiOut& out) {
    Boss06Link* Lp = e.boss06;
    if (!Lp) return;
    Boss06Link& L = *Lp;
    EnemyAi& a = Z.ai;
    if (ho.zava_throw) {                                           // ZAVA_E2 §2, 0x53ffe9..0x54021c
        if (a.i(0x7bc) != 0x130) { out.section = 0x130; out.section_force = true; L.section = 0x130; }
        a.i(0x7bc) = 0x130;
        a.i(0x7a0) &= ~3;                                          // riding and bit 1
        a.f(0x814) = 0; a.i(0x708) = 1; a.i(0x710) = 0;
        a.f(0x834) = 0; a.f(0x83c) = 0; a.f(0x838) = 0;
        Z.flags12c &= 0xffffefbeu;
        out.set_durable = 0;                                       // byte Z+0x9d8 = 0
        Z.flyer = false; Z.guard399 = false;                       // word Z+0x398 = 0
        set_origin(Z, ho.zava_throw->pos);
        Z.mover.k1 = ho.zava_throw->vel;                           // Z+0x338.. = v^ * 0.5
        Z.hurt_shape = 0; Z.hurt_radius = 0.6f; Z.hurt_half_h = 0.0f;   // Z+0x7c / +0x68 / +0x78
        Z.def = 252.0f;
        Z.alpha_to = 1.0f; Z.alpha_step = 1.0f;                    // Z+0x4dc / Z+0x4e0
    }
    if (ho.zava_reseat) ride(Z, L);                                // FUN_0053aa90(FUN_005355e0())
    if (L.z) L.z->pos = origin(Z);
}

}  // namespace cleria::brains
