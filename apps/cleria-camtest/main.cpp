// cleria-camtest — the region camera rig (src/cleria/camrig) vs build/event_re/CAMERA_OPS.md,
// on synthetic SCM records (no assets needed).
#include <cmath>
#include <cstdio>

#include "cleria/camrig.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[cam] %-72s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A follow region: A = mode 1 (the look point), B = mode 3 orbit.
static CameraRegion region(float yaw, float pitch, float dist, float fov, float blend,
                           Vec3 org = {}, Vec3 mn = {}, Vec3 mx = {}) {
    CameraRegion r;
    r.target.mode = 1;
    r.eye.mode = 3; r.eye.yaw = yaw; r.eye.pitch = pitch; r.eye.dist = dist;
    r.fov = fov; r.blend = blend;
    r.origin = org; r.aabb_min = mn; r.aabb_max = mx;
    return r;
}

int main(int, char**) {
    const auto rng = [] { return 0x12345678u; };
    const Vec3 player{0, 0, 0}, look{0, 1, 0};
    {   // reset -> the first step is a hard cut onto the region the player stands in
        Scm scm;
        scm.regions.push_back(region(0, 0.5f, 20, 0.5f, 0.015f));
        scm.regions.push_back(region(1.0f, 0.6f, 25, 0.6f, 0.02f, {0, 0, 0}, {-5, -5, -5}, {5, 5, 5}));
        CamRig rig; rig.reset(scm);
        const CamRig::Out o = rig.step(player, look, rng);
        check(rig.current() == 1 && rig.blend_t() == 1.0f, "reset: the first step cuts onto the containing region 1");
        const Vec3 want = scm_eye(scm.regions[1].eye, look, look);
        check(near(o.eye.x, want.x) && near(o.eye.y, want.y) && near(o.eye.z, want.z) && near(o.fov, 0.6f),
              "settled: the published eye / fov are the region's own");
    }
    {   // Camera_Change: clone into the copy slot, blend from it over `frames`, edit persists
        Scm scm;
        scm.regions.push_back(region(0, 0.5f, 20, 0.5f, 0.015f));
        CamRig rig; rig.reset(scm);
        rig.step(player, look, rng);
        rig.copy_set(9);
        rig.change(30.0f, -1.0f, -1.0f, 40.0f, 4);
        check(rig.from() == 9 && rig.blend_t() == 0.0f, "0xEC: blend from the copy slot, t = 0");
        check(near(rig.slot(0).eye.dist, 30) && near(rig.slot(0).eye.yaw, 0) && near(rig.slot(0).fov, 40 * 0.017453292f),
              "0xEC: dist / fov written, -1 keeps the yaw");
        check(near(rig.slot(9).eye.dist, 20), "0xEC: the copy slot holds the pre-edit values");
        CamRig::Out o = rig.step(player, look, rng);                      // t = 0.25 -> smooth 0.15625
        const float e = 0.15625f, fov = 0.5f + (40 * 0.017453292f - 0.5f) * e;
        check(near(o.fov, fov), "0xEC: the fov blends on the smoothstep 3t^2 - 2t^3");
        for (int k = 0; k < 3; ++k) o = rig.step(player, look, rng);
        check(rig.blend_t() == 1.0f && rig.from() == 0 && near(o.fov, 40 * 0.017453292f), "0xEC: settled after `frames` steps");
        rig.change(-1, -1, -4.0f, -1, 0);
        check(near(rig.slot(0).eye.pitch, -4 * 0.017453292f), "0xEC: a pitch of -4 is written (only -1.1 < p < 0 keeps)");
    }
    {   // Camera_Set / Camera_Return
        Scm scm;
        scm.regions.push_back(region(0, 0.5f, 20, 0.5f, 0.015f));
        scm.regions.push_back(region(0, 0.5f, 20, 0.9f, 0.015f, {100, 0, 100}, {-1, -1, -1}, {1, 1, 1}));   // never contains the player
        CamRig rig; rig.reset(scm);
        rig.step(player, look, rng);
        rig.set(1, 2);
        rig.step(player, look, rng);
        check(rig.current() == 1 && near(rig.blend_t(), 0.5f), "0xC5: the forced slot, blended at 1/frames");
        rig.step(player, look, rng);
        check(rig.blend_t() == 1.0f, "0xC5: settled");
        rig.ret(4);
        rig.step(player, look, rng);
        check(rig.current() == 0 && rig.forced() == -2 && near(rig.blend_t(), 0.25f),
              "0xC6: back to the picked region at the op's rate, still returning");
        for (int k = 0; k < 4; ++k) rig.step(player, look, rng);
        check(rig.forced() == -1, "0xC6: the return ends once t == 1");
    }
    {   // Scroll: the base is the published target; prio 0 eases; frames <= 1 hard-cuts
        Scm scm;
        scm.regions.push_back(region(0, 0.5f, 20, 0.5f, 0.015f));
        CamRig rig; rig.reset(scm);
        CamRig::Out o0 = rig.step(player, look, rng);
        rig.scroll_moveto({10, 1, 0}, 4, 0);
        CamRig::Out o = rig.step(player, look, rng);                      // t 0.25 -> 0.15625
        check(near(o.target.x, o0.target.x + (10 - o0.target.x) * 0.15625f, 1e-2f), "0xC3: smoothstep at priority 0");
        for (int k = 0; k < 3; ++k) o = rig.step(player, look, rng);
        check(near(o.target.x, 10, 1e-2f), "0xC3: arrives at the scripted point");
        rig.scroll_return(0, 0);
        o = rig.step(player, look, rng);
        check(near(o.target.x, 0, 1e-2f), "0xC4 frames 0: back on the player at once");
    }
    {   // the camera-space offset translates eye and target together
        Scm scm;
        scm.regions.push_back(region(0, 0.0f, 20, 0.5f, 0.015f));
        scm.regions[0].offset = {0, 5, 0};
        CamRig rig; rig.reset(scm);
        const CamRig::Out o = rig.step(player, look, rng);
        check(near(o.target.y, 6.0f, 1e-2f) && near(o.eye.y - o.target.y, scm_eye(scm.regions[0].eye, look, look).y - 1.0f, 1e-2f),
              "+0x3AC offset (0, 5, 0) lifts eye and target by 5 (a pan, not a rotation)");
    }
    {   // the boss hook (BOSS_CAMERA.md §1): the current slot is rewritten fixed and claimed
        Scm scm;
        scm.regions.push_back(region(0, 0.5f, 20, 0.5f, 0.015f));
        for (int k = 1; k <= 6; ++k)
            scm.regions.push_back(region(0, 0.5f, 30, 0.5236f, 0.015f, {10000, 10000, 10000}, {-1, -1, -1}, {1, 1, 1}));
        CamRig rig; rig.reset(scm);
        rig.step(player, look, rng);
        rig.write_forced(-1);
        rig.write_sel(1, 0, 0.0f, 0.02f);                                // the fight start
        CamRig::BossPresets bp;
        bp.eye[1] = {0, 17.9f, 0.227f}; bp.target[1] = {0, 2, 30.967f};
        bp.eye[2] = {0, 10, 3};          bp.target[2] = {0, 2, 33};
        rig.set_boss(bp);
        rig.step(player, look, rng);
        check(rig.current() == 1 && rig.from() == 0 && near(rig.blend_t(), 0.02f),
              "boss: preset 1 claimed (no volume pick), blending from slot 0 at 0.02");
        check(rig.slot(1).eye.mode == 0 && near(rig.slot(1).eye.fixed_pos.y, 17.9f) && near(rig.slot(1).fov, 0.61086524f) &&
              near(rig.slot(1).eye.dist, 27.0f) && near(rig.slot(1).eye.pitch, 0.24434610f),
              "boss: slot 1 = fixed eye/target, dist 27, pitch 14 deg, fov 35 deg");
        for (int k = 0; k < 60; ++k) rig.step(player, look, rng);
        CamRig::Out o = rig.step(player, look, rng);
        check(near(o.eye.y, 17.9f) && near(o.eye.z, 0.227f) && near(o.fov, 0.61086524f), "boss: settled on preset 1");
        rig.write_sel(2, 1, 0.0f, 0.03f);
        rig.step(player, look, rng);
        check(near(rig.slot(2).fov, 0.78539819f), "boss: preset 2 takes the 45 deg fov");
        rig.clear_boss();
        for (int k = 0; k < 40; ++k) rig.step(player, look, rng);
        check(rig.current() == 0, "boss off: the volume pick returns to region 0");
        // boss06 (ZAVA_CG.md §G.2): the host's hook edits the slot and claims one step at a time
        rig.write_sel(2, 0, 0.0f, 0.01f);
        rig.edit_slot(2).eye.dist = 35.0f;
        rig.claim();
        rig.step(player, look, rng);
        check(rig.current() == 2 && rig.from() == 0 && near(rig.blend_t(), 0.01f) && near(rig.slot(2).eye.dist, 35.0f),
              "claim(): the step keeps the written slot 2 (no volume pick), blending at the written 0.01");
        rig.step(player, look, rng);
        check(rig.current() == 0, "claim() lasts one step: the pick returns to region 0");
    }
    {   // B.flags != 0: the yaw from (L - A), and the orbit PIVOTS on L (S_3099's slot 5).
        Scm scm;
        CameraRegion r;
        r.target.mode = 0; r.target.fixed_pos = {0, 0, 0};
        r.eye.mode = 3; r.eye.flags = 3; r.eye.pitch = 0.0f; r.eye.dist = 18.0f;
        r.fov = 0.5f; r.blend = 0.015f;
        scm.regions.push_back(r);
        CamRig rig; rig.reset(scm);
        const Vec3 pl{0, 0, -17.5f}, lk{0, 1, -17.5f};
        const CamRig::Out o = rig.step(pl, lk, rng);
        check(near(o.eye.x, 0.0f, 1e-3f) && near(o.eye.z, -35.5f, 1e-3f) && near(o.eye.y, 1.0f, 1e-3f),
              "flags 3: the eye sits 18 beyond the player, outward from A");
        check(near(o.target.z, -17.5f, 1e-3f), "flags >= 3: the target is the raw look point");
    }
    std::printf("[cam] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
