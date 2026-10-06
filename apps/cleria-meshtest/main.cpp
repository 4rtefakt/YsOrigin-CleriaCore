// cleria-meshtest — the scripted map-object runtime (src/cleria/meshobj.hpp) against
// build/event_re/WAITOPS.md §4: Mesh_MoveTo's constant-speed step / stop rule, the clip play
// counter for stop and loop sections, the deferred clip switch, and the missing-object answer.
// Also ops 0x188 Mesh_DistanceToAlpha / 0x189 Mesh_SyncronizePos (EVENTVM_HANDLERS_3.md §3), and,
// with the assets, S_1004's step hulls following their render model's UP clip (COLLISION.md §1c).
// Usage: cleria-meshtest [assets]   (without assets the S_1004 checks are skipped)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "cleria/meshobj.hpp"
#include "cleria/scene.hpp"
#include "cleria/sob.hpp"

using namespace cleria;

static int fails = 0;
static std::vector<std::uint8_t> read_bytes_for_test(const std::filesystem::path& p) {
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    std::vector<std::uint8_t> b;
    if (!f) return b;
    std::uint8_t buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return b;
}
static void check(bool ok, const char* what) {
    std::printf("[mesh] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

int main(int argc, char** argv) {
    {   // Mesh_MoveTo(asiba, 0,0,10, r 0.16, speed 0.15): stops within the radius, not snapped.
        MeshWorld w;
        w.add("asiba_00", {0, 0, 0}, {});
        w.move_to("ASIBA_00", {0, 0, 10}, 0.16f, 0.15f, false);
        int steps = 0;
        bool found = false;
        while (w.moving("asiba_00", &found) && steps < 1000) { w.step(); ++steps; }
        const float z = w.find("asiba_00")->pos.z;
        std::printf("[mesh]   move: %d steps, z %.4f\n", steps, z);
        check(found && steps == 66 && z < 10.0f && 10.0f - z <= 0.16f,
              "MoveTo: constant 0.15/step, stops within 0.16 short (66 steps), no snap");
    }
    {   // radius < 0.01 snaps to the destination on the overshoot; MoveOffset is relative.
        MeshWorld w;
        w.add("step01", {2, 0, 0}, {});
        w.move_to("step01", {1, 0, 0}, 0.005f, 0.15f, true);
        bool found = false;
        int steps = 0;
        while (w.moving("step01", &found) && steps < 100) { w.step(); ++steps; }
        const Vec3 p = w.find("step01")->pos;
        std::printf("[mesh]   offset move: %d steps, x %.4f\n", steps, p.x);
        check(p.x == 3.0f && steps == 8, "MoveOffset +1 (dest = pos + offset), radius < 0.01 snaps on the overshoot");
    }
    {   // radius <= 0 takes the speed as the radius.
        MeshWorld w;
        w.add("a", {0, 0, 0}, {});
        w.move_to("a", {0, 1, 0}, 0.0f, -0.3f, false);
        check(w.find("a")->radius == 0.3f && w.find("a")->speed == 0.3f, "MoveTo: speed = |op5|, radius 0 -> speed");
    }
    {   // A stop section: TURN_ON 0..5 at rate 2.0. SetMotion zeroes the counter now; the clip
        // switches in the next update; the counter becomes 1 when the time reaches the end.
        MeshWorld w;
        w.add("switch1", {0, 0, 0}, {{"TURN_ON", 0, 5, -1, 2.0f}, {"TURN_OFF", 5, 15, -1, 2.0f}});
        w.find("switch1")->count = 7;
        w.set_motion("switch1", "turn_on", false, 0);
        bool found = false;
        const bool fin0 = w.motion_finished("switch1", 1, &found);
        w.step();                                          // applies TURN_ON at time 0
        const bool fin1 = w.motion_finished("switch1", 1, &found);
        w.step(); w.step();                                // time 2, 4
        const bool fin3 = w.motion_finished("switch1", 1, &found);
        w.step();                                          // time 6 -> clamped to 5, count 1
        const bool fin4 = w.motion_finished("switch1", 1, &found);
        check(found && !fin0 && !fin1 && !fin3 && fin4 && w.find("switch1")->time == 5.0f,
              "stop clip: counter 0 at SetMotion, 1 when the time reaches the end (clamped)");
    }
    {   // A loop section counts its wraps; start frame -1 starts at the end.
        MeshWorld w;
        w.add("fan", {0, 0, 0}, {{"WAIT", 0, 9, -2, 1.0f}, {"STOP", 0, 0, -1, 0.25f}});
        w.set_motion("fan", "WAIT", false, 0);
        for (int i = 0; i < 1 + 25; ++i) w.step();         // apply, then 25 advances: 25 > 10+11
        const std::uint32_t loops = w.find("fan")->count;
        w.set_motion("fan", "STOP", false, -1);
        w.step();
        bool found = false;
        std::printf("[mesh]   loop wraps after 25 steps: %u\n", loops);
        check(loops == 2 && w.motion_finished("fan", 1, &found),
              "loop clip: +1 per wrap (span = end - start + 1); start -1 = at the end (count 1)");
    }
    {   // StopMotion(name, 0) freezes the advance; a missing object reports found = false.
        MeshWorld w;
        w.add("door_01", {0, 0, 0}, {{"OPEN", 0, 90, -1, 0.2f}});
        w.set_motion("door_01", "OPEN", false, 0);
        w.step();
        w.stop_motion("door_01", 0);
        const float t = w.find("door_01")->time;
        w.step(); w.step();
        bool found = true;
        w.moving("nothing", &found);
        check(w.find("door_01")->time == t && !found, "StopMotion(0) freezes the clip; a missing object is not found");
    }
    {   // 0x188 (FUN_00573aa0): a = (b.fade + 1) * 0.5 * k * b.alpha * clamp(1 - |yb - ya| / div).
        // S_1004's STEPSHADOWUPDATE: (shadow, step, 10.0, 0.45); the UP clip lifts a step 7.
        MeshWorld w;
        w.add("step_00_shadow", {0, -2, 0}, {});
        w.add("step00", {0, -2, 0}, {});
        MeshObj& sh = *w.find("step_00_shadow");
        MeshObj& st = *w.find("step00");
        sh.frame_y = -2.0f; st.frame_y = -2.0f;
        w.distance_to_alpha("step_00_shadow", "step00", 10.0f, 0.45f);
        const float rest = sh.alpha;
        const bool dirty = sh.dirty_alpha;
        st.frame_y = 5.0f;
        w.distance_to_alpha("STEP_00_SHADOW", "step00", 10.0f, 0.45f);
        const float up = sh.alpha;
        st.frame_y = 9.0f;                                   // d / div > 1: clamped to 0 -> collapse
        sh.dirty_pose = false;
        w.distance_to_alpha("step_00_shadow", "step00", 10.0f, 0.45f);
        const float gone = sh.alpha;
        const bool collapse = sh.dirty_pose;
        st.frame_y = -1.5f;                                  // div 0: d is not divided (1 - 0.5)
        w.distance_to_alpha("step_00_shadow", "step00", 0.0f, 0.45f);
        const float nodiv = sh.alpha;
        st.alpha = 0.5f; st.fade = 0.0f; st.frame_y = -2.0f; // b's alpha and +0x468 scale it
        w.distance_to_alpha("step_00_shadow", "step00", 10.0f, 0.45f);
        const float scaled = sh.alpha;
        w.distance_to_alpha("step_00_shadow", "missing", 10.0f, 0.45f);   // nothing happens
        std::printf("[mesh]   0x188: rest %.4f up %.4f gone %.4f nodiv %.4f scaled %.4f\n", rest, up, gone, nodiv,
                    scaled);
        check(rest == 0.45f && dirty && std::fabs(up - 0.135f) < 1e-6f && gone == 0.0f && collapse &&
                  std::fabs(nodiv - 0.225f) < 1e-6f && std::fabs(scaled - 0.1125f) < 1e-6f && sh.alpha == scaled,
              "DistanceToAlpha: 0.45 at rest, 0.135 lifted 7, 0 past div, div 0, b alpha x (fade+1)/2");
    }
    {   // 0x189 (FUN_00573b80): per-axis copy of +0x50/+0x54/+0x58, a moved object is re-baked.
        MeshWorld w;
        w.add("step_02_shadow", {39, -2, -11.5f}, {});
        w.add("step02", {44.5f, -2, -19}, {});
        w.find("step02")->pos.y = 5.0f;
        w.sync_pos("step_02_shadow", "step02", true, false, true);
        const MeshObj& sh = *w.find("step_02_shadow");
        const bool moved = sh.dirty_pos;
        w.find("step_02_shadow")->dirty_pos = false;
        w.sync_pos("step_02_shadow", "step02", true, false, true);   // nothing new
        check(sh.pos.x == 44.5f && sh.pos.y == -2.0f && sh.pos.z == -19.0f && moved && !sh.dirty_pos,
              "SyncronizePos(1,0,1): x and z follow, y kept; only a change re-bakes");
    }
    // S_1004 (assets): a step's collision twin (S10STEP_00_.YMO, a DEFAULT clip only) follows the
    // render model's UP clip through the same-named node, and a load-time 0x173 placement.
    const std::filesystem::path assets = argc > 1 ? argv[1] : "";
    const std::filesystem::path sob = assets / "MAP" / "S_10" / "S_1004" / "S_1004.SOB";
    if (!assets.empty() && std::filesystem::exists(sob)) {
        auto top = [](const RoomMesh& m, const char* nm, Vec3* lo) {
            float y = -1e9f;
            *lo = {1e9f, 1e9f, 1e9f};
            if (const RoomMesh::ObjRange* r = m.object_range(nm))
                for (std::uint32_t k = r->v0; k < r->v1; ++k) {
                    y = std::max(y, m.vertices[k].py);
                    lo->x = std::min(lo->x, m.vertices[k].px);
                    lo->z = std::min(lo->z, m.vertices[k].pz);
                }
            return y;
        };
        ObjStates st;
        st.clip["step02"] = "UP";
        st.pos["step02"] = {44.5f, -2.0f, -19.0f};           // the root's 0x173 for Yunica / Hugo
        Vec3 lo0{}, lo1{};
        const float rest = top(build_collision(assets, sob), "step02", &lo0);
        const float up = top(build_collision(assets, sob, nullptr, nullptr, nullptr, &st), "step02", &lo1);
        std::printf("[mesh]   S_1004 step02 hull top: rest %.2f at x %.2f z %.2f; UP %.2f at x %.2f z %.2f\n", rest,
                    lo0.x, lo0.z, up, lo1.x, lo1.z);
        check(rest == -1.0f && up == 6.0f && lo1.x == 43.0f && lo1.z == -21.0f,
              "S_1004: a step's hull rises 7 with its render model's UP and sits at its 0x173 spot");
        // The frame 0x188 reads: the step's node of mesh 0, posed; the shadow's (no clips).
        SobObject step, shadow;
        for (const SobObject& o : Sob::parse(read_bytes_for_test(sob)).objects) {
            if (o.name == "step02") step = o;
            if (o.name == "step_02_shadow") shadow = o;
        }
        Vec3 fs{}, fu{}, fh{};
        const bool ok = object_frame(assets, step, "STOP", 0, &fs) && object_frame(assets, step, "UP", 60, &fu) &&
                        object_frame(assets, shadow, nullptr, 0, &fh);
        check(ok && fs.y == -2.0f && fu.y == 5.0f && fh.y == -2.0f && fu.x == step.position.x,
              "object_frame: step -2 at STOP, 5 at UP's end; the shadow -2");
    } else {
        std::printf("[mesh]   (no assets: S_1004 checks skipped)\n");
    }
    std::printf("[mesh] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
