// Actor-vs-actor body push: FUN_0045e460, the pass every actor's tick runs right after its
// integrator FUN_0045a510 (player FUN_0046fba0 L114, every monster handler, the NPC puppet
// FUN_0045f250). Spec: build/ai_spec/HITVOLUME.md §5.1 — every value read from the bytes
// (0x45e460..0x45e743).
//
// It never damages and never blocks outright: an overlap adds a velocity to knock channel 2
// (+0x344/+0x34c), which the movers spend over the next ticks (enemy_mover.hpp mover_post,
// hit.hpp step_knockback_player). So bodies are soft: they part over a few ticks.
//
//   if !(self.kind & 0x80) return                        ; +0x60 bit 7: the body is live
//   for o in the actor list (0x730098), in order:
//     skip unless o.kind & 0x80, self.mask & o.kind, o != self
//     skip unless FUN_004f9e90(self body, o body)       ; angle = atan2(dz, dx), self -> o
//     skip if self.bank already holds self's token      ; FUN_004faac0(this = self, self+0x60):
//                                                        ; token +0x80, timer +0x84 = 1, so ONE
//                                                        ; push per actor per tick (bank shared
//                                                        ; with the hit tokens; a full bank allows)
//     s = max(0.1, self+0x310 * 0.7)                     ; 0x6bd19c, 0x6bd36c; +0x310 = speed
//     c = (cos, sin)(angle) * s
//     self+0x3bc |= 8 ; o+0x3bc |= 8                     ; "touching" (no reader found)
//     if self.kind & 1 (the player): nothing else        ; the player pushes nobody
//     if o.section(+0x994) != 9: o.push += c             ; o is shoved away from self
//     if o.kind & 1 (the player):
//       only in the burst sections (char 1: 0x139; char 3 or 0: 0x13d):
//         o.push -= c ; self.push -= c                   ; the player holds, self gives way
//     else if self.section != 9: self.push -= c          ; two non-players part symmetrically
//
// Masks (the factory, HITVOLUME §4.1): player 0x81/0x3c, NPC class 4 0x84/0x3f, monster
// 0x88/0x2f. So monsters and NPCs push the player and each other; the player pushes nobody.
// [not modelled] the second loop over the SOB body list 0x730224 (self pushed out of scene
// objects at max(0.1, +0x310 * 1.1), its channel 1 zeroed).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "cleria/hit.hpp"
#include "cleria/hit_volume.hpp"

namespace cleria::bodypush {

// The +0x64 mask the factory pairs with each +0x60 kind byte (FUN_00459410).
inline std::uint8_t mask_of_kind(std::uint8_t kind) {
    switch (kind) {
        case 0x81: return 0x3c;
        case 0x84: return 0x3f;
        case 0x88: return 0x2f;
        default: return 0;
    }
}

// Where a push lands: an actor's knock channel 2 x / z (the player's Impulse, a mover's Vec3).
struct Push {
    float* x = nullptr;
    float* z = nullptr;
    static Push of(Impulse& k) { return {&k.x, &k.z}; }
    static Push of(Vec3& k) { return {&k.x, &k.z}; }
    explicit operator bool() const { return x != nullptr; }
    void add(float dx, float dz) const { if (x) { *x += dx; *z += dz; } }
};

struct Body {
    HitShape shape;             // +0x60: the hurt record (CASTINFO +0x30 / +0x58 / +0x5c / +0x54)
    std::uint8_t kind = 0;      // +0x60 low byte, bit 7 = live (cleared at death, by the brains)
    float speed = 0;            // +0x310: the pusher's current speed
    int section = -1;           // +0x994: the ASO section (9 = down: not pushed)
    std::uint32_t token = 0;    // +0x80: this actor's body token
    RehitBank* bank = nullptr;  // this actor's 10-slot bank (+0x8c/+0x90)
    Push push;                  // knock channel 2 (+0x344/+0x34c); empty = not pushed by anyone
    bool touched = false;       // +0x3bc & 8
    std::uint8_t mask_clear = 0;  // bits a brain cleared from its +0x64 (M980 INIT: &= ~8, pushes no monsters)
};

inline constexpr float kMinPush = 0.1f;     // 0x6bd19c
inline constexpr float kSpeedPush = 0.7f;   // 0x6bd36c

// Does the player's current section let it hold its ground (0x45e59b..0x45e5d8)?
inline bool player_holds(int character, int section) {
    if (character == 1) return section == 0x139;
    if (character == 3 || character == 0) return section == 0x13d;
    return false;
}

// One FUN_0045e460 call for `self` over the actor list `all` (n entries, self among them, in
// the engine's list order). Returns true when a push was applied.
inline bool pass(Body& self, Body* const* all, std::size_t n, int character) {
    if (!(self.kind & 0x80)) return false;
    const std::uint8_t mask = std::uint8_t(mask_of_kind(self.kind) & ~self.mask_clear);
    bool pushed = false;
    for (std::size_t i = 0; i < n; ++i) {
        Body& o = *all[i];
        if (!(o.kind & 0x80) || !(mask & o.kind) || &o == &self) continue;
        float ang = 0;
        if (!overlap(self.shape, o.shape, &ang)) continue;
        if (self.bank && self.bank->check_and_record(self.token, 1)) continue;
        const float s = std::max(kMinPush, self.speed * kSpeedPush);   // maxss(0.1, speed*0.7)
        const float cx = float(std::cos(double(ang))) * s;             // FUN_0066fd70 (cos, double)
        const float cz = float(std::sin(double(ang))) * s;             // FUN_00670d30 (sin, double)
        self.touched = o.touched = true;
        pushed = true;
        if (self.kind & 1) continue;                                   // 0x45e545: the player
        if (o.section != 9) o.push.add(cx, cz);
        if (o.kind & 1) {
            if (!player_holds(character, o.section)) continue;
            o.push.add(-cx, -cz);
            self.push.add(-cx, -cz);
            continue;
        }
        if (self.section != 9) self.push.add(-cx, -cz);
    }
    return pushed;
}

}  // namespace cleria::bodypush
