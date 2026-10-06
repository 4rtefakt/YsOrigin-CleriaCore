// The region camera and its cutscene ops (build/event_re/CAMERA_OPS.md), as a pure step machine:
//  - a 64-slot table DAT_0076a150 (stride 0x454), one SCM record per slot at room load; the ops edit
//    slots in place, and the edits last until the room reloads its camera file;
//  - FUN_0042a3d0 once per 60 Hz step: look point, scroll ramp, the region pick (sticky-aware, first
//    containing volume of 1..count-1, the forced slot over it), a region change starting a blend
//    (instant when c0 is set or the slot's own rate >= 1), the evaluation of the current and "from"
//    slots, the smoothstep blend of their evaluated eye / target / offset / pitch / fov;
//  - FUN_0042a080: the camera-space offset (+ the shake) translates eye and target together;
//  - the ops 0xEB CopyCamera_Set, 0xEC Camera_Change, 0xC5 Camera_Set, 0xC6 Camera_Return,
//    0xC3 Scroll_MoveTo, 0xC4 Scroll_Return, 0xCF Shake.
// [not modelled] mode-2 points (no corpus users), the mode-3 TARGET orbit (4 records; evaluated as
// its fixed point), the boss camera hooks (FUN_0042bb10's boss path), the render interpolation.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "cleria/mathx.hpp"
#include "cleria/scm.hpp"

namespace cleria {

class CamRig {
  public:
    static constexpr int kSlots = 64;

    // FUN_0042b1a0 + the SCM install: slot i = record i; -1 forced, rate 0.1, t 1, c0 set, cur =
    // from = 0, the scroll zeroed. The copy-slot index (0x76a200) is NOT reset.
    void reset(const Scm& scm);
    bool ok() const { return count_ > 0; }

    void copy_set(int id);                                                         // 0xEB
    void change(float dist, float yaw_deg, float pitch_deg, float fov_deg, int frames);   // 0xEC
    void set(int id, int frames);                                                  // 0xC5
    void ret(int frames);                                                          // 0xC6
    void scroll_moveto(const Vec3& target, int frames, int prio);                  // 0xC3
    void scroll_return(int frames, int prio);                                      // 0xC4
    void shake(const Vec3& amp, int frames);                                       // 0xCF
    void hard_cut() { c0_ = true; }

    struct Out { Vec3 eye{}, target{}; float fov = 0.5236f, pitch = 0; };
    // One 60 Hz step. `player` = the player's origin (the volume test), `look` = the live look point
    // L. `rng` = the global xorshift128 (the shake draws three per step while it runs).
    Out step(const Vec3& player, const Vec3& look, const std::function<std::uint32_t()>& rng);

    // Introspection (tests).
    int current() const { return cur_; }
    int from() const { return from_; }
    float blend_t() const { return t_; }
    int forced() const { return forced_; }
    const CameraRegion& slot(int i) const { return slots_[std::size_t(i)].rec; }

  private:
    struct Slot {
        CameraRegion rec;
        bool populated = false;
        Vec3 target{}, eye{};        // +0x38 / +0x44: evaluated this step
        float lp_y = 0;              // the target-Y low-pass state
        bool snap = true;            // +0x5c: snap the low-pass on the next evaluation
    };
    void evaluate(Slot& s, const Vec3& look, const Vec3& scrolled);

    std::vector<Slot> slots_ = std::vector<Slot>(kSlots);
    int count_ = 0;                  // 0x76a154
    int cur_ = 0, from_ = 0;         // 0x76a158 / 0x76a15c
    float rate_ = 0.1f, t_ = 1.0f;   // 0x76a1b8 / 0x76a1bc
    bool c0_ = true;                 // 0x76a1c0
    int forced_ = -1;                // 0x76a1f8 (-2 = returning)
    float forced_rate_ = 1.0f;       // 0x76a1fc
    int copy_ = 0;                   // 0x76a200 (never reset)
    // the scroll
    Vec3 sc_target_{}, sc_base_{};   // 0x76a1d0 / 0x76a1dc
    float sc_dest_ = 0, sc_t_ = 0, sc_rate_ = 0;   // 0x76a1e8 / 1ec / 1f0
    int sc_prio_ = 0;                // 0x76a1f4
    // the shake
    Vec3 sh_amp_{}, sh_off_{};       // 0x76a594 / 0x76a5a0
    int sh_frames_ = 0;              // 0x76a5ac
    // the published values
    Out pub_{};
    Vec3 last_look_{};               // 0x76a1c4: L, the Scroll_Return base
};

}  // namespace cleria
