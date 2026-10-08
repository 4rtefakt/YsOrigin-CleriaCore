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
#include "cleria/ymo.hpp"

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
    {   // Mesh_SetRotation 0x174 / Mesh_ResetRotation 0x179 (FUN_00572f10 / FUN_005730b0).
        MeshWorld w;
        MeshObj& b = w.add("boss04", {0, 0, 0}, {});
        b.rot = b.spawn_rot = {0.0f, 1.5f, 0.0f};
        w.set_rotation("BOSS04", {0.0f, 0.0f, 0.0f});
        const bool set = b.rot.y == 0.0f && b.dirty_pose;
        b.dirty_pose = false;
        w.reset_rotation("boss04");
        check(set && b.rot.y == 1.5f && b.dirty_pose, "Mesh_SetRotation re-poses; Mesh_ResetRotation restores +0x3dc");
    }
    {   // Mesh_Rotate 0x172 (FUN_00572e70; DALLES.md §2.3): the per-step delta in radians; frames <= 0 = forever.
        MeshWorld w;
        MeshObj& a = w.add("s70part1", {0, 0, 0}, {});
        MeshObj& b = w.add("s70part2", {0, 0, 0}, {});
        w.rotate("S70PART1", {0.0f, 0.05f, 0.0f}, 0);              // OBJ_ROTATE's call: forever
        w.rotate("s70part2", {0.0f, -0.05f, 0.0f}, 3);             // three steps
        w.rotate("nothing", {1.0f, 1.0f, 1.0f}, 0);                // a missing object: nothing
        const bool armed = a.rot_steps == -1 && b.rot_steps == 3 && a.rot.y == 0.0f;
        for (int k = 0; k < 10; ++k) w.step();
        const float d = 0.05f * 0.017453292f;
        check(armed && std::fabs(a.rot.y - 10.0f * d) < 1e-6f && a.rot_steps == -1 && a.dirty_pose,
              "Mesh_Rotate(.., 0): +0.05 deg a step, forever");
        check(std::fabs(b.rot.y + 3.0f * d) < 1e-7f && b.rot_steps == 0, "Mesh_Rotate(.., 3): exactly three steps");
    }
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
        // FUN_00501a30's battle block (#53): the torches take only class 1 (fire), the walls only class 2
        // (thunder); both drop the min-1 chip; HP 1, DEF 0. An HP of 0 means 9999 / DEF 999.
        auto block = [&](const char* rel, const char* name) {
            SceneTrigger t;
            for (const SobObject& o : Sob::parse(read_bytes_for_test(assets / rel)).objects)
                if (o.name == name) sob_battle_block(o, t);
            return t;
        };
        const SceneTrigger torch = block("MAP/S_30/S_3004/S_3004.SOB", "fire_01");
        const SceneTrigger wall = block("MAP/S_20/S_2015/S_2015.SOB", "kabe_00");
        std::printf("[mesh]   fire_01 HP %.0f DEF %.0f resist %.0f %.0f %.0f %.0f nofloor %d; kabe_00 resist %.0f %.0f %.0f %.0f\n",
                    torch.hp, torch.def, torch.resist[0], torch.resist[1], torch.resist[2], torch.resist[3],
                    int(torch.no_floor), wall.resist[0], wall.resist[1], wall.resist[2], wall.resist[3]);
        check(torch.hp == 1.0f && torch.def == 0.0f && torch.resist[0] == 1.0f && torch.resist[1] == 0.0f &&
              torch.resist[2] == 1.0f && torch.resist[3] == 1.0f && torch.no_floor,
              "S_3004 fire_01: HP 1, DEF 0, only class 1 (fire) unresisted, no min-1 chip");
        check(wall.resist[0] == 1.0f && wall.resist[1] == 1.0f && wall.resist[2] == 0.0f && wall.resist[3] == 1.0f &&
              wall.no_floor, "S_2015 kabe_00: only class 2 (thunder) unresisted, no min-1 chip");
        SobObject none;
        SceneTrigger inv;
        sob_battle_block(none, inv);
        check(inv.hp == 9999.0f && inv.def == 999.0f, "an SOB HP of 0: 9999 / DEF 999 (FUN_00501a30 0x5024ec)");
    } else {
        std::printf("[mesh]   (no assets: S_1004 checks skipped)\n");
    }
    {   // PoseTracks: the override-track pose evaluation (build/boss_re/DARM_TRACKS.md §2..§5, §7).
        Model m;
        auto node = [&](const char* nm, const char* parent, Vec3 t) {
            Node n; n.name = nm; n.parent = parent; n.scale = {1, 1, 1}; n.translation = t;
            m.nodes.push_back(n);
        };
        node("root", "", {0, 0, 0}); node("a", "root", {1, 0, 0}); node("b", "a", {0, 1, 0}); node("c", "root", {0, 0, 5});
        const float s45 = 0.70710678f;
        m.nodes[3].quat[1] = s45; m.nodes[3].quat[3] = s45;          // c rests turned 90 deg about Y
        auto clip = [&](const char* nm, float base, bool turn) {
            AnimClip c; c.name = nm;
            for (const char* n : {"a", "b", "c"}) {
                AnimTrack t; t.node = n;
                t.translation = {{{base, 0, 0}, 0}, {{base + 1, 0, 0}, 10}, {{base + 2, 0, 0}, 20}};
                t.scale = {{{1, 1, 1}, 0}};
                if (n[0] != 'c') {                                   // c: no rotation keys
                    KeyQuat q; q.frame = 0;
                    if (turn) { q.q[1] = s45; q.q[3] = s45; }
                    t.rotation = {q};
                }
                c.tracks.push_back(t);
            }
            return c;
        };
        m.clips.push_back(clip("main", 0.0f, false));
        m.clips.push_back(clip("over", 100.0f, true));
        m.resolve_hierarchy();
        m.rest_pose();
        const AnimClip &mainc = m.clips[0], &over = m.clips[1];
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
        auto lx = [&](int i) { return m.nodes[std::size_t(i)].local.m[12]; };
        PoseTracks pt;
        pt.reset(m);
        check(pt.recs.size() == 4 && pt.recs[1].owner == -1 && pt.recs[1].cur_t.x == 1.0f && pt.recs[1].snap_t.x == 1.0f &&
              pt.recs[3].snap_q[1] == s45, "tracks: reset: cur = snap = the rest S / R / T, every owner -1");
        pt.evaluate(m, mainc, 5.0f, 0.0f, {});
        check(near(lx(1), 0.5f) && near(m.nodes[2].world.m[12], 1.0f) && near(lx(0), 0.0f),
              "tracks: no track: the main clip's sample (a 0.5; b under it); a node without a track is not touched");
        check(near(m.nodes[3].local.m[0], 1.0f) && near(m.nodes[3].local.m[2], 0.0f),
              "tracks: no rotation keys: the identity, not the rest quaternion");
        pt.evaluate(m, mainc, 25.0f, 0.0f, {});
        const float past = lx(1);
        pt.evaluate(m, mainc, -5.0f, 0.0f, {});
        check(near(past, 1.0f) && near(lx(1), -0.5f),
              "tracks: key lookup: past the last key it runs back to key 0 over the last interval; u is not clamped");
        // bind: paint the sub-tree, then the snapshot (the main clip's last sample)
        pt.evaluate(m, mainc, 5.0f, 0.0f, {});
        pt.paint(m, 1, 0);
        pt.snapshot(0);
        check(pt.recs[1].owner == 0 && pt.recs[2].owner == 0 && pt.recs[0].owner == -1 && pt.recs[3].owner == -1 &&
              near(pt.recs[1].snap_t.x, 0.5f), "tracks: bind paints the node and its sub-tree; the snapshot is their last raw sample");
        PoseTracks::Track tk[2];
        tk[0].clip = &over; tk[0].frame = 10.0f; tk[0].weight = 1.0f;
        pt.evaluate(m, mainc, 5.0f, 0.0f, tk);
        const bool w1 = near(lx(1), 0.5f) && near(m.nodes[1].local.m[0], 1.0f) && near(pt.recs[1].cur_t.x, 101.0f);
        tk[0].weight = 0.5f;
        pt.evaluate(m, mainc, 5.0f, 0.0f, tk);
        const bool wh = near(lx(1), 50.75f) && near(m.nodes[1].local.m[0], s45) && near(lx(3), 0.5f) && near(pt.recs[1].snap_t.x, 0.5f);
        tk[0].weight = 0.0f;
        pt.evaluate(m, mainc, 5.0f, 0.0f, tk);
        check(w1, "tracks: weight 1 = the snapshot pose exactly; cur stays the raw override sample");
        check(wh, "tracks: weight 0.5: S / T half way, R by slerp (45 of 90 deg); a main-owned node is unaffected");
        check(near(lx(1), 101.0f) && near(m.nodes[1].local.m[0], 0.0f) && near(m.nodes[1].local.m[2], -1.0f) && near(lx(2), 101.0f),
              "tracks: weight 0 = the override clip's sample (the same track index), the whole sub-tree");
        // release: the snapshot of the override sample; the main clip's keys fade in
        pt.snapshot(0);
        tk[0].releasing = true; tk[0].weight = 1.0f;
        pt.evaluate(m, mainc, 5.0f, 0.0f, tk);
        const bool r1 = near(lx(1), 101.0f) && pt.recs[1].owner == 0;
        tk[0].weight = 0.5f;
        pt.evaluate(m, mainc, 5.0f, 0.0f, tk);
        const bool rh = near(lx(1), 50.75f);
        tk[0].weight = 0.0f;
        pt.evaluate(m, mainc, 5.0f, 0.0f, tk);
        check(r1 && rh && near(lx(1), 0.5f), "tracks: release: the main clip at the track's weight, from the override's last sample; owners unchanged");
        pt.paint(m, 1, -1);
        check(pt.recs[1].owner == -1 && pt.recs[2].owner == -1, "tracks: the hand-back paints the sub-tree -1");
        pt.paint(m, 1, 0); pt.paint(m, 2, 1);
        check(pt.recs[1].owner == 0 && pt.recs[2].owner == 1, "tracks: the last bind wins: a nested bind takes the inner sub-tree");
        PoseTracks pm;
        pm.reset(m);
        pm.snapshot(-1);
        pm.evaluate(m, mainc, 10.0f, 0.25f, {});
        check(near(lx(1), 1.0f + (1.0f - 1.0f) * 0.25f) && near(lx(2), 1.0f + (0.0f - 1.0f) * 0.25f),
              "tracks: the main weight blends main-owned nodes toward their snapshot (the rest pose here)");
    }
    if (argc > 1) {   // Darm's tail: every clip lists the same 68 node tracks; a bound tentacle plays 20_Beat
        const std::filesystem::path assets = argv[1];
        Model m = Model::parse(read_bytes_for_test(assets / "ENEMY" / "BOSS08" / "BOSS08T.YMO"));
        m.resolve_hierarchy();
        bool same = m.clips.size() == 27;
        for (const AnimClip& c : m.clips) {
            same = same && c.tracks.size() == m.clips[0].tracks.size();
            for (std::size_t i = 0; same && i < c.tracks.size(); ++i)
                same = c.tracks[i].node == m.clips[0].tracks[i].node && !c.tracks[i].scale.empty() && !c.tracks[i].rotation.empty() &&
                       !c.tracks[i].translation.empty();
        }
        check(same && !m.clips.empty() && m.clips[0].tracks.size() == 68,
              "BOSS08T: 27 clips, 68 tracks each in one node order, all with S / R / T keys (DARM_TRACKS §2.6)");
        const AnimClip *wait = nullptr, *beat = nullptr;
        for (const AnimClip& c : m.clips) { if (c.name == "19_OnBackWait") wait = &c; if (c.name == "20_Beat") beat = &c; }
        int root = -1;
        for (std::size_t i = 0; i < m.nodes.size(); ++i) if (m.nodes[i].name == "Bone_LeftTentacle1-1") root = int(i);
        if (same && wait && beat && root >= 0) {
            auto maxd = [](const Mat4& a, const Mat4& b) { float d = 0; for (int k = 0; k < 16; ++k) d = std::max(d, std::fabs(a.m[std::size_t(k)] - b.m[std::size_t(k)])); return d; };
            m.rest_pose(); m.pose(*wait, 1400.0f);
            std::vector<Mat4> lw, ww;
            for (const Node& n : m.nodes) { lw.push_back(n.local); ww.push_back(n.world); }
            m.rest_pose(); m.pose(*beat, 1520.0f);
            std::vector<Mat4> lb;
            for (const Node& n : m.nodes) lb.push_back(n.local);
            m.rest_pose();
            PoseTracks pt;
            pt.reset(m);
            pt.evaluate(m, *wait, 1400.0f, 0.0f, {});
            float d0 = 0;
            for (std::size_t i = 0; i < m.nodes.size(); ++i) d0 = std::max(d0, maxd(m.nodes[i].world, ww[i]));
            check(d0 < 1e-3f, "BOSS08T: without a track the evaluation is Model::pose at a key frame");
            pt.paint(m, root, 4);                            // k 0 = Bone_LeftTentacle1-1 -> track 4 (DARM_CE §0)
            pt.snapshot(4);
            int owned = 0;
            for (const PoseTracks::Rec& r : pt.recs) owned += r.owner == 4;
            PoseTracks::Track tk[8];
            tk[4].clip = beat; tk[4].frame = 1520.0f; tk[4].weight = 1.0f;
            pt.evaluate(m, *wait, 1400.0f, 0.0f, tk);
            float d1 = 0;
            for (std::size_t i = 0; i < m.nodes.size(); ++i) d1 = std::max(d1, maxd(m.nodes[i].world, ww[i]));
            tk[4].weight = 0.0f;
            pt.evaluate(m, *wait, 1400.0f, 0.0f, tk);
            float din = 0, dout = 0, moved = 0;
            for (std::size_t i = 0; i < m.nodes.size(); ++i) {
                if (pt.recs[i].owner == 4) { din = std::max(din, maxd(m.nodes[i].local, lb[i])); moved = std::max(moved, maxd(lb[i], lw[i])); }
                else dout = std::max(dout, maxd(m.nodes[i].local, lw[i]));
            }
            std::printf("[mesh]   tentacle sub-tree %d nodes; weight 1 vs the main pose %.2g; weight 0: in %.2g, out %.2g (the clips differ by %.2g)\n",
                        owned, double(d1), double(din), double(dout), double(moved));
            check(owned == 5, "BOSS08T: Bone_LeftTentacle1-1's sub-tree is its five bones");
            check(d1 < 1e-3f, "BOSS08T: a fresh bind (weight 1) still shows the main clip's pose");
            check(din < 1e-3f && dout < 1e-3f && moved > 0.01f, "BOSS08T: weight 0: the tentacle is 20_Beat's pose, the rest the main clip's");
        } else {
            check(false, "BOSS08T: 19_OnBackWait, 20_Beat and Bone_LeftTentacle1-1 exist");
        }
    }
    std::printf("[mesh] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
