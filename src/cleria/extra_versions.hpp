// The Extra versions of Yunica, Hugo and Toal (Type Select's second answer: 0x74c0dc / 0x74c0d8 / 0x74c0d4),
// build/mode_re/EXTRA_VERSIONS.md. An Extra version is the normal character plus code branches in the player
// tick FUN_00465170 (and, for Hugo, in his eyes' code): no asset, stat or cost changes. This is the pure part:
// the per-tick counters (player +0x814 / +0x818 / +0x81c) and what they ask the host to spawn.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cleria/cos_table.hpp"
#include "cleria/mathx.hpp"

namespace cleria::extra {

// Player +0x814 / +0x818 / +0x81c: ticks spent in the Ventus / Terra / Ignis skill sections.
struct Counters {
    std::uint32_t c814 = 0, c818 = 0, c81c = 0;
};

// FUN_005916e0's descriptor as the two Terra branches fill it (EXTRA_VERSIONS.md §2.2, §4.3, §5): a kind-0x17
// pulse emitter `ahead` in front of the player. The other fields are the same for every caller here:
// step +0x0c = 0.3, power +0x14 = 0, scale +0x1c = 1.0, +0x24 = a fresh hit token | 0x1000000.
struct Emitter {
    bool on = false;
    float ahead = 0;          // the offset along the facing
    int c = 0;                // D+0x28: C, the life factor (life = C * P + 1)
    int effect = 0;           // D+0x2c
    int p = 0;                // D+0x30: P, the pulse reload (a pulse every P + 1 ticks)
};

// ---- Yunica [BYTES 0x466e0a..0x46737b] ----
struct YunicaOut {
    // Ventus (0x10d / 0x110), the section's 12th tick: `bullets` kind-6 bullets of effect 0x66 from her origin,
    // bullet i heading facing + a0 + i * step; f0 = speed, f1 = accel, f4 = 1.0, f9 = f168, f10 = 1.9.
    int bullets = 0;
    float a0 = 0, step = 0, speed = 0, accel = 0, f168 = 0;
    // Terra (0x10c / 0x10f), tick 24 / 30.
    Emitter em;
};
inline YunicaOut yunica_tick(Counters& c, int sec) {
    YunicaOut o;
    if (sec == 0x10d || sec == 0x110) {
        if (++c.c814 == 12) {
            const bool ch = sec == 0x110;
            o.bullets = ch ? 4 : 3;
            o.a0 = ch ? 0.7853982f : 1.0471976f;         // 0x6bd3a0 / 0x6bd454
            o.step = ch ? 1.5707964f : 2.0943952f;       // 0x6bd4e4 / 0x6bd5a8
            o.speed = ch ? 0.2f : 0.12f;                 // 0x6bd210 / 0x6bd1b0
            o.accel = ch ? 0.004f : 0.002f;              // 0x6bd03c / 0x6bd010
            o.f168 = ch ? 5.0f : 6.0f;                   // 0x6bd868 / 0x6bd880
        }
    } else c.c814 = 0;
    if (sec == 0x10c || sec == 0x10f) {
        const bool ch = sec == 0x10f;
        if (++c.c818 == (ch ? 30u : 24u)) {
            o.em.on = true;
            o.em.ahead = ch ? 3.5f : 1.5f;               // 0x6bd720 / 0x6bd4d4
            o.em.c = 6; o.em.effect = 0x76; o.em.p = ch ? 5 : 3;
        }
    } else c.c818 = 0;
    return o;
}

// ---- Toal [BYTES 0x4673d4..0x467aff]; demon = flag 141 (0x76bb50) != 0 ----
struct ToalOut {
    // FUN_00450410(id, at his origin, rot (0, camera yaw, 0), scale s; side 0, power 0, knock 1, rtype 0,
    // status 0, cls -1): Ventus's kick effect (human form only) and Ignis's repeated fire.
    int fx[2] = {0, 0};
    float fx_scale[2] = {1.0f, 1.0f};
    // Terra, tick 18.
    Emitter em;
    // Terra, from tick 12 every 3rd / 2nd tick: a kind-2 bullet of effect 0xa7 from 2.0 ahead, each axis off by
    // rnd(-r, r), a random direction, f1 = rnd(0.0001, 0.0002), f0 = rnd(0.01, 0.015), f4 = 1.0.
    bool bullet = false;
    float r = 0;
};
inline ToalOut toal_tick(Counters& c, int sec, bool demon) {
    ToalOut o;
    if (sec == 0x10d || sec == 0x110) {
        if (!demon && c.c814 % 9u == 0 && c.c814 < (sec == 0x110 ? 18u : 9u)) o.fx[0] = 0xae;
        ++c.c814;
    } else c.c814 = 0;
    if (sec == 0x10e || sec == 0x111) {
        if (c.c81c % (demon ? 10u : 12u) == 0) {
            o.fx[1] = sec == 0x111 ? (demon ? 0xb3 : 0xb4) : (demon ? 0xaf : 0xa2);
            o.fx_scale[1] = demon ? 1.2f : 1.1f;         // 0x6bd484 / 0x6bd464
        }
        ++c.c81c;
    } else c.c81c = 0;
    if (sec == 0x10c || sec == 0x10f) {
        const bool ch = sec == 0x10f;
        ++c.c818;
        if (c.c818 == 18) {
            o.em.on = true;
            o.em.ahead = 0.5f;                           // 0x6bd304
            o.em.c = (ch ? 5 : 4) + (demon ? 1 : 0); o.em.effect = 0xa7; o.em.p = demon ? 5 : 4;
        }
        if (c.c818 >= 12 && c.c818 % (ch ? 2u : 3u) == 0) {
            o.bullet = true;
            o.r = ch ? 2.5f : 1.5f;                      // 0x6bd624 / 0x6bd4d4
        }
    } else c.c818 = 0;
    return o;
}

// ---- Hugo ----
// The Force Shield's life +0x7bc at the cast [BYTES 0x46f4c8..0x46f53e] (Boost's 480 overrides both versions).
constexpr int hugo_shield_life(bool charged, bool extra) {
    return charged ? (extra ? 0x1b8 : 0x17c) : (extra ? 0x1a4 : 0x168);
}
// FUN_00593b90 [BYTES 0x593dbd..0x593dda]: every bullet of effect 0x82 flies x 1.35 under Hugo Extra.
constexpr float kHugoShotSpeed = 1.35f;                  // 0x6bd4ac
// The player tick [BYTES 0x46737b..0x4673ce]: flag 157 (the eyes' mode source) from the bracelet slot
// (FUN_0046d560: 0 Ventus, 1 Terra, 2 Ignis, 3 none): the V, the rope, the wheel; none leaves it.
constexpr int hugo_flag157(int slot, int cur) {
    return slot == 0 ? 0 : slot == 1 ? 2 : slot == 2 ? 1 : cur;
}

// ---- Which script and sheet: the player and cast 0xa6 (EXTRA_VERSIONS.md §1.4) ----
// Two of the version flags change files: Adol's 0x74c0cc and Yunica's School Outfit 0x74c0d0. ASO ids index the name
// table 0x68a510, AIA ids the table 0x68a800 (aso_aia_name) [DOC ANI_VARIANT §2]. CleriaCore names a script by its
// stem, so aso_stem() carries only the ids these readers use.
inline const char* aso_stem(int aso) {
    switch (aso) {
        case 0x01: return "C_010";    // Yunica
        case 0x02: return "C_020";    // Hugo
        case 0x03: return "C_030";    // Toal
        case 0x0c: return "C_101";    // Adol, Felghana
        case 0xa3: return "C_980";    // Yunica, School Outfit
        case 0xa4: return "C_990";    // Adol, Napishtim
        case 0xa5: return "M_800";    // cast 0xa6's own (CASTINFO +0x68 = 165) [DOC ANI_VARIANT §4]
        default: return "";
    }
}

// FUN_00464fc0 [BYTES 0x464fc0..0x4650e8]: the player's ASO (+0x978), the AIA it writes into that ASO slot's header
// +0xc (`want`) and the one sheet it loads beside them (AniDat_GetAiaSlot, -1 = none). `want` starts from the
// table 0x69bd40 (0x69bd48 while flag 161 != 0); then per character: Adol 0xc, or 0xa4 with want 0xf4 when
// 0x74c0cc != 0; Yunica 1 with preload 0xb, or 0xa3 with preload 0xa3 + 0x69 = 0x10c and want 0x10b (0xf3 while
// flag 161 != 0) when 0x74c0d0 != 0; Hugo 2; Toal 3 with preload 0xc, want 0xc while flag 141 != 0. Every flag test
// is `cmp [x], 0` + JBE / JNZ: any non-zero value is "on".
struct AnimSet {
    int aso = 0, want = 0, preload = -1;
};
inline AnimSet player_anim_set(int chr, bool flag161, bool adol_napishtim, bool school_outfit, bool flag141) {
    static constexpr std::uint16_t kWant[4] = {0x0e, 0x0f, 0x10, 0x11};        // 0x69bd40: c_110..c_113
    static constexpr std::uint16_t kWantDemo[4] = {0x0d, 0x06, 0x07, 0x08};    // 0x69bd48: c_101, c_102..c_104
    AnimSet s;
    s.want = (flag161 ? kWantDemo : kWant)[chr & 3];
    switch (chr) {
        case 1:
            if (school_outfit) { s.aso = 0xa3; s.preload = 0x10c; s.want = flag161 ? 0xf3 : 0x10b; }
            else { s.aso = 1; s.preload = 0xb; }
            break;
        case 2: s.aso = 2; break;
        case 3: s.aso = 3; s.preload = 0xc; if (flag141) s.want = 0xc; break;
        default:
            if (adol_napishtim) { s.aso = 0xa4; s.want = 0xf4; }
            else s.aso = 0xc;
            break;
    }
    return s;
}

// The scene entry FUN_00431840 [BYTES 0x43256d..0x4325ed], for every room but "demo6\": loads the player's sheet
// (0x698ccc / 0x698cd4 by flag 161; Adol Napishtim 0xf4; Yunica with the outfit 0x10b / 0xf3) and one more: Yunica
// 0xb, or 0x10c with the outfit; Toal 0xc. The same files as player_anim_set (`want`, `preload`) but Toal's
// flag-141 sheet, so this is only the load order, which CleriaCore's lazy sheets do not need.

// FUN_00459410 (the factory) [BYTES 0x459439..0x45944d]: `cmp edx(cast), 0xa6; jnz; cmp [0x74c0d0], 0; mov eax,
// 0xa3; cmova ecx, eax` — cast 0xa6 (CAST_M800, Yunica as an actor) is built with ASO 0xa3 (C_980, the outfit's
// player script) in place of its CASTINFO +0x68 (M_800) while the outfit is on. The test is on the CAST id (the
// stack argument [ebp+0x10]), not the instance id. The ASO goes to +0x24c, and from there to the sprite.
inline int cast_aso(int cast_id, int own_aso, bool school_outfit) {
    return cast_id == 0xa6 && school_outfit ? 0xa3 : own_aso;
}
// The same as a stem: "C_980" for cast 0xa6 under the outfit, nullptr = the cast's own script.
inline const char* cast_aso_override(int cast_id, bool school_outfit) {
    return cast_id == 0xa6 && school_outfit ? aso_stem(0xa3) : nullptr;
}

// Cast_Load FUN_00572230 (script op 0xad) [BYTES 0x572230..0x5722b5]: after the cast's own ASO (CASTINFO +0x68, not
// the factory's swap) and that ASO's header sheet, one more sheet: cast 0xac 0xac + 0x5d = 0x109; cast 0xa6 0xb
// (c_114), or 0xa6 + 0x66 = 0x10c (c_982) while 0x74c0d0 != 0; cast 0xa8 0xc (c_115). -1 = none.
inline int cast_load_extra_aia(int cast_id, bool school_outfit) {
    if (cast_id == 0xac) return 0x109;
    if (cast_id == 0xa6) return school_outfit ? 0x10c : 0xb;
    if (cast_id == 0xa8) return 0xc;
    return -1;
}

// ---- The kind-0x17 GoXx (FUN_005916e0 / FUN_00591850; EXTRA_VERSIONS.md §5, GOXX_17_19.md §1-§2) ----
// A ground runner: it moves `step` a tick along a fixed heading on the floor and, every P + 1 ticks, the host
// spawns one EF2 (FUN_00450410: rot (0, -pi/2 - heading, 0), scale s, power, knock 0, rtype 0, status 0, cls 1)
// at its position. It hits nothing itself. Also Napishtim Adol's Ignis (anime_Special a0 = 1).
// [not modelled] the water probe (life -= 2 a tick in water, vy >= -0.125, the ring and the wake), the floor
// decals (2 RNG draws each), the hit token it writes to each effect, the 0x74c0f0 == 0 move-only state.
struct Runner {
    Vec3 pos{}, last{};
    float heading = 0, cs = 1, sn = 0;        // +0x130, +0x158, +0x160
    float step = 0, power = 0, scale = 1;     // +0x12c, +0xfc, +0x13c
    float vy = 0;                             // +0x15c
    int effect = 0, p = 0;                    // +0x14c, +0x150
    int life = 0, cnt = 0;                    // +0x154, +0x168
    float atk = 0;                            // the owner's stat block at creation (+0x94..)
    bool dead = false;

