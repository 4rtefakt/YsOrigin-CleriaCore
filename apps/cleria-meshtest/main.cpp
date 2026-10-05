// cleria-meshtest — the scripted map-object runtime (src/cleria/meshobj.hpp) against
// build/event_re/WAITOPS.md §4: Mesh_MoveTo's constant-speed step / stop rule, the clip play
// counter for stop and loop sections, the deferred clip switch, and the missing-object answer.
// Usage: cleria-meshtest [assets]   (the assets argument is ignored)
#include <cmath>
#include <cstdio>

#include "cleria/meshobj.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[mesh] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

int main() {
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
    std::printf("[mesh] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
