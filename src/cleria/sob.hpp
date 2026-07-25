// SOB parser — per-room object placement. See docs/formats/SIDECARS.md.
// Each record places a YMO model with a transform. The room's own geometry is
// just another record.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

struct SobObject {
    std::string ymo_path;   // e.g. "data\\map\\s_00\\s_0000\\s_0000.ymo"
    std::string name;       // "box_01", "save_01", ...
    Vec3 position;          // metres
    Vec3 rotation;          // radians, Euler XYZ
    Vec3 scale;             // usually (1,1,1)

    // ---- behaviour block (+0x148..+0x1c8) ------------------------------------------------
    // What makes an object interactive. Verified across all 1114 SOB records in the corpus
    // with zero counterexamples; the field semantics are [H-high] (the SOB loader itself,
    // FUN_00433110, is not dumped yet).
    //
    // `trigger` is a bitfield selecting HOW the object fires its script:
    //   1 = attackable body   (breakables: @Break_5/30/100, s_pole01, s_Syokudai00..)
    //   2 = step-on plate     (*_fumi switches, @Jump_001, @Fall_001)
    //   4 = touch volume      (Touch_kekkai, s_mirror01..16)
    //   6 = 2|4 touch-damage  (@Touch_Toge spikes, Touch_Yari)
    //   8 = action button     (s_box01..04 chests, @Save statues)
    //   0 = inert scenery
    // `script` is the attached XSO; a leading '@' means it lives in MAP/S_COMMON/ rather
    // than beside the room. i0/i1/f0 are bound to the script's $I0/$I1/$F0 variables (the
    // operand resolver maps a "$..." string operand onto the GoScript arg block), and the
    // owning object's own name is $S0. For @Break_* those are the two drop item ids; for
    // @Touch_Toge the same field is damage instead — $I0's meaning is per-script.
    std::int32_t trigger = 0;   // +0x150
    std::int32_t rflags = 0;    // +0x14c render/behaviour bits
    std::int32_t enabled = 0;   // +0x154
    std::int32_t i0 = 0;        // +0x160 -> $I0
    std::int32_t i1 = 0;        // +0x164 -> $I1
    float f0 = 0;               // +0x1c8 -> $F0
    std::string script;         // +0x188
    std::string label;          // +0x1a8 (save-point caption / alias, SJIS)

    bool attackable() const { return (trigger & 1) != 0; }
    bool step_on()    const { return (trigger & 2) != 0; }
    bool touch()      const { return (trigger & 4) != 0; }
    bool actionable() const { return (trigger & 8) != 0; }

    Mat4 world() const { return compose_trs(position, rotation, scale); }
};

struct Sob {
    std::vector<SobObject> objects;
    static Sob parse(std::span<const std::uint8_t> bytes);  // throws on bad data
};

}  // namespace cleria