    // FUN_005916e0 [BYTES 0x591720..0x591847]: `at` = the descriptor's position.
    static Runner make(const Vec3& at, float heading, float step, float power, float scale, int c, int p, int effect,
                       float atk) {
        Runner r;
        r.heading = heading; r.cs = std::cos(heading); r.sn = std::sin(heading);
        r.step = step; r.power = power; r.scale = scale; r.effect = effect; r.p = p; r.atk = atk;
        r.life = c * p + 1;
        const float k = float(unsigned(p) >> 1) * step;
        r.pos = {at.x + r.cs * k, at.y - 0.75f, at.z + r.sn * k};         // 0x6bd390
        r.last = r.pos;
        return r;
    }
    // One sim tick [BYTES 0x591890.., pulse 0x591baa..0x591d96, move 0x591d9f..0x591de4]. `ray(from, d, &hit)`:
    // the terrain ray (FUN_0041e420 set 2) over the segment from..from + d. Returns true when it pulses: the
    // host spawns the effect at `*at` (the position before this tick's move).
    template <class Ray>
    bool tick(Ray&& ray, Vec3* at) {
        if (dead) return false;
        if (--life <= 0) { dead = true; return false; }
        Vec3 hit{};
        if (ray(Vec3{pos.x, pos.y + 1.1f, pos.z}, Vec3{0.0f, -2.6f, 0.0f}, &hit)) {   // 0x6bd464; 0xc0266666
            if (hit.y - pos.y > 0.0f) pos.y = hit.y;                                  // snaps up only
            vy = 0.0f;
        } else vy = -0.75f;
        bool pulse = false;
        if (cnt != 0) --cnt;
        else {
            cnt = p;
            pulse = true; *at = pos;
            const Vec3 d{pos.x - last.x, pos.y - last.y, pos.z - last.z};
            if (ray(Vec3{last.x, last.y + 0.75f, last.z}, d, &hit)) { dead = true; return true; }   // a wall: this pulse, then gone
            last = pos;
        }
        pos = {pos.x + cs * step, pos.y + vy * step, pos.z + sn * step};
        return pulse;
    }
};

// ---- Adol, the Napishtim version (0x74c0cc != 0 && character == 0; build/mode_re/ADOL.md §4) ----
// Player +0x6fc / +0x700 / +0x704 / +0x708: the Terra sword's press counter [BYTES 0x465e68..0x465f17,
// 0x466cf4..0x466e02] and the Ignis sword's hold-and-release [BYTES 0x465fa5..0x466047]. `slot` = FUN_0046d560
// (0 Ventus, 1 Terra, 2 Ignis, 3 none).
struct Napishtim {
    std::uint32_t count = 0;        // +0x6fc: quick attack presses
    float decay = 0;                // +0x700
    float since = 0;                // +0x704: ticks since the last attack press
    float lock = 0;                 // +0x708: the lockout after section 0x124

