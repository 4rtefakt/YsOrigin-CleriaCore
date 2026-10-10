// M820 — Toal, a class-4 duel opponent (Hugo's S_3080 duel, `Battle`). Handler FUN_004e2bd0,
// brain FUN_004df8c0 (AI index 75). Spec: build/ai_spec/M820.md.
//
// The handler runs the brain only while g_flags[130] ("boss AI live") is set; otherwise Toal
// is a script puppet (FUN_0045f250: no AI, no action player) whose HP never drops below 1.
// g_flags[131] freezes the AI the same way. The gauge is pushed on every HP change with
// lenScale 7500 (8500 when g_flags[179]).
// When his HP falls below 1 with the player alive the handler (m820_dead) restores HP to 1,
// writes 174 = 1 and clears 156 and 130 (ending `Battle`'s Wait_FlagEqualInt(156, 0); the
// script then takes `Battle_Win` on 174), hides the gauge, kills every effect attached to him,
// puts back the turn rate, DEF and run multiplier the brain saved, and asks for section 5.
// Hugo losing is not this handler's business: with g_flags[133] set (`Battle` sets it) the
// PLAYER's death handler FUN_0046f810 clears 156 / 130 and leaves 174 = 0 (`Battle_Lose`).
//
// MOVESET. The brain is a combo sequencer like M830's: the sub-state +0x72c plays one move
// and returns to +0x79c (0x14 = decide again, 0x1e = continue the scripted chain +0x730).
// The move parameters ride in the bytes of +0x7a0: [0] the recovery ticks (or a dash / dodge
// mode), [1] a variant flag (or the dash's angle in 10-degree steps), [2] an extra-push flag.
//   100   the five-hit sword combo: sections 0x100, 0x103, 0x104, 0x101, 0x102 in turn
//         (the entry-time stack table), each requested once the previous one has ended,
//         while the target stays within 5.0 and he has taken fewer than 5 - d/2 hits.
//   0x6e  the lunge (section 0x10b) with a channel-1 push of d*0.25 + 2.75.
//   0x78  the jump slash (section 0x10e, or 0x111 with byte [1]).
//   0x82  the big slash (section 0x10c, or 0x10f with byte [1]: its Marker 1 pulls the player
//         toward a point 2 ahead of Toal through the player's +0x344 channel).
//   0x8c  the vanishing dash (sections 0x13c / 0x10d, or 0x110 with byte [2]): on the
//         section's Marker 1 a channel-1 burst along the heading (mode 0), toward the point
//         in front of (1) or behind (2) the target, or at the angle a 3-ray probe chose (3);
//         then +0x7a8 fades him out and back in, arming a body hit every tick on d >= 0.
//   0x96  the charge-up super (section 0x13d, DEF + 15; only with g_flags[179], then 1500
//         ticks of cooldown in +0x7c4).
//   0xa0  the jump-dodge (section 3), M830's candidate tables.
// The GoEffect-pool scan reads Hugo's live effects 0x86 / 0x87 / 0x82 / 0x89 / 0x90 through
// AiWorld::for_each_effect (they drive the dodge reactions; build/event_re/DUELS.md §7).
// [not modelled] +0x64 / +0x4be / +0x3c0
// writes. The zoom blur (0x76a550 / 0x76a554 / 0x7b2488 + FUN_005e0590 centre) on the big
// slash and the super leaves as AiOut::zoom_s / zoom_r / zoom_centre.
#include <cmath>
#include <cstdint>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kGone = -7777777.0f;              // DAT_006be15c
constexpr float kPi = 3.141590118408203f;         // 0x40490fd0 (0x6bd6a8, the dodge tables' pi)
constexpr float kHalfPi = 1.5707950592041016f;    // 0x3fc90fd0
constexpr float k120 = 2.095440626144409f;        // 0x6bec04
constexpr float k60 = 1.046149492263794f;         // 0x6bebf4
constexpr float kProbe = 1.0367248058319092f;     // 0x6bd450: the dash probe's side rays

// Effects the brain attaches to Toal (+0x1a0 = A+0x20): FUN_004522b0(A+0x20) kills them all on
// the defeat. Tag 0 = the g_flags[179] aura (EF2 0xab, INIT). No other effect the brain spawns
// is attached (the dodge's EF2 191 is a plain one).
constexpr std::uint32_t kAttachedTags = 1u << 0;

// The five combo sections, the stack table [esp+0x64..0x74] built at entry (0x4df8d2..0x4df913).
constexpr int kCombo[5] = {0x100, 0x103, 0x104, 0x101, 0x102};

int gflag(const AiEnv& e, int i, int unset) { return e.world ? e.world->flag(i, unset) : unset; }

// The byte views of the packed dwords (+0x7a0..+0x7a2 the move bytes, +0x7a4 the combo index,
// +0x8a4 the half-HP latch).
int byte_at(const EnemyAi& a, int off) {
    return int((std::uint32_t(a.i(off & ~3)) >> ((off & 3) * 8)) & 0xffu);
}
void set_byte(EnemyAi& a, int off, int v) {
    const int sh = (off & 3) * 8;
    std::uint32_t w = std::uint32_t(a.i(off & ~3));
    w = (w & ~(0xffu << sh)) | ((std::uint32_t(v) & 0xffu) << sh);
    a.i(off & ~3) = std::int32_t(w);
}
void set_word(EnemyAi& a, int off, int v) {      // a 16-bit store inside one dword
    set_byte(a, off, v & 0xff);
    set_byte(a, off + 1, (v >> 8) & 0xff);
}

