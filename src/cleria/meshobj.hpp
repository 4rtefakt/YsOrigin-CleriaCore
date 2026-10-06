// Scripted map objects (GoMesh) at runtime: position, the Mesh_MoveTo state machine, clip
// playback with its play counter, and the flags the script ops toggle. Ported from
// build/event_re/WAITOPS.md §4 (FUN_00572d50/dc0 start a move, FUN_00503190 steps it;
// FUN_00572f70 requests a clip, FUN_005f5c80 applies it, FUN_005f5e10 / FUN_005f6000
// advance it). Pure state: the host owns the geometry and re-bakes an object when its
// position or pose changes.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// One animation section of an object's YMO (the in-memory 0xA8 record at model+0x298):
// start/end frame, next (>= 0 chain to that section, -2 loop, other negative = stop) and the
// playback rate in frames per step.
struct MeshClip {
    std::string name;
    std::int32_t start = 0, end = 0, next = -1;
    float rate = 1.0f;
};

struct MeshObj {
    std::string name;                 // lower-case (FUN_00434d00 matches case-insensitively)
    Vec3 spawn{};                     // +0x3d0: the SOB placement
    Vec3 pos{};                       // +0x50
    // Mesh_MoveTo / MoveOffset (+0x3e8 dest, +0x3f4 radius, +0x3f8 speed; 0 = idle)
    Vec3 dest{};
    float radius = 0, speed = 0;
    // Clip playback (the mesh's anim cursor +0x26c)
    std::vector<MeshClip> clips;
    int sect = -1;                    // cur+0x18
    float time = 0;                   // cur+0x20
    float time_scale = 1;             // cur+0x24 (Mesh_SetTimeScale)
    std::uint32_t count = 0;          // cur+0x1c: the play counter Wait_MeshMotionFinish reads
    std::string pending;              // +0x424: a clip requested by SetMotion, applied in the update
    bool pending_blend = false;       // +0x444
    std::int32_t pending_start = 0;   // +0x448
    bool frozen = false;              // +0x28f: Mesh_StopMotion(name, 0)
    bool sleeping = false;            // +0x298 & 0x10: Mesh_Sleep(name, 1) skips the update
    bool event_on = true;             // Mesh_EnableEvent: its touch script may start
    bool dirty_pos = false, dirty_pose = false;   // the host must re-bake this tick
    Vec3 rot{};                       // the placement rotation (radians, as SobObject): boss facing
    // Mesh_Fall 0x18A (FUN_00573be0 -> FUN_00503190): +0x44c start y, +0x450 velocity,
    // +0x454 restitution (0 = not falling).
    float fall_start = 0, fall_vel = 0, fall_e = 0;
    // Mesh_SetAlphaTarget 0x16E (FUN_00572b40): +0x418 alpha, +0x41c target, +0x420 step.
    float alpha = 1, alpha_target = 1, alpha_step = 0;
    // A full placement a brain owns (boss02's pieces, obj+0x20): the re-bake uses it instead of
    // pos / rot (pos is still kept as its row 3 for the platform carry).
    bool has_world = false;
    Mat4 world{};
    bool no_collide = false;          // the host drops its collision this tick (boss02's one-way entries)
};

class MeshWorld {
  public:
    void clear() { objs_.clear(); }
    MeshObj& add(const std::string& name, const Vec3& spawn, std::vector<MeshClip> clips);
    MeshObj* find(const std::string& name);
    const MeshObj* find(const std::string& name) const;
    std::unordered_map<std::string, MeshObj>& all() { return objs_; }

    // Mesh_MoveTo 0x170 (offset = false) / Mesh_MoveOffset 0x171 (offset = true).
    void move_to(const std::string& name, const Vec3& to, float radius, float speed, bool offset);
    // Mesh_SetMotion 0x175: the counter resets now, the clip switches in the next update.
    void set_motion(const std::string& name, const std::string& clip, bool blend, std::int32_t start);
    // Mesh_StopMotion 0x16d: (name, 0) freezes the advance, (name, 1) resumes it.
    void stop_motion(const std::string& name, std::int32_t on);
    void sleep(const std::string& name, bool on);                // Mesh_Sleep 0x16a
    void set_position(const std::string& name, const Vec3& p);   // Mesh_SetPosition 0x173
    void reset_position(const std::string& name);                // Mesh_ResetPosition 0x178
    void enable_event(const std::string& name, bool on);         // Mesh_EnableEvent 0x176
    void set_time_scale(const std::string& name, float s);       // Mesh_SetTimeScale 0x17b
    void fall(const std::string& name, float e, float h);        // Mesh_Fall 0x18a
    // FUN_005f5c80 called by a brain: the clip switches now (not deferred like Mesh_SetMotion),
    // `start` frames into the section. set_clip_index takes the section index instead.
    void set_clip_now(const std::string& name, const std::string& clip, std::int32_t start = 0);
    void set_clip_index(const std::string& name, int idx, std::int32_t start = 0);
    void alpha_target(const std::string& name, float a, int frames);   // Mesh_SetAlphaTarget 0x16e

    // One live step of every object (FUN_00503ef0): the anim advance, then the move, then
    // the pending clip. Marks objects whose position / pose changed.
    void step();

    // The wait predicates. `found` = false when the object does not exist (the original
    // then rewinds forever).
    bool moving(const std::string& name, bool* found) const;                          // 0x104
    bool motion_finished(const std::string& name, std::uint32_t n, bool* found) const;  // 0x105

  private:
    static void apply_clip(MeshObj& m, int idx, std::int32_t start);   // FUN_005f5c80
    static void set_time(MeshObj& m, float t);                         // FUN_005f5e10
    std::unordered_map<std::string, MeshObj> objs_;
};

}  // namespace cleria