    // A bracelet switch lands [BYTES 0x465363..0x46536d].
    void on_switch() { count = 0; lock = 0.0f; }
    // An attack-press edge with the weapons out (+0x50c & 1, local_54 == 0). True on the 11th quick press with
    // Terra: the host plays section 0x124 (SetSection(0x124, 0)), turns to the stick when one is held, drops the
    // attack request (+0x6f4 / +0x6f5) and skips the rest of this tick's attack and skill input (jmp 0x466b0d).
    bool press(int slot) {
        since = 0.0f;
        if (slot != 1 || lock != 0.0f) return false;
        if (++count <= 10) return false;
        count = 0;
        lock = 60.0f;                                    // 0x42700000
        return true;
    }
    // Every tick before the skill block: the attack button up (+0x510 & 1 clear) in an Ignis wind-up section
    // starts its swing — SetSection(the result, 0), the tint struct +0x264..+0x274 zeroed, the rest of the
    // tick's attack and skill input skipped. -1 = nothing.
    static int ignis_release(int slot, bool attack_held, int section) {
        if (attack_held || slot != 2) return -1;
        return section == 0x11b ? 0x11c : section == 0x11d ? 0x11e : section == 0x11f ? 0x120 : -1;
    }
    // Every tick with Terra, after the i-frame countdown: past 12 ticks without a press the count falls by one a
    // tick; from 6 presses up Adol is tinted, v = (count * 192 - 960) / 5 in red and green. What FUN_00445b30 is
    // called with: op 0 = not called (section 0x124 keeps its own flash), 1 = (0, 0), 2 = (colour, colour) —
    // +0x268 = +0x264 = the colour, t +0x26c = 0, step +0x270 = 0.001 (0x6bcff0), count +0x274 = 0.
    struct Tint {
        int op = 0;
        std::uint32_t colour = 0;
    };
    Tint frame(int slot, int section) {
        Tint t;
        if (slot != 1) return t;
        since += 1.0f;
        if (since > 12.0f) {                             // 0x6bd948
            decay += 1.0f;
            if (decay >= 1.0f) {
                decay = 0.0f;
                if (count != 0) --count;
            }
        }
        if (count > 5) {
            const std::uint32_t v = (count * 192u - 960u) / 5u;      // 6 -> 38 .. 10 -> 192
            t.op = 2;
            t.colour = (v << 16) | (v << 8);
        } else if (section != 0x124) t.op = 1;
        if (lock != 0.0f) lock -= 1.0f;
        return t;
    }
};

// anime_Special(1, power, -, scale, C, effect, step, P) [BYTES 0x44a0ef..0x44a1ac]: the kind-0x17 runner from the
// actor's origin along `heading` (+0x308 - the camera yaw). `a` = the script's raw dwords.
inline Runner special_runner(const std::int32_t a[8], const Vec3& origin, float heading, float atk) {
    auto f = [&](int i) { float v; std::memcpy(&v, &a[i], 4); return v; };
    return Runner::make(origin, heading, f(6), f(1), f(3), a[4], a[7], a[5], atk);
}

// ---- The kind-0x18 GoXx: the chain lightning (FUN_00591f60 / FUN_00592d00 / FUN_00592af0; GOXX_17_19.md §3) ----
// anime_Special(2, power, -, hops, flags, delay) of Napishtim Adol's Terra skill. Created with the monsters near
// the point in front of the caster, it visits `n` of them — least visited first, then nearest to the previous
// hop — at 0.85 a tick, one direct hit per arrival (FUN_004fba30 flag 1: no overlap test, no re-hit bank; power
// +0xfc, class +0x109 = 2, knock 0). With no monster it is a 5-unit ray along the heading instead: 19 scans,
// power -2.0, re-hit 12 on its token.
// Its two bolts are type-7 lines (Bolt below) fed with the head's position each tick.
// [not modelled] a target's node offset (FUN_005f5000 for class 0x0d).

// The CRT's qsort as the exe links it [BYTES 0x64c4d0; its short sort 0x64c3f0]: not stable, and the order it
// leaves equal keys in decides the chain's later hops (after one pass over the targets every key ties).
// cmp(a, b) < 0 / 0 / > 0.
template <class T, class Cmp>
void crt_qsort(std::vector<T>& v, Cmp cmp) {
    if (v.size() < 2) return;
    int lostk[30], histk[30], sp = 0;
    int lo = 0, hi = int(v.size()) - 1;
    for (;;) {
        const int size = hi - lo + 1;
        if (size <= 8) {                                  // a selection sort: the largest to the end
            for (int h = hi; h > lo; --h) {
                int mx = lo;
                for (int p = lo + 1; p <= h; ++p)
                    if (cmp(v[std::size_t(p)], v[std::size_t(mx)]) > 0) mx = p;
                std::swap(v[std::size_t(mx)], v[std::size_t(h)]);
            }
        } else {
            auto at = [&](int i) -> T& { return v[std::size_t(i)]; };
            int mid = lo + size / 2;
            if (cmp(at(lo), at(mid)) > 0) std::swap(at(lo), at(mid));
            if (cmp(at(lo), at(hi)) > 0) std::swap(at(lo), at(hi));
            if (cmp(at(mid), at(hi)) > 0) std::swap(at(mid), at(hi));
            int loguy = lo, higuy = hi;
            for (;;) {
                if (mid > loguy) { do { ++loguy; } while (loguy < mid && cmp(at(loguy), at(mid)) <= 0); }
                if (mid <= loguy) { do { ++loguy; } while (loguy <= hi && cmp(at(loguy), at(mid)) <= 0); }
                do { --higuy; } while (higuy > mid && cmp(at(higuy), at(mid)) > 0);
                if (higuy < loguy) break;
                std::swap(at(loguy), at(higuy));
                if (mid == higuy) mid = loguy;
            }
            ++higuy;
            if (mid < higuy) { do { --higuy; } while (higuy > mid && cmp(at(higuy), at(mid)) == 0); }
            if (mid >= higuy) { do { --higuy; } while (higuy > lo && cmp(at(higuy), at(mid)) == 0); }
            if (higuy - lo >= hi - loguy) {
                if (lo < higuy) { lostk[sp] = lo; histk[sp] = higuy; ++sp; }
                if (loguy < hi) { lo = loguy; continue; }
            } else {
                if (loguy < hi) { lostk[sp] = loguy; histk[sp] = hi; ++sp; }
                if (lo < higuy) { hi = higuy; continue; }
            }
        }
        if (--sp < 0) return;
        lo = lostk[sp]; hi = histk[sp];
    }
}

// ---- The type-7 GoXx: the jagged line (GOXX_17_19.md §3.5) ----
// FUN_005903b0 (constructor), FUN_00590200 (a node's drift), FUN_00590ac0 (update), FUN_00590f50 (draw) [BYTES
// 0x590200..0x5913f9]. N nodes from `a` to `b`, each pushed off the straight line by a random unit vector x 0.35 /
// (its distance to the previous node), then smoothed (2 : 1 : 2 with its neighbours, x 0.2); every node drifts
// 0.01 a tick along a heading that turns 0.45..1.35 rad per node made. A free line lives `life` updates; a driven
// one (the chain's) starts hidden and is fed by its owner. While `forks` is set it throws a side line every 4th
// update from a random node: 2.0..3.5 x `reach` long, `fork_segs` nodes, 20 updates, widths 0.125 -> 0.025.
// The descriptor's +0x24 (0.15 / 0.1) is never read.
struct Bolt {
    struct Node {                                         // 0x38 bytes
        Vec3 pos{}, prev{};                               // +0x00, +0x0c (+0x18: the frame's blend of the two)
        Vec3 vel{};                                       // +0x24
        float w = 0;                                      // +0x30: the half width
        bool hidden = false, water = false;               // +0x34, +0x35
    };
    struct Desc {
        Vec3 a{}, b{};                                    // +0x00, +0x0c
        float w0 = 0, w1 = 0;                             // +0x18, +0x1c
        int segs = 2, life = 0, forks = 0;                // +0x20, +0x28, +0x2c
        float reach = 0;                                  // +0x30
        int fork_segs = 0;                                // +0x34
        bool water = false, driven = false;               // +0x3c, +0x44
    };
    std::vector<Node> nodes;                              // +0x120, +0x124
    int life = 0;                                         // +0x128
    float scroll = 0;                                     // +0x12c: the texture's u, -0.015 a tick
    int forks = 0, cd = 3;                                // +0x130, +0x134
    int fork_segs = 0;                                    // +0x138
    float reach = 0;                                      // +0x13c
    float ang = 0, turn = 0;                              // +0x148, +0x14c
    bool driven = false;                                  // +0x150
    bool dead = false;