// The three sections the vanishing dash plays (the brain tests this set in seven places).
bool dash_sec(int s) { return s == 0x13c || s == 0x10d || s == 0x110; }

// The GoEffect-pool scan (0x4dfbc3..0x4dfc44): over DAT_007300bc[0..DAT_007300c0), skipping
// entries with byte +0x11 & 2, by EF2 id (+0x1d8, a word compare): 0x86 / 0x87 -> `a`, 0x82 ->
// n += 1, 0x89 -> n += 20, 0x90 -> `b` (Hugo's live shots and skills) [BYTES 0x4dfbe0..0x4dfc37].
// The pool comes from AiWorld::for_each_effect (none without a world: the scan's empty answer).
struct PoolScan { bool a = false, b = false; int n = 0; };
PoolScan scan_effects(const AiEnv& e) {
    PoolScan r;
    if (!e.world) return r;
    e.world->for_each_effect([&](const AiWorld::Fx& f) {
        const int id = f.id & 0xffff;
        if (id == 0x86 || id == 0x87) r.a = true;
        else if (id == 0x82) r.n += 1;
        else if (id == 0x89) r.n += 0x14;
        else if (id == 0x90) r.b = true;
    });
    return r;
}

AiOut still(EnemyAi& a) {                          // puppet / frozen: see m830.cpp
    AiOut out;
    out.skip_action_player = true;
    a.mode() = 0; a.speed = 0;
    return out;
}

