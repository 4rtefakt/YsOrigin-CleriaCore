// The emotion bubbles of op 0x148 Chr_Emotion (build/event_re/CHR_OPS.md §1): GoEmotion objects
// (pool 3, cap 32) that follow their actor's head, play a cell animation from IMAGE/EMOTION.DDS
// (256x128, 32x32 cells, 8 x 4), fade in over 15 ticks, hold L[kind] and fade out over 25.
//  - spawn: FUN_005710d0 (an actor's older bubbles still fading in / holding jump to their fade-out)
//  - step:  FUN_004ffab0, once per logic tick (not frozen by a Fix; only the menu pause stops it)
//  - quad:  the 0.75 x 0.75 world quad, billboard-right offset C[kind], drawn with the PREVIOUS
//           tick's alpha (the update writes the vertex colour before recomputing it)
#pragma once
#include <cstdint>
#include <vector>

namespace cleria {

struct Emotion {
    int owner = 0;        // +0x120: the actor (instance id; 13 = the player)
    int kind = 0;         // +0x124: 0..8
    float yoff = 0;       // +0x128: the op's float (XMM3)
    int age = 0;          // +0x12c: logic ticks
    float alpha = 0;      // +0x130
    std::uint8_t a8 = 0;  // the vertex alpha written this tick (from the previous alpha)
    int cell = 0;         // the EMOTION.DDS cell written this tick
    bool dead = false;
};

inline constexpr int kEmotionKinds = 9;
inline constexpr int kEmotionCap = 32;          // pool 3

// FUN_005710d0 steps 2..6: shorten `owner`'s older bubbles, then add one (if the pool has room).
void spawn_emotion(std::vector<Emotion>& list, int owner, int kind, float yoff);
// FUN_004ffab0's logic-tick part: age, cell, the vertex alpha, the new alpha or death.
void step_emotion(Emotion& e);

// The quad in billboard space (x along the camera-right, y up from the bubble base, which is the
// owner's origin + 0.75 + yoff) and its UVs.
struct EmotionQuad { float x0, x1, y0, y1, u0, v0, u1, v1; std::uint8_t a8; };
EmotionQuad emotion_quad(const Emotion& e);

}  // namespace cleria