    static float u01(std::uint32_t w) {                   // U in [0, 1) from a word's low 23 bits
        const std::uint32_t bits = (w & 0x7fffffu) | 0x3f800000u;
        float f;
        std::memcpy(&f, &bits, 4);
        return f - 1.0f;
    }
    // FUN_00590200: the node's drift along the line's heading, then the heading turns (one draw).
    template <class Rnd>
    void jag(Node& q, Rnd&& rnd) {
        q.vel = {float(std::cos(double(ang))) * 0.01f, 0.0f, float(std::sin(double(ang))) * 0.01f};   // 0x6bd084
        ang = (u01(rnd()) * 0.9f + 0.45f) * turn + ang;   // 0x6bd3e4, 0x6bd2f0
    }
    // FUN_005903b0. Draws: the heading, the turn's sign, then four a node (the offset's x, y, z; the drift).
    template <class Rnd>
    static Bolt make(const Desc& d, Rnd&& rnd) {
        Bolt b;
        const int n = unsigned(d.segs) < 2u ? 2 : d.segs;
        b.nodes.resize(std::size_t(n));
        b.life = d.life; b.forks = d.forks; b.reach = d.reach; b.fork_segs = d.fork_segs; b.driven = d.driven;
        b.nodes[0].pos = d.a; b.nodes[0].w = d.w0;
        b.nodes[std::size_t(n - 1)].pos = d.b; b.nodes[std::size_t(n - 1)].w = d.w1;
        const float step = 1.0f / float(n - 1);
        b.ang = u01(rnd()) * 6.2831855f + 0.0f;           // 0x6bd898
        b.turn = std::int32_t(rnd()) < 0 ? 1.0f : -1.0f;  // [BYTES 0x5905eb: jge -> -1.0]
        float t = step;
        for (int i = 1; i < n; ++i, t += step) {          // the last node too: `b` is moved as the others are
            Node& q = b.nodes[std::size_t(i)];
            const Node& p = b.nodes[std::size_t(i - 1)];
            q.w = (d.w1 - d.w0) * t + d.w0;
            q.pos = {(d.b.x - d.a.x) * t + d.a.x, (d.b.y - d.a.y) * t + d.a.y, (d.b.z - d.a.z) * t + d.a.z};
            const float dx = q.pos.x - p.pos.x, dy = q.pos.y - p.pos.y, dz = q.pos.z - p.pos.z;
            float jx = u01(rnd()), jy = u01(rnd()), jz = u01(rnd());
            jx = (jx + jx) - 1.0f; jy = (jy + jy) - 1.0f; jz = (jz + jz) - 1.0f;
            float l = std::sqrt(jy * jy + jx * jx + jz * jz);
            if (l == 0.0f) jx = jy = jz = 0.0f;
            else { l = 1.0f / l; jx *= l; jy *= l; jz *= l; }
            const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dl > 0.0f) {
                const float k = 0.35f / dl;               // 0x6bd2b0
                q.pos.x += jx * k; q.pos.y += jy * k; q.pos.z += jz * k;
            }
            b.jag(q, rnd);
        }
        std::vector<Vec3> raw(std::size_t(n), Vec3{});
        for (int i = 0; i < n; ++i) { raw[std::size_t(i)] = b.nodes[std::size_t(i)].pos; b.nodes[std::size_t(i)].water = d.water; }
        for (int i = 1; i < n - 1; ++i) {                 // 0x6bd210
            const Vec3 &l = raw[std::size_t(i - 1)], &c = raw[std::size_t(i)], &r = raw[std::size_t(i + 1)];
            b.nodes[std::size_t(i)].pos = {(l.x + l.x + c.x + (r.x + r.x)) * 0.2f, (l.y + l.y + c.y + (r.y + r.y)) * 0.2f,
                                          (l.z + l.z + c.z + (r.z + r.z)) * 0.2f};
        }
        for (Node& q : b.nodes) { q.prev = q.pos; q.hidden = d.driven; }
        return b;
    }
    // FUN_00590ac0's sim half, one tick. True when it threw a side line: *fork (made inside this update: its
    // draws come before the next object's).
    template <class Rnd>
    bool tick(Rnd&& rnd, Bolt* fork) {
        if (dead) return false;
        if (!driven && --life < 0) { dead = true; return false; }
        bool made = false;
        if (forks != 0) {
            if (cd == 0) {
                cd = 3; --forks;
                const std::uint32_t n = std::uint32_t(nodes.size()), m = (n * 2u) / 3u;
                const std::uint32_t r = (rnd() >> 17) % m;
                const Node s = nodes[std::size_t((n >> 3) + r)];
                if (!s.hidden) {
                    const float rad = (float(int(r)) / float(int(m)) * 1.5f + 2.0f) * reach;   // 0x6bd4d4, 0x6bd580
                    const int v = int(rnd() >> 24);                                         // FUN_0045e310
                    Desc d;
                    d.a = s.pos;
                    d.b = {s.pos.x + cos_table(v) * rad, s.pos.y + 0.0f, s.pos.z + cos_table((v - 0x40) & 0xff) * rad};
                    d.w0 = 0.125f; d.w1 = 0.025f; d.segs = fork_segs; d.life = 20; d.water = s.water;
                    *fork = make(d, rnd);
                    made = true;
                }
            } else --cd;
        }
        for (Node& q : nodes) {
            q.prev = q.pos;
            q.pos = {q.pos.x + q.vel.x, q.pos.y + q.vel.y, q.pos.z + q.vel.z};
        }
        scroll -= 0.015f;                                 // 0x6bd0c0
        return made;
    }
    // The chain's feed [BYTES 0x5930b9..0x59328c]: the head +- 0.25 on each axis becomes node 0, the others move
    // down one (each index keeps its width). Four draws: x, y, z, the drift.
    template <class Rnd>
    void feed(const Vec3& head, bool in_water, Rnd&& rnd) {
        Vec3 p = head;
        p.x += u01(rnd()) * 0.5f - 0.25f;                 // 0x6bd304, 0x6bd244
        p.y += u01(rnd()) * 0.5f - 0.25f;
        p.z += u01(rnd()) * 0.5f - 0.25f;
        for (std::size_t k = nodes.size() - 1; k >= 1; --k) {
            const float w = nodes[k].w;
            nodes[k] = nodes[k - 1];
            nodes[k].w = w;
        }
        nodes[0].pos = p;
        jag(nodes[0], rnd);
        nodes[0].hidden = false; nodes[0].water = in_water;
    }
    // The quads FUN_00590f50 builds for one pass (`water`: the nodes' +0x35) [BYTES 0x590fe4..0x5912d7]: a segment
    // is drawn when its first node is shown and of the pass; `view` = the camera's forward 0x795f50; the far end
    // overshoots by 0.1 of the segment. emit(corners[4], u[4], v[4]) in the order near +, near -, far +, far -.
    // `a` = the frame's blend 0x74c0e0. Alpha (of 255): 0x15 x life under 12 updates, else 255.
    int alpha() const { return life < 12 ? (life * 0x15) & 0xff : 0xff; }
    template <class Emit>
    void quads(bool pass_water, const Vec3& view, float a, Emit&& emit) const {
        auto at = [&](const Node& q) {
            return Vec3{(q.pos.x - q.prev.x) * a + q.prev.x, (q.pos.y - q.prev.y) * a + q.prev.y, (q.pos.z - q.prev.z) * a + q.prev.z};
        };
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            const Node &A = nodes[i - 1], &B = nodes[i];
            if (A.hidden || A.water != pass_water) continue;
            const Vec3 ra = at(A), rb = at(B);
            const Vec3 d{ra.x - rb.x, ra.y - rb.y, ra.z - rb.z};
            const Vec3 e{d.x * -0.1f, d.y * -0.1f, d.z * -0.1f};                           // 0x6bdfd8
            const Vec3 p{view.z * d.y - view.y * d.z, view.x * d.z - view.z * d.x, view.y * d.x - view.x * d.y};
            float inv = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (inv > 0.0f) inv = 1.0f / inv;
            const float ka = A.w * inv, kb = B.w * inv;
            const Vec3 fb{rb.x + e.x, rb.y + e.y, rb.z + e.z};
            const Vec3 c[4] = {{ra.x + p.x * ka, ra.y + p.y * ka, ra.z + p.z * ka}, {ra.x - p.x * ka, ra.y - p.y * ka, ra.z - p.z * ka},
                               {fb.x + p.x * kb, fb.y + p.y * kb, fb.z + p.z * kb}, {fb.x - p.x * kb, fb.y - p.y * kb, fb.z - p.z * kb}};
            const float u[4] = {scroll, scroll, scroll + 1.0f, scroll + 1.0f}, v[4] = {0.0f, 1.0f, 0.0f, 1.0f};
            emit(c, u, v);
        }
    }
};