// FUN_004df8c0.
void body(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, Actor& A, AiOut& out) {
    using ai::r15;
    const int d = difficulty(e);
    const float fd = float(d);
    const int pct = int(std::floor(A.hp * 100.0f / A.max_hp));     // FUN_004217f0 + cvttss2si
    a.set_v(0x808, e.self);                                         // (the restore: ai::keep_out, by the host)
    if (e.launched) return;
    const Vec3 to = a.v(0x734);
    const float dist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    bool facing = true;                                             // 0x4dfbaf: dot >= 0.2 (0x6bd210)
    {
        float hx = std::cos(a.heading), hz = std::sin(a.heading);
        float tx = to.x, tz = to.z;
        const float hl = std::sqrt(hx * hx + hz * hz), tl = std::sqrt(tx * tx + tz * tz);
        if (hl > 0) { hx /= hl; hz /= hl; }
        if (tl > 0) { tx /= tl; tz /= tl; }
        if (0.2f > tx * hx + 0.0f + tz * hz) facing = false;
    }
    const PoolScan fx = scan_effects(e);                            // Hugo's live effects
    if (kGone > a.f(0x880)) {                                       // defeated: stand still
        a.action() = 2; a.steer() = 0; a.hit_cut = true;
        return;
    }
    switch (a.phase()) {
        case 0: {                                                   // ---- INIT (0x4dfc98)
            ai::init(a, p, e);                                      // FUN_004782f0 + EI[72] +0x90..
            // 0x4dfcbc: FUN_004202e0(CI[+0x124]+0x60 - 0xe, level = +0xc8): the script's Chr_SetStatus level.
            if (e.world) e.world->restat(A, A.cast_id, A.level);
            a.i(0x70c) = 0;
            a.i(0x728) = 0;
            a.set_v(0x808, e.self);
            a.i(0x72c) = 0; a.i(0x730) = 0;
            a.f(0x880) = 0.0f;
            a.phase() = 4;
            a.f(0x7ac) = a.f(0x814);                                // the turn rate
            a.f(0x7b0) = A.def;                                     // DEF
            a.f(0x7b4) = a.f(0x820);                                // the run multiplier ...
            a.f(0x820) = 5.0f;                                      // ... then 5.0 (0x40a00000)
            a.dur() = 30; a.action() = 2; a.steer() = 0;
            if (gflag(e, 179, 0) != 0) {                            // DAT_0076bbe8
                AiEffect au{0xab, e.self, 1.0f, true};              // attached (+0x1d5 = 1), offset 0
                au.tag = 0;
                out.effects.push_back(au);
                A.exp_reward += 16000.0f;                           // DAT_006bdf64
            }
            A.flags130 |= 1u;
            A.kind = 0x88; a.hittable = true;                       // +0x60 = 0x88; [not modelled] +0x64 = 0x2f, +0x4be
            A.flags12c = 0x1090000u;                                // a store, not an or
            return;
        }
        case 1: ai::acquire(a); [[fallthrough]];
        case 2: a.phase() = 4; [[fallthrough]];
        case 4: break;
        default: return;
    }
    if (e.took_damage) a.i(0x7c0) += 1;                             // +0x3bc & 2

    // ---- MAIN (0x4dfe9a)
    int S = e.section;
    auto req = [&](int id) { if (ai::request(S, id, false, e)) { out.section = id; out.section_force = false; } };
    auto force = [&](int id) { S = id; out.section = id; out.section_force = true; };
    const bool f179 = gflag(e, 179, 0) != 0;                        // DAT_0076bbe8, read where used
    const int K2 = 5 - d / 2;                                       // hits before it breaks off (0x4e007e)
    int& sub = a.i(0x72c);
    int& seq = a.i(0x730);
    auto hits_out = [&] { return std::uint32_t(a.i(0x7c0)) >= std::uint32_t(K2); };
    auto open = [&] { return std::uint32_t(sub) < 100u && std::uint32_t(seq) < 100u; };
    auto approach = [&](int t) { a.action() = 0x400; a.steer() = 0; a.dur() = t; };
    auto face = [&] {                                               // +0x308 = +0x314 = atan2(dz, dx)
        a.heading = float(std::atan2(double(to.z), double(to.x)));
        out.snap_facing = true;
    };
    // The recovery-byte formulas (0x4e08ce.., 0x4e0a5c..): (k - d) * 4 + R%15, 62 - 4d + R%15.
    auto rb18 = [&] { return int((std::uint32_t((18 - d) * 4) + r15(e) % 15u) & 0xffu); };
    auto rb23 = [&] { return int((std::uint32_t((23 - d) * 4) + r15(e) % 15u) & 0xffu); };
    auto rb62 = [&] { return int((std::uint32_t(62 - 4 * d) + r15(e) % 15u) & 0xffu); };
    auto to_chain = [&] {                                           // 0x4e074b
        a.i(0x7bc) = 0; a.i(0x7c0) = 0; a.i(0x7a0) = 0; sub = 0x1e; a.i(0x79c) = 0x1e;
    };
    auto pick_move = [&](int s, int packed) {                       // 0x4e014f
        sub = s; a.i(0x7a0) = packed; a.i(0x79c) = 0x14; a.i(0x7c0) = 0;
    };

    // Sub 0x14, the decision (0x4dfef4..0x4e07ac). Each `return` is a jump to the post block.
    auto decide = [&] {
        static constexpr int kTB0[6] = {130, 140, 200, 230, 300, 0x19a};   // 0x6be460 + 0x12c, 0x19a
        static constexpr int kT54[2] = {0xf0, 0xfa};
        static constexpr int kT78[6] = {130, 140, 230, 300, 400, 0x104};   // 0x6be4a0 + 0x190, 0x104
        // A. effect 0x90 live: dodge or dash at once (0x4dff6c).
        if (fx.b && open()) {
            const std::uint32_t R = e.rng();
            if (5.0f > dist) {
                const bool bit = (R & 0x20000u) != 0;
                sub = bit ? 0xa0 : 0x8c; seq = 0; a.i(0x79c) = 0x14;
                a.i(0x7a0) = bit ? 0 : 0x10003; a.i(0x7a4) = 0;
                return;
            }
            if (int((R >> 17) % 100u) < 33) { a.action() = 2; a.steer() = 0; a.dur() = 15; return; }
            const bool bit = (e.rng() & 0x20000u) != 0;
            if (fx.n >= 12) {
                sub = 0x8c; seq = 0; a.i(0x79c) = 0x14; a.i(0x7a0) = bit ? 0x903 : 0x1b03; a.i(0x7a4) = 0;
                return;
            }
            sub = 0xa0; seq = 0; a.i(0x79c) = 0x14; a.i(0x7a4) = 0; a.i(0x7a0) = bit ? 1 : 0;
            return;
        }
        // B. hit K2 times: dash away (75%) or face and big-slash (0x4e008c).
        if (hits_out() && open()) {
            if (int(r15(e) % 100u) < 75) {
                pick_move(0x8c, 0x10002);
            } else {
                face();
                pick_move(0x82, rb23() | 0x10100);
            }
            return;
        }
        // C. not facing: approach (50%) or dash behind the target (0x4e016e).
        if (!facing && open()) {
            if (int(r15(e) % 100u) < 50) { approach(15); return; }
            sub = 0x8c; a.i(0x7a0) = 2; a.i(0x79c) = 0x14;
        }
        // D. effect 0x86 / 0x87 live: a dodge chain (0x4e01bd).
        if (fx.a) {
            const int lim = f179 ? (d + 5) * 10 : 100;
            if (int(r15(e) % 100u) < lim && open()) {
                const std::uint32_t R2 = r15(e);
                seq = 5.0f > dist ? kT54[R2 & 1u] : kTB0[R2 % 6u];
            }
        }
        // E. n >= 12 (many 0x82 / any 0x89): a chain from T78 (0x4e0244).
        if (fx.n >= 12 && open()) seq = kT78[r15(e) % (f179 ? 6u : 5u)];
        // F. the target is airborne: the jump slash (0x4e0289).
        if (!e.target_grounded && open() && int(r15(e) % 100u) < 50) {
            if (3.0f > dist) pick_move(0x78, rb23() | (pct <= 50 ? 0x100 : 0));
            else if (4.5f > dist) pick_move(0x78, rb23() | (pct <= 50 ? 0x10100 : 0x10000));
        }
        // G. by HP band (0x4e03bc).
        if (open()) {
            if (pct > 90) {
                if (dist > 5.0f) {
                    approach(15);
                } else {
                    static constexpr int t[3] = {0, 100, 0x6e};     // [esp+0x44]
                    const std::uint32_t m = r15(e) % 3u;
                    if (m == 0) {                                   // the combo; R%15 - 10d + 60
                        pick_move(100, int((r15(e) % 15u + std::uint32_t(60 - 10 * d)) & 0xffu));
                        return;
                    }
                    seq = t[m];
                }
            } else if (pct > 80) {
                static constexpr int t[5] = {100, 110, 120, 130, 140};      // 0x6be3c0 + 0x8c
                const std::uint32_t i = r15(e) % 5u;
                if (int(r15(e) % 100u) < 75 && dist > 5.0f) approach(15);
                else seq = t[i];
            } else if (pct > 50) {
                static constexpr int t[7] = {110, 120, 130, 140, 200, 230, 260};   // 0x6be400 + ..
                static constexpr int n[3] = {100, 0xf0, 0xfa};      // [esp+0x44]
                const std::uint32_t R = r15(e);
                if (5.0f > dist) seq = n[R % 3u];
                else if (int(R % 100u) < 75) approach(15);
                else seq = t[r15(e) % (f179 ? 7u : 6u)];
            } else {
                static constexpr int t[6] = {200, 230, 300, 260, 0x136, 0x136};   // 0x6be490 + ..
                if (byte_at(a, 0x8a4) == 0) {                       // the first time under 51%
                    seq = 0x136;
                    set_byte(a, 0x8a4, 1);
                } else if (5.0f > dist) {
                    seq = kT54[r15(e) & 1u];
                } else {
                    bool pick = true;
                    if (dist > 5.0f && int(r15(e) % 100u) < 50) {
                        pick = false;
                        if (int(r15(e) % 100u) < 50) {
                            approach(15);
                        } else {
                            sub = 0xa0; seq = 0; a.i(0x79c) = 0x14; a.i(0x7a0) = 2; a.i(0x7a4) = 0;
                        }
                    }
                    if (pick) {
                        seq = t[r15(e) % (f179 ? 6u : 3u)];
                        if (seq == 0x136 && a.i(0x7c4) != 0) seq = t[r15(e) & 3u];   // the super cools down
                    }
                }
            }
        }
        // H (0x4e0743).
        if (std::uint32_t(seq) >= 100u) { to_chain(); return; }
        if (std::uint32_t(sub) >= 100u) return;
        approach(15);
    };

    // Sub 0x1e, the scripted chain on +0x730 (0x4e07d6; tables 0x4e29f0 / 0x4e29a8 for
    // 0..0xe8, 0x4e2b24 / 0x4e2adc for 0xf1..0x19c). No duration wait: each step starts a move.
    auto chain = [&] {
        const int v = seq;
        auto next = [&](int s) { sub = s; seq = v + 1; };
        switch (v) {
            case 0:
                if (e.rng() & 0x20000u) {
                    sub = 0xa0; seq = v + 1;
                    a.i(0x7a0) = (5.0f <= dist) + 1;                // aside near, toward far
                    a.i(0x7a4) = 0;
                } else {
                    seq = v + 1;
                }
                break;
            case 1: a.action() = 2; a.steer() = 0; a.dur() = 30 - 5 * d; seq = v + 1; break;
            case 2: sub = 0x14; seq = 0; break;
            case 0x64: case 0xfa: case 0xfb: case 0x12d:
                next(100); set_byte(a, 0x7a0, 0);
                break;
            case 0x65: case 0x7a:
                sub = 0x6e; seq = 0; set_byte(a, 0x7a1, 0); set_byte(a, 0x7a0, rb18());
                break;
            case 0x6e: next(0x6e); set_byte(a, 0x7a0, rb18()); break;
            case 0xfd: sub = 0x6e; seq = 0; set_byte(a, 0x7a0, rb18()); break;
            case 0x6f: sub = 0x8c; seq = 0; set_byte(a, 0x7a0, 2); break;
            case 0x78: next(0xa0); set_byte(a, 0x7a0, 0); break;
            case 0x79: case 0xca: next(0xa0); set_byte(a, 0x7a1, 0); break;
            case 0x82: case 0x8c: case 0xc9: case 0x12e: next(0x8c); set_byte(a, 0x7a0, 2); break;
            case 0x83: sub = 100; seq = 0; set_byte(a, 0x7a0, rb23()); break;
            case 0x8d: {
                sub = 0x82; seq = 0;
                const int b = rb23();
                set_word(a, 0x7a1, 0); set_byte(a, 0x7a0, b);
                break;
            }
            case 0xc8: next(0xa0); set_byte(a, 0x7a2, 0); break;
            case 0xf3: case 0x107: case 0x193:
                if (!facing) { approach(5); break; }
                [[fallthrough]];
            case 0xcb: {
                sub = 0x82; seq = 0;
                const int b = rb62();
                set_word(a, 0x7a1, 1); set_byte(a, 0x7a0, b);
                break;
            }
            case 0xe6: case 0x104: case 0x105: case 0x106: next(0x8c); set_word(a, 0x7a0, 0x303); break;
            case 0xe7: next(0x6e); set_word(a, 0x7a0, 0x100); break;
            case 0xe8: sub = 0x6e; seq = 0; set_byte(a, 0x7a1, 1); set_byte(a, 0x7a0, rb18()); break;
            case 0xf0: {
                next(0x78);
                const int b = rb23();
                set_word(a, 0x7a1, 0); set_byte(a, 0x7a0, b);
                break;
            }
            case 0xf1: case 0xfc: case 300: case 0x19a: next(0x8c); set_byte(a, 0x7a0, 1); break;
            case 0xf2: {
                next(0x82);
                const int b = rb62();
                set_word(a, 0x7a1, 0); set_byte(a, 0x7a0, b);
                break;
            }
            case 0x12f: {
                if (!facing) { approach(5); break; }
                sub = 0x78; seq = 0;
                const int b = rb62();
                set_word(a, 0x7a1, 0x101); set_byte(a, 0x7a0, b);
                break;
            }
            case 0x136: next(0xa0); a.i(0x7a0) = 0; a.i(0x7a4) = 0; break;
            case 0x137: case 0x138: case 0x139: next(0x8c); set_word(a, 0x7a0, 0x1b03); break;
            case 0x13a: sub = 0x96; seq = 0; break;
            case 0x190: {                                           // probe three 3.0 rays for the dash
                next(0x8c); set_byte(a, 0x7a0, 3);
                const float th = float(std::atan2(double(to.z), double(to.x)));
                auto dl = [&](float ang) {
                    return Vec3{float(std::cos(double(ang))) * 3.0f, 0.0f, float(std::sin(double(ang))) * 3.0f};
                };
                const bool h1 = e.world ? e.world->ray(e.self, dl(th + kProbe)).hit : false;
                const bool h2 = e.world ? e.world->ray(e.self, dl(th - kProbe)).hit : false;
                const bool h3 = e.world ? e.world->ray(e.self, dl(th)).hit : false;
                set_byte(a, 0x7a1, !h1 ? 6 : !h2 ? 30 : (h3 ? 18 : 0));   // x 10 degrees
                break;
            }
            case 0x191:                                             // far: the behind-the-target dash
                if (dist > fd * 0.25f + 4.0f && dist > 4.5f && dist > 8.0f) {
                    sub = 0x8c; set_byte(a, 0x7a0, 2);
                }
                seq = v + 1;
                break;
            case 0x192: {
                const int s = int(r15(e) % 3u + 11u) * 10;          // 110 / 120 / 130
                seq = 0; sub = s;
                if (s == 0x6e) { set_byte(a, 0x7a1, 0); set_byte(a, 0x7a0, rb18()); }
                if (s == 0x78) {
                    seq += 1;
                    const int b = rb23();
                    set_word(a, 0x7a1, 0x101); set_byte(a, 0x7a0, b);
                } else {                                            // 0x6e and 0x82 are overwritten
                    sub = 0x1e; seq = 0x193;
                }
                break;
            }
            case 0x19b: {
                next(0x78);
                const int b = rb23();
                set_word(a, 0x7a1, 0x101); set_byte(a, 0x7a0, b);
                break;
            }
            case 0x19c: sub = 0x6e; seq = 0; set_byte(a, 0x7a1, 0); set_byte(a, 0x7a0, rb23()); break;
            default: break;
        }
    };

    switch (sub) {
        case 0: seq = 0; sub = 0x14; a.f(0x814) = a.f(0x7ac); [[fallthrough]];   // 0x4dfec2
        case 0x14:
            if (a.dur() != 0) break;
            decide();
            break;
        case 0x1e:                                                  // 0x4e07b1
            if (hits_out() || fx.b) { sub = 0x14; seq = 0; break; }
            chain();
            break;
        case 100:                                                   // ---- the combo (0x4e11c9)
            a.f(0x814) = (fd * 0.07f + 0.33f) * a.f(0x7ac);
            if (dash_sec(S)) face();
            a.i(0x7a4) = 0;
            sub += 1;
            [[fallthrough]];
        case 0x65:                                                  // 0x4e1260
            if (dist > 5.0f || hits_out()) {
                a.action() = 1; a.dur() = byte_at(a, 0x7a0);
                out.set_durable = 0;
                a.f(0x814) = a.f(0x7ac);
                sub = a.i(0x79c);
                break;
            }
            req(kCombo[byte_at(a, 0x7a4) % 5]);                     // the index is < 5 here
            push1(A, a.heading + 0.0f, 0.3f);                       // FUN_0047aa00(0.3, 0)
            out.set_durable = 1;
            sub += 1;
            a.action() = 2; a.dur() = -1;
            [[fallthrough]];
        case 0x66: {                                                // 0x4e12e1: wait for the hit to end
            const int k = byte_at(a, 0x7a4);
            if (k < 5 && S == kCombo[k]) break;                     // (k == 5 is unreachable here)
            if (k + 1 < 5) { sub -= 1; set_byte(a, 0x7a4, k + 1); break; }
            a.action() = 1; sub += 1; a.dur() = byte_at(a, 0x7a0); set_byte(a, 0x7a4, k + 1);
            break;
        }
        case 0x67:                                                  // 0x4e136c
            if (a.dur() != 0) break;
            out.set_durable = 0;
            a.f(0x814) = a.f(0x7ac);
            sub = a.i(0x79c);
            break;
        case 0x6e: {                                                // ---- the lunge (0x4e13a1)
            if (dash_sec(S)) face();
            const double h = double(a.heading);
            const float s = fd * 0.25f + 2.5f + 0.25f;
            A.mover.k1.x += float(std::cos(h)) * s;
            A.mover.k1.y += s * 0.0f;
            A.mover.k1.z += float(std::sin(h)) * s;
            req(0x10b);
            if (byte_at(a, 0x7a1) != 0) { a.steer() = 0; a.action() = 2; } else { a.action() = 1; }
            sub += 1; a.dur() = -1;
            break;
        }
        case 0x6f:                                                  // 0x4e14eb
            if (S == 0x10b) break;
            sub = 0x70;
            if (byte_at(a, 0x7a1) != 0) { a.steer() = 0; a.action() = 2; } else { a.action() = 1; }
            a.dur() = byte_at(a, 0x7a0);
            break;
        case 0x70: case 0x7a: case 0x85:                            // 0x4e153c
            if (a.dur() != 0) break;
            out.set_durable = 0;
            sub = a.i(0x79c);
            break;
        case 0x78: {                                                // ---- the jump slash (0x4e1565)
            if (dash_sec(S)) face();
            req(byte_at(a, 0x7a1) != 0 ? 0x111 : 0x10e);
            if (byte_at(a, 0x7a2) == 0) {
                a.action() = 1;
            } else {
                const double h = double(a.heading);
                const float c = float(std::cos(h)), sn = float(std::sin(h));
                A.mover.k1.x += c + c;
                A.mover.k1.y += 0.0f;
                A.mover.k1.z += sn + sn;
                a.steer() = 0; a.action() = 2;
            }
            sub += 1; a.dur() = -1;
            break;
        }
        case 0x79:                                                  // 0x4e167d
            if (S == 0x10e || S == 0x111) break;
            sub = 0x7a; a.action() = 1; a.dur() = byte_at(a, 0x7a0);
            break;
        case 0x82: {                                                // ---- the big slash (0x4e16c3)
            if (dash_sec(S)) face();
            if (byte_at(a, 0x7a1) != 0) { req(0x10f); sub += 1; }
            else { req(0x10c); sub += 2; }
            if (byte_at(a, 0x7a2) != 0) {
                const double h = double(a.heading);
                const float s = fd * 0.125f + 1.0f;
                A.mover.k1.x += float(std::cos(h)) * s;
                A.mover.k1.y += s * 0.0f;
                A.mover.k1.z += float(std::sin(h)) * s;
            }
            a.action() = 1; a.dur() = -1;
            break;
        }
        case 0x83:                                                  // 0x4e180e (0x10f only)
            if (e.aso_marker == 1) {
                // The zoom blur 0.4 / rate -0.0075 (0x76a550 = 0x3ecccccd / 0x76a554 = 0xbbf5c28f / 0x7b2488 = 1),
                // centred (FUN_005e0590) on origin + 2 (cos h, 0, sin h).
                const float c = float(std::cos(double(a.heading))), sn = float(std::sin(double(a.heading)));
                out.zoom_s = 0.4f; out.zoom_r = -0.0075f;
                out.zoom_centre = Vec3{c + c + e.self.x, e.self.y + 0.0f, sn + sn + e.self.z};
                sub += 1;
            }
            if (S == 0x10f || S == 0x10c) break;
            out.set_durable = 0;
            sub = a.i(0x79c); a.dur() = 0;
            break;
        case 0x84: {                                                // 0x4e18f0: the 0x10f pull
            if (S == 0x10f && e.aso_marker == 1) {
                const double h = double(a.heading);
                const float c = float(std::cos(h)), sn = float(std::sin(h));
                const Vec3 C{c + c + e.self.x, e.self.y + 0.0f, sn + sn + e.self.z};
                if (e.world) {
                    e.world->for_each([&](Actor& o) {               // DAT_00730098 order
                        if (o.cast_id != 13 || !(o.hp >= 1.0f)) return;
                        Vec3 v{o.pos.x - C.x, (o.pos.y + o.height) - C.y, o.pos.z - C.z};
                        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);   // FUN_0042cc50
                        if (l == 0.0f) v = {}; else { const float q = 1.0f / l; v = {v.x * q, v.y * q, v.z * q}; }
                        v = {v.x * 15.0f, v.y * 15.0f, v.z * 15.0f};
                        if (std::uint32_t(e.character) <= 3u) {    // DAT_0076bb74 (always)
                            const float k = fd * 0.0006f + 0.0066f;
                            o.mover.k2.x -= v.x * k;                // P+0x344
                            o.mover.k2.z -= v.z * k;                // P+0x34c
                        }
                    });
                }
                out.zoom_centre = C;                                // FUN_005e0590: the blur's centre follows C
            }
            if (S == 0x10c || S == 0x10f) break;
            sub += 1; a.action() = 1; a.dur() = byte_at(a, 0x7a0);
            break;
        }
        case 0x8c:                                                  // ---- the vanishing dash (0x4e1af3)
            if (byte_at(a, 0x7a2) != 0) force(0x110);
            else force((S == 0x13c || S == 0x10d) ? 0x10d : 0x13c);
            sub += 1;
            [[fallthrough]];
        case 0x8d:                                                  // 0x4e1b41
            if (!dash_sec(S)) { a.dur() = 0; sub = a.i(0x79c); break; }
            if (e.aso_marker != 1) break;                           // the +0x9bc as the tick began
            sub += 1;
            [[fallthrough]];
        case 0x8e: {                                                // 0x4e1b87: the burst
            const float base = fd * 0.25f + 5.0f + 1.0f;
            const float lo = base * 0.75f * 0.5f;
            float X = base * 0.75f;
            Vec3 dir{};
            const int m = byte_at(a, 0x7a0);
            if (m == 0) {                                           // along the heading
                const double h = double(a.heading);
                dir = {float(std::cos(h)), 0.0f, float(std::sin(h))};
            } else if (m == 1 || m == 2) {                          // in front of / behind the target
                const float th = m == 1 ? e.target_heading : e.target_heading + kPi;
                const float c = float(std::cos(double(th))), sn = float(std::sin(double(th)));
                const Vec3 v{c + c + e.target.x - e.self.x, 0.0f, sn + sn + e.target.z - e.self.z};
                const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                if (l != 0.0f) { const float q = 1.0f / l; dir = {v.x * q, v.y * q, v.z * q}; }   // FUN_0042cc50
                X = l * 0.3f;                                       // 0x6bd27c
            } else if (m == 3) {                                    // the probed angle
                const float th = float(std::atan2(double(to.z), double(to.x)));
                const float ang = th + float(byte_at(a, 0x7a1)) * 10.0f * kPi / 180.0f;
                dir = {float(std::cos(double(ang))), 0.0f, float(std::sin(double(ang)))};
                X = base;
            }
            float mag = base < X ? base : X;                        // minss
            mag = lo > mag ? lo : mag;                              // maxss
            if (byte_at(a, 0x7a2) != 0) mag = base * 8.0f;
            a.heading = float(std::atan2(double(dir.z), double(dir.x)));
            out.snap_facing = true;
            A.mover.k1.x += dir.x * mag;
            A.mover.k1.y += dir.y * mag;
            A.mover.k1.z += dir.z * mag;
            a.action() = 1; a.dur() = 20;
            a.i(0x7a8) = 30;                                        // the fade timer
            if (byte_at(a, 0x7a2) != 0) { a.dur() += 20; a.i(0x7a8) += 20; }
            sub += 1;
            break;
        }
        case 0x8f:                                                  // 0x4e1f9a
            if (a.dur() != 0) break;
            sub = a.i(0x79c);
            break;
        case 0x96:                                                  // ---- the super (0x4e1fbc)
            if (!f179 || a.i(0x7c4) != 0) {
                a.dur() = 0; out.set_durable = 0; sub = a.i(0x79c);
                break;
            }
            if (dash_sec(S)) face();
            out.set_durable = 1;
            req(0x13d);
            A.def = a.f(0x7b0) + 15.0f;                             // 0x6bd964
            a.action() = 2;
            sub += 1; a.dur() = -1;
            break;
        case 0x97: {                                                // 0x4e208c
            const int m = e.aso_marker;
            if (m == 2 || m == 3) a.f(0x814) = 0.0f;
            if (m == 4) {
                a.f(0x814) = (fd * 0.015f + 0.07f) * a.f(0x7ac);
                // The zoom blur 0.3 / -0.0075 (0x3e99999a / 0xbbf5c28f, 0x7b2488 = 1), centred on origin +
                // 1.3 (0x6bd4a4) (cos h, 0, sin h).
                out.zoom_s = 0.3f; out.zoom_r = -0.0075f;
                out.zoom_centre = Vec3{float(std::cos(double(a.heading))) * 1.3f + e.self.x, e.self.y + 0.0f,
                                       float(std::sin(double(a.heading))) * 1.3f + e.self.z};
                sub += 1;
            }
            if (S == 0x13d) break;
            out.set_durable = 0;
            a.f(0x814) = a.f(0x7ac);
            A.def = a.f(0x7b0);
            sub = a.i(0x79c); a.dur() = 0;
            break;
        }
        case 0x98:                                                  // 0x4e21b3
            if (e.aso_marker == 4) {                                // the blur held at 0.3 (the strength word alone)
                out.zoom_s = 0.3f;
                out.zoom_centre = Vec3{float(std::cos(double(a.heading))) * 1.3f + e.self.x, e.self.y + 0.0f,
                                       float(std::sin(double(a.heading))) * 1.3f + e.self.z};
                break;
            }
            if (S == 0x13d) break;
            sub = 0x99;
            A.def = a.f(0x7b0);
            a.f(0x814) = a.f(0x7ac);
            a.action() = 1;
            a.dur() = int(r15(e) % 20u) - 15 * d + 105;
            break;
        case 0x99:                                                  // 0x4e22a2
            if (a.dur() != 0) break;
            out.set_durable = 0;
            sub = a.i(0x79c);
            a.i(0x7c4) = 1500;                                      // the super's cooldown
            a.i(0x7c0) = 0;
            break;
        case 0xa0: {                                                // ---- the jump-dodge (0x4e22df)
            const float th = float(std::atan2(double(to.z), double(to.x)));
            const int m = byte_at(a, 0x7a0);
            float T[5];
            if (m == 0) {                                           // away
                const float t0[5] = {kPi, k120, -k120, kHalfPi, -kHalfPi};
                for (int k = 0; k < 5; ++k) T[k] = t0[k];
                if (th > a.heading) for (int k = 1; k < 5; ++k) T[k] *= -1.0f;
            } else if (m == 1) {                                    // aside
                const float t0[5] = {kHalfPi, -kHalfPi, k120, -k120, kPi};
                for (int k = 0; k < 5; ++k) T[k] = t0[k];
                if (th > a.heading) { T[0] = -kHalfPi; T[1] = kHalfPi; T[2] = -k120; T[3] = k120; }
            } else {                                                // toward (m == 2; others unreachable [H])
                const float t0[5] = {0.0f, k60, -k60, kHalfPi, -kHalfPi};
                for (int k = 0; k < 5; ++k) T[k] = t0[k];
                if (a.heading > th) { T[0] = -0.0f; T[1] = -k60; T[2] = k60; T[3] = -kHalfPi; }
            }
            int k = 0;
            for (; k < 5; ++k) {
                const double ang = double(T[k] + th);
                const Vec3 dl{float(std::cos(ang)) * 4.0f, 0.0f, float(std::sin(ang)) * 4.0f};
                const bool hit = e.world ? e.world->ray(e.self, dl).hit : false;
                if (hit) continue;
                force(3);
                A.mover.k1.x += dl.x * 0.25f * 3.0f;
                A.mover.k1.y += dl.y * 0.25f * 3.0f;
                A.mover.k1.z += dl.z * 0.25f * 3.0f;
                jump(A, 0.2f, e.jump_v0, out);                      // FUN_00449460(&{0.2})
                // [not modelled] +0x3c0 |= 1
                a.heading = th;                                     // +0x308 only
                out.effects.push_back({191, {e.self.x, e.self.y - 0.09f, e.self.z}, 1.0f, false});
                break;
            }
            a.dur() = k >= 5 ? 0 : 30;
            a.action() = 2;
            sub = a.i(0x79c);
            break;
        }
        default: break;
    }

    // ---- the post block (0x4e261a): the dash fade, its body hit and its exit.
    {
        const std::uint32_t v = std::uint32_t(a.i(0x7a8));
        const bool outside = std::uint32_t(sub - 0x8c) >= 10u;     // not in subs 0x8c..0x95
        bool leave = false;
        if (v > 10u) {
            const float al = a.alpha + (v >= 25u ? -0.2f : 0.0666f);   // 0x6bd210 / 0x6bd15c
            a.alpha = al;
            if (0.0f > al) a.alpha = 0.0f;
            else if (al > 1.0f) a.alpha = 1.0f;
            if (d >= 0 && dash_sec(S)) {                            // FUN_0045e750, a fresh serial
                AiHitbox h;
                h.shape = 0; h.radius = d == 0 ? 0.5f : (d == 1 ? 0.75f : 1.0f);
                h.fwd = 0.5f; h.angle_deg = 0; h.voff = 0; h.half_h = 0;
                h.life = 2; h.rehit = 200; h.one_shot = 0; h.rtype = 0; h.status = 0;
                h.power = -10.0f; h.knock = 0;
                h.new_serial = true;
                out.hitboxes.push_back(h);
            }
            if (v == 11u && outside && dash_sec(S)) leave = true;
            a.i(0x7a8) = std::int32_t(v - 1u);
        } else {
            a.alpha = 1.0f;
            std::uint32_t w = v;
            if (w != 0) { w -= 1; a.i(0x7a8) = std::int32_t(w); }
            if (dash_sec(S) && outside && w == 0) leave = true;
        }
        if (leave) {                                                // 0x4e27c3: drop the dash section
            const int act = a.action();
            if (act == 1 || act == 2 || act == 0 || act == -1) force(2);
            else if (act == 0x400 || act == 0x800) force(1);
            else if (act == 0x10 || act == 0x20) force(0);          // FUN_00458e40(0)
            out.set_durable = 0;                                    // FUN_00460c10 -> +0x9d8 = 0
        }
        if (a.i(0x7c4) != 0) a.i(0x7c4) -= 1;
    }

    // ---- the tail (0x4e2834)
    if (a.phase() == 4) {
        ai::steer(a, e);
        ai::turn(a, a.f(0x814));
        if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
        ai::act(a, p, e, &out);
    }
}

}  // namespace

