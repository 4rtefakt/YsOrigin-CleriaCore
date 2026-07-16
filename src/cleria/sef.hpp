// SEF — per-room scene effects (torch flames, spikes, waterfall spray, orbs...).
//
// Each record places one effect from the global effect table by id. Effect id
// 200 = `map\syokudai_honou.eft` (燭台の炎, the torch-stand flame) and accounts
// for 471 placements across the game.
//
// Reverse-engineered from yso_win.exe: loader FUN_00433360, spawn FUN_00450410.
// The record's 0x40-byte tail is never read (0/1603 non-zero corpus-wide).
// See docs/formats/SIDECARS.md and build/ef2_re/EF2_FORMAT.md.
#pragma once
#include <cstdint>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

struct SefEffect {
    std::uint32_t id = 0;   // index into the effect table (eftlist)
    Vec3 position{};        // world
    Vec3 rotation{};        // RADIANS (the file stores degrees; the loader converts)
};

struct Sef {
    std::vector<SefEffect> effects;

    static Sef parse(const std::vector<std::uint8_t>& bytes);
    bool ok() const { return !effects.empty(); }
};

// The torch flame.
constexpr std::uint32_t kEffectTorchFlame = 200;

}  // namespace cleria