struct Chain {
    struct Target {
        int id = -1;
        Vec3 pos{};
    };
    struct Hop {                                          // the 0x1c-byte chain entry
        int id = -1;                                      // +0x00 the target (-1 = none / lost)
        Vec3 pos{};                                       // +0x04
        int timer = 0;                                    // +0x14: 5 after the arrival
        bool lost = false;                                // +0x18
    };
    Vec3 pos{};                                           // +0x50: the head
    std::vector<Hop> hops;                                // +0x128
    int n = 0, cur = 0;                                   // +0x120, +0x124
    int delay = 0;                                        // +0x138
    float power = 0, atk = 0;                             // +0xfc; the caster's stat block at creation
    bool ray = false;                                     // +0x13d
    int scans = 0;                                        // +0x140
    float heading = 0;                                    // +0x144
    std::uint32_t token = 0;                              // +0xf0 (ray mode only)
    bool dead = false;
    Bolt bolt[2];                                         // +0x12c, +0x130: the two driven lines (chain mode)
    bool bolted = false;                                  // ... were made (the host makes them: the creator's draws)
    int flick = 0;                                        // +0x134
    bool water = false;                                   // +0x13c: FUN_005902d0 at the head, set by the host before a tick

    // The scan's centre [BYTES 0x59206b..0x592093]: ONE cosf feeds both axes (sic).
    static Vec3 centre(const Vec3& at, float h) {
        const float c = 2.0f * std::cos(h);
        return {at.x + c, at.y, at.z + c};
    }
    static int d100(const Vec3& a, const Vec3& b) {       // (int)(|a - b|^2 * 100.0), cvttss2si
        const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
        return int((y * y + x * x + z * z) * 100.0f);
    }
    // A monster within 12.0 of the centre [BYTES 0x59215f: d100 < 0x3840]. [H] its byte +0x250 is taken as set
    // (clear = no range limit).
    static bool in_range(const Vec3& c, const Vec3& p) { return d100(c, p) < 0x3840; }

