// SCM — per-room camera regions.
//
// The engine has ONE "region" concept (a named, rotatable AABB) shared by fog
// (SFO) and cameras (SCM) — its RTTI still names `RegionBase`, `RegionCamera`,
// `RegionFog`. Each frame the player is tested against every region; the first
// hit wins, else record 0, and changes blend rather than cut.
//
// Each 1040-byte record is two 0x1D4 `CameraPoint`s — A = the look-at target,
// B = the eye — followed by the region header. The camera the game actually
// ships: FOV ~30 deg, eye pitch +30 deg, distance 30, yaw 0 (camera on -Z).
//
// Reverse-engineered from yso_win.exe: loader FUN_004338c0, update FUN_0042a3d0,
// point evaluator FUN_0042b6e0. See build/source_map/SCM_CAMERA.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// One end of the camera (target or eye).
struct CameraPoint {
    std::uint32_t mode = 0;      // 0 = fixed, 1 = follow player, 3 = orbit
    float yaw = 0;               // radians (mode 3)
    float pitch = 0;             // radians (mode 3) — true depression angle
    float dist = 0;              // (mode 3)
    std::uint32_t flags = 0;     // != 0 -> yaw tracks the player
    Vec3 fixed_pos{};            // (mode 0)
};

struct CameraRegion {
    CameraPoint target;          // point A
    CameraPoint eye;             // point B
    float fov = 0;               // vertical FOV, radians
    float blend = 0.015f;        // per-frame lerp increment
    std::string name;            // CP932 (e.g. "基本カメラ")
    Vec3 origin{};
    Vec3 aabb_min{}, aabb_max{}; // relative to origin
    float region_yaw = 0;
    bool sticky = false;

    bool contains(const Vec3& p) const;
};

struct Scm {
    std::vector<CameraRegion> regions;  // [0] = room default

    static Scm parse(const std::vector<std::uint8_t>& bytes);
    bool ok() const { return !regions.empty(); }
    const CameraRegion* pick(const Vec3& p) const;
};

// Where the eye sits for `r`, looking at `target`, with the player at `player`.
// Mirrors FUN_0042b6e0's orbit math.
Vec3 scm_eye(const CameraRegion& r, const Vec3& target, const Vec3& player);

}  // namespace cleria
