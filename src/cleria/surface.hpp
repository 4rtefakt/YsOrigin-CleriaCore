// The footstep surface table — the exe's own data, at `.data 0x6d5da0`, stride 16,
// 21 entries (the 0x15 bound is proven by the lookup loop).
//
// Surfaces are looked up BY NAME (`FUN_00439700`: a linear __stricmp over the 21
// entries), and the names are exactly the material names on the `_.YMO` collision
// hulls. That is the whole join: collision material name -> surface -> EFFECTDEF id.
//
// This is engine data, not asset data — it lives in the executable, so it is
// reproduced here rather than read from `assets/`.
#pragma once
#include <cstdint>
#include <string_view>

namespace cleria {

struct SurfaceType {
    const char* name;             // +0x00
    std::uint16_t base_sound_id;  // +0x06  EFFECTDEF id of variant 0; 0 => silent
    // +0x08 / +0x0A — the STEP INTERVAL IN TICKS, walking and running. An earlier
    // pass recorded these as "a (always 20)" and "b (always 13)" and marked them
    // [UNKNOWN]. They are the footstep cadence: at 60 Hz, 0.333 s per step walking
    // and 0.2167 s running, uniform across every audible surface.
    std::uint16_t walk_ticks;
    std::uint16_t run_ticks;
    std::uint8_t jitter;          // +0x0C  timer += rng % jitter (0 => none)
    std::uint8_t variant_mask;    // +0x0D  3 => 4 variants, 0 => 1
};

// Index == the surface id the exe uses. Order is load-bearing.
inline constexpr SurfaceType kSurfaceTable[] = {
    //                      se   walk  run  jit mask
    {"none",                 0,   10,  10,   0, 0},   //  0  silent
    {"stone",               44,   20,  13,   0, 3},   //  1  f_stone0..3
    {"stopper",              0,   10,  10,   0, 0},   //  2  silent - invisible wall
    {"water",               48,   20,  13,   2, 3},   //  3  f_splash0..3
    {"earth",               28,   20,  13,   0, 3},   //  4  f_earth0..3
    {"wood",                20,   20,  13,   0, 3},   //  5  f_wood0..3
    {"grass",               32,   20,  13,   2, 3},   //  6  f_grass0..3
    {"metal",               36,   20,  13,   0, 3},   //  7  f_metal0..3
    {"carpet",              24,   20,  13,   0, 3},   //  8  f_carpet0..3
    {"sand",                40,   20,  13,   1, 3},   //  9  f_sand0..3  (jitter 1 => always +0)
    {"snow",                40,   20,  13,   0, 3},   // 10  reuses sand
    {"ice",                 28,   20,  13,   0, 3},   // 11  reuses earth
    {"poison",              48,   20,  13,   2, 3},   // 12  reuses water
    {"lava",                64,   20,  13,   0, 3},   // 13  f_lava0..3
    {"pool",                48,   20,  13,   2, 3},   // 14  reuses water
    {"ironfence",           36,   20,  13,   0, 3},   // 15  reuses metal
    {"woodfence",           20,   20,  13,   0, 3},   // 16  reuses wood
    {"blood",               48,   20,  13,   2, 3},   // 17  reuses water
    {"sand2",               40,   20,  13,   1, 3},   // 18  reuses sand
    {"reserve19",           52,   20,  13,   2, 3},   // 19  f_rstone0..3
    {"reserve20",            0,   20,  13,   2, 3},   // 20  silent
};
inline constexpr int kSurfaceCount = int(sizeof(kSurfaceTable) / sizeof(kSurfaceTable[0]));

// Surface id for a material name. Case-insensitive, matching the exe's __stricmp.
//
// **An unknown name resolves to 1 (`stone`), not to -1 and not to silence.** The
// standalone lookup FUN_00439700 does return -1, but it is DEAD CODE (zero xrefs).
// The live lookup is that routine inlined into FUN_005610e0, the collision-mesh
// builder, where the compiler folded the `return -1` into a jump straight onto the
// clamp:
//
//   0056137b  JC  0x00561362          ; loop, 21 entries
//   0056137d  JMP 0x00561383          ; not found ->
//   0056137f  TEST ESI,ESI            ; found: idx >= 0?
//   00561381  JNS 0x00561388
//   00561383  MOV ESI,0x1             ; <-- both paths land here: default = stone
//
// So `default` (the 2nd most common collision material), `dafault` (Falcom's typo),
// `door`, `10switch`, `10yuka04` and `10_wall_01` all play stone. Silence comes from
// `base_sound_id == 0` (`none` / `stopper` / `reserve20`) — never from a bad index.
int surface_index(std::string_view name);

// The default the exe clamps to when a name matches nothing.
inline constexpr int kSurfaceDefault = 1;  // "stone"

// What the engine actually stores: resolved once at mesh-build time and cached as a
// u16 per triangle (FUN_005610e0 @0x5613f4 writes `param_1[+0x14][tri] = idx`), not
// re-resolved per footstep.
inline int surface_index_or_default(std::string_view name) {
    const int i = surface_index(name);
    return i < 0 ? kSurfaceDefault : i;
}

// True for surfaces that exist only to block movement and are never floors or
// sound sources. `stopper` is the invisible wall.
inline bool surface_is_stopper(int idx) { return idx == 2; }

// The EFFECTDEF id a step on `surface` plays, or -1 for silence.
//
//   FUN_0045d600(actor, surface):
//     base = table[surface].base_sound_id
//     if (base == 0) return;                 // 'none' / 'stopper' are silent
//     n = actor->step_counter++;
//     play(base + (n & table[surface].variant_mask), n);
//
// `step_n` is the actor's own monotonic step counter — the variant cycles with it
// rather than being random, so consecutive steps alternate deterministically.
int footstep_sound(int surface, unsigned step_n);

// Ticks to wait before the NEXT step on this surface. `running` picks run_ticks over
// walk_ticks. `jitter_roll` should be a fresh random u32; the exe adds `rng % jitter`
// (water/grass/poison/pool/blood use jitter 2; sand/sand2 use 1, which always adds 0).
//
// The cadence is a COUNTDOWN IN TICKS, reloaded from the surface record — not a
// distance accumulator and not an animation event (FUN_0045c490 @0x45ced1: test the
// enable byte, decrement the timer, and only fire when it hits zero). It is also
// independent of the animation cycle: walking is 20 ticks/step against C_010's
// 48-tick walk cycle, so steps deliberately do NOT line up with footplants.
inline int footstep_interval(int surface, bool running, std::uint32_t jitter_roll) {
    if (surface < 0 || surface >= kSurfaceCount) return 20;
    const SurfaceType& s = kSurfaceTable[surface];
    int t = running ? s.run_ticks : s.walk_ticks;
    if (s.jitter) t += int(jitter_roll % s.jitter);
    return t;
}

}  // namespace cleria