    // FUN_00591f60. `found`: the live hittable monsters in range, in actor order (the first 20 are kept).
    // `h` = the caster's +0x308 as stored (the handler does not subtract the camera yaw here).
    static Chain make(const Vec3& at, float h, float power, int hops_n, int delay, const std::vector<Target>& found,
                      float atk, std::uint32_t token) {
        Chain c;
        c.pos = at; c.power = power; c.delay = delay; c.atk = atk; c.heading = h;
        c.n = unsigned(hops_n) > 20u ? 20 : hops_n == 0 ? 1 : hops_n;
        if (found.empty()) {                              // the ray [BYTES 0x5926d7..0x592708]
            c.n = 0; c.power = -2.0f; c.ray = true; c.scans = 19; c.token = token;
            return c;
        }
        struct F {
            Target t;
            int visits = 0, d = 0;
        };
        std::vector<F> fs;
        const Vec3 ctr = centre(at, h);
        for (const Target& t : found) {
            if (fs.size() >= 20) break;
            fs.push_back({t, 0, d100(ctr, t.pos)});
        }
        Vec3 from = at;
        for (int i = 0; i < c.n; ++i) {
            for (F& f : fs) {
                f.d = d100(from, f.t.pos);
                if (float(f.d) <= 1.1920929e-07f) { f.d = 0x7fffffff; break; }   // the point it stands on (sic: break)
            }
            crt_qsort(fs, [](const F& a, const F& b) {    // FUN_00591f40: visits, then d (int subtractions)
                return a.visits != b.visits ? a.visits - b.visits : int(std::uint32_t(a.d) - std::uint32_t(b.d));
            });
            Hop hp;
            hp.id = fs[0].t.id; hp.pos = fs[0].t.pos;
            c.hops.push_back(hp);
            ++fs[0].visits;
            from = fs[0].t.pos;
        }
        return c;
    }
    // FUN_00591ef0 at creation (chain mode): the caster's new +0x308, toward the first hop.
    float first_heading() const {
        return ray || hops.empty() ? heading : std::atan2(hops[0].pos.z - pos.z, hops[0].pos.x - pos.x);
    }
    // FUN_00592af0's test of one monster (origin `p`, body radius `r` = its +0x68) against the ray.
    bool ray_reaches(const Vec3& p, float r) const {
        const Vec3 v{p.x - pos.x, p.y - pos.y, p.z - pos.z};
        const float t = v.x * std::cos(heading) + v.z * std::sin(heading);
        const float q = v.x * v.x + v.y * v.y + v.z * v.z;
        const float r2 = (r + 0.4f) * (r + 0.4f);          // 0x6bd2d4
        if (t < 0.0f && q > r2) return false;              // behind and not overlapping
        if (q - t * t > r2) return false;                  // the ray misses the sphere
        const float s = std::sqrt(r2 - (q - t * t));
        const float tn = q > r2 ? t - s : t + s;
        return tn * tn < 25.0f;                            // 0x6bd9c0: reach 5.0
    }
    // FUN_00592d00, one sim tick. live(id, &pos): is that monster still there with HP >= 1, and its origin;
    // rnd(): one word of the game's xorshift; hit(id): the arrival's direct hit. Returns true when the host is to
    // run the ray scan this tick (ray mode).
    template <class Live, class Rnd, class Hit>
    bool tick(Live&& live, Rnd&& rnd, Hit&& hit) {
        if (dead) return false;
        if (delay != 0) { --delay; return false; }
        if (ray) {
            if (scans == 0) { dead = true; return false; }
            --scans;
            return true;
        }
        if (cur >= n && hops[std::size_t(std::max(cur - 1, 0))].timer == 0) {
            if (bolted) for (Bolt& b : bolt) { b.driven = false; b.life = 12; }   // let go: they fade over 12 updates
            dead = true;
            return false;
        }
        int alive = 0;
        for (int i = 0; i < n; ++i) {
            Hop& e = hops[std::size_t(i)];
            if (e.id >= 0) {
                ++alive;
                Vec3 p{};
                if (!live(e.id, &p)) { e.id = -1; continue; }
                e.pos = p;
            }
            if (e.timer != 0) --e.timer;
        }
        if (alive == 0) { n = cur; return false; }
        if (cur >= n) return false;
        Hop& e = hops[std::size_t(cur)];
        if (e.id < 0 && !e.lost) {                         // a lost target becomes a random point near the head
            e.lost = true;
            if (cur >= 1) {
                const int v = int(rnd() >> 24);
                const std::uint32_t bits = (rnd() & 0x7fffffu) | 0x3f800000u;
                float u;
                std::memcpy(&u, &bits, 4);
                e.pos = {pos.x + cos_table(v) * 1.5f, pos.y + ((u - 1.0f) * 1.5f - 0.5f),
                         pos.z + cos_table((v - 0x40) & 0xff) * 1.5f};
            }
        }
        if (bolted)                                        // the head feeds both lines [BYTES 0x5930b0..0x593311]
            for (Bolt& b : bolt) {
                b.feed(pos, water, rnd);
                if (flick == 0) {                          // ~45.8 % a line a tick: one side line, then 3 ticks off
                    if ((rnd() & 0xfffe0000u) < 0x75300000u) { flick = 3; b.forks = 1; }
                } else --flick;
            }
        const Vec3 d{e.pos.x - pos.x, e.pos.y - pos.y, e.pos.z - pos.z};
        const float q = d.x * d.x + d.y * d.y + d.z * d.z;
        if (q <= 0.7225f) {                                // 0x6bd38c: arrival
            if (e.id >= 0) hit(e.id);
            e.timer = 5;
            pos = e.pos;
            if (cur + 1 < n && e.id == hops[std::size_t(cur + 1)].id) {   // the next hop is the same target: step aside
                const int v = int(rnd() >> 24);
                pos.x += cos_table(v) * 1.0625f;           // 0x6bd45c
                pos.z += cos_table((v - 0x40) & 0xff) * 1.0625f;
            }
            ++cur;
        } else {
            const float l = std::sqrt(q);
            if (l > 0.0f) pos = {pos.x + d.x * (0.85f / l), pos.y + d.y * (0.85f / l), pos.z + d.z * (0.85f / l)};   // 0x6bd3bc
        }
        return false;
    }
};

}  // namespace cleria::extra