void m820_dead(Actor& A, AiWorld& w) {
    EnemyAi& a = A.ai;
    if (w.flag(130, 1) == 0) { A.hp = 1.0f; return; }               // the puppet path (0x4e2bdf)
    // The live path with HP < 1 (0x4e2c1d).
    if (Actor* P = w.player(); P && !P->alive()) {                  // the player lost: Toal stays up
        A.hp = 1.1f;
        a.hittable = false; A.def = 999.0f;                         // FUN_0047aa80(edx = 1)
        return;
    } else if (P) {
        P->i_frames = 9999;                                         // a store, not a grant
    }
    if (kGone > a.f(0x880)) return;                                 // already resolved
    a.hook_kill_tags |= kAttachedTags;                              // FUN_004522b0(A + 0x20)
    a.f(0x880) = -9999999.0f;                                       // 0xcb18967f
    w.set_flag(174, 1);                                             // DAT_0076bbd4
    w.set_flag(156, 0);                                             // DAT_0076bb8c: the duel is over
    w.set_flag(130, 0);                                             // DAT_0076bb24: back to a puppet
    A.hp = 1.0f;
    w.hide_boss_gauge();                                            // DAT_006d5fd8 = 0
    a.f(0x814) = a.f(0x7ac);                                        // the turn rate
    A.def = a.f(0x7b0);                                             // +0xa8 = +0x7b0
    a.f(0x820) = a.f(0x7b4);                                        // the run multiplier
    a.hook_section = 5;                                             // FUN_00446550(5, 0) unless current
    a.action() = 1;
    a.alpha = 1.0f;                                                 // +0x4d8
    a.hit_cut = true;                                               // +0x118 = 0
}

AiOut m820(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (!e.actor) return {};
    Actor& A = *e.actor;
    // ---- the handler FUN_004e2bd0
    if (gflag(e, 130, 1) == 0) {                                    // the script puppet
        if (A.hp < 1.0f) A.hp = 1.0f;
        return still(a);
    }
    // (the dead branch is m820_dead; the off-screen freeze is enemy_ai_tick's)
    AiOut out;
    if (gflag(e, 131, 0) != 0) {
        out = still(a);                                             // FUN_0045f470 only
    } else {
        body(a, p, e, A, out);
        if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }   // FUN_0047aa80
    }
    // The gauge (0x4e2fb3): while not defeated, push on every HP change.
    if (!(kGone > a.f(0x880)) && a.f(0x880) != A.hp) {
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp;
        out.gauge_len = gflag(e, 179, 0) != 0 ? 8500.0f : 7500.0f;  // DAT_006bdf54 / DAT_006bdf50
    }
    return out;
}

}  // namespace cleria::brains
