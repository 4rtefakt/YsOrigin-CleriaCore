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
    const char* name;
    std::uint16_t base_sound_id;  // EFFECTDEF id of variant 0; 0 => silent
    std::uint8_t rand_mod;        // if != 0, an unrelated randomised field (see below)
    std::uint8_t variant_mask;    // 3 => 4 variants, 0 => 1
};

// Index == the surface id the exe uses. Order is load-bearing.
inline constexpr SurfaceType kSurfaceTable[] = {
    {"none",       0, 0, 0},   //  0  silent
    {"stone",     44, 0, 3},   //  1  f_stone0..3
    {"stopper",    0, 0, 0},   //  2  silent — the invisible-wall marker
    {"water",     48, 2, 3},   //  3  f_splash0..3
    {"earth",     28, 0, 3},   //  4  f_earth0..3
    {"wood",      20, 0, 3},   //  5  f_wood0..3
    {"grass",     32, 2, 3},   //  6  f_grass0..3
    {"metal",     36, 0, 3},   //  7  f_metal0..3
    {"carpet",    24, 0, 3},   //  8  f_carpet0..3
    {"sand",      40, 1, 3},   //  9  f_sand0..3
    {"snow",      40, 0, 3},   // 10  reuses sand
    {"ice",       28, 0, 3},   // 11  reuses earth
    {"poison",    48, 2, 3},   // 12  reuses water
    {"lava",      64, 0, 3},   // 13  f_lava0..3
    {"pool",      48, 2, 3},   // 14  reuses water
    {"ironfence", 36, 0, 3},   // 15  reuses metal
    {"woodfence", 20, 0, 3},   // 16  reuses wood
    {"blood",     48, 2, 3},   // 17  reuses water
    {"sand2",     40, 1, 3},   // 18  reuses sand
    {"reserve19", 52, 2, 3},   // 19  f_rstone0..3
    {"reserve20",  0, 2, 3},   // 20  silent
};
inline constexpr int kSurfaceCount = int(sizeof(kSurfaceTable) / sizeof(kSurfaceTable[0]));

// Surface id for a material name, or -1 if it names no surface. Case-insensitive,
// matching the exe's __stricmp.
int surface_index(std::string_view name);

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

}  // namespace cleria
