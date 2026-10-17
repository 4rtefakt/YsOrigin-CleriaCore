// cleria-view — free-fly + SCM follow camera, scroll/shake, fog update
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// authored FOV wins and this number never reaches the screen. It was hand-tuned against
// screenshots back when the SCM camera payload was undecoded; kept as the free-fly
// default only.
static float g_fov = 0.4712f;  // 27 degrees

// ---- free-fly camera --------------------------------------------------------
struct Camera {
    Vec3 pos{}; float yaw = 0, pitch = 0;
    Vec3 forward() const {
        return {std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                std::cos(pitch) * std::sin(yaw)};
    }
    Mat4 view_proj(float aspect, float near_, float far_) const {
        const Vec3 f = forward();
        const Mat4 v = look_at_lh(pos, {pos.x + f.x, pos.y + f.y, pos.z + f.z}, {0, 1, 0});
        return v * perspective_lh(g_fov, aspect, near_, far_);
    }
    // The same view mirrored in the water plane y = plane_y, as the reflection pass draws with it (record 1 of
    // FUN_005ec6f0 [BYTES 0x5ec6f0..0x5ec9f7]; REFLECTIONS.md 4.2):
    //   view' = D3DXMatrixReflect(plane) x view;  M = view' x proj;  C = P x transpose(inverse(M)) -- the plane in
    //   clip space -- with -P when C.w > 0, unchanged projection when C.w == 0;  C normalised (4 components);
    //   proj' = proj x K, K = identity with its third column = C.
    // So a vertex's clip z becomes its signed distance to the water (over |C|): what lies under the water falls
    // before the near plane. No user clip plane.
    Mat4 reflected_view_proj(float aspect, float near_, float far_, float plane_y) const {
        const Vec3 f = forward();
        Mat4 r;                                    // D3DXMatrixReflect of (0, 1, 0, -plane_y)
        r.m[5] = -1.0f; r.m[13] = 2.0f * plane_y;
        const Mat4 m = r * look_at_lh(pos, {pos.x + f.x, pos.y + f.y, pos.z + f.z}, {0, 1, 0}) *
                       perspective_lh(g_fov, aspect, near_, far_);
        double a[4][8];                            // inverse(M), Gauss-Jordan
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 8; ++j) a[i][j] = j < 4 ? double(m.m[i * 4 + j]) : (j - 4 == i ? 1.0 : 0.0);
        for (int c = 0; c < 4; ++c) {
            int p = c;
            for (int i = c + 1; i < 4; ++i) if (std::fabs(a[i][c]) > std::fabs(a[p][c])) p = i;
            if (std::fabs(a[p][c]) < 1e-30) return m;
            for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[p][j]);
            const double d = a[c][c];
            for (int j = 0; j < 8; ++j) a[c][j] /= d;
            for (int i = 0; i < 4; ++i) {
                if (i == c) continue;
                const double k = a[i][c];
                for (int j = 0; j < 8; ++j) a[i][j] -= k * a[c][j];
            }
        }
        double C[4];                               // C[j] = sum_i P[i] x inverse(M)[j][i], P = (0, 1, 0, -plane_y)
        for (int j = 0; j < 4; ++j) C[j] = a[j][4 + 1] - double(plane_y) * a[j][4 + 3];
        if (C[3] == 0.0) return m;
        const double s = (C[3] > 0.0 ? -1.0 : 1.0) / std::sqrt(C[0] * C[0] + C[1] * C[1] + C[2] * C[2] + C[3] * C[3]);
        Mat4 out = m;                              // M x K: only the third column changes
        for (int i = 0; i < 4; ++i)
            out.m[i * 4 + 2] = float(s * (double(m.m[i * 4]) * C[0] + double(m.m[i * 4 + 1]) * C[1] +
                                          double(m.m[i * 4 + 2]) * C[2] + double(m.m[i * 4 + 3]) * C[3]));
        return out;
    }
};

// Place the camera to overlook a freshly-loaded room (orbit-style vantage).
static void spawn_in(Camera& cam, const Room& r) {
    const Vec3 c = {(r.mesh.bmin.x + r.mesh.bmax.x) * 0.5f,
                    (r.mesh.bmin.y + r.mesh.bmax.y) * 0.5f,
                    (r.mesh.bmin.z + r.mesh.bmax.z) * 0.5f};
    const Vec3 d = r.mesh.bmax - r.mesh.bmin;
    const float diag = std::max(std::sqrt(dot(d, d)), 1.0f);
    const float y = -0.7f, p = 0.9f, rad = diag * 0.75f;
    cam.pos = {c.x + rad * std::cos(p) * std::cos(y), c.y + rad * std::sin(p),
               c.z + rad * std::cos(p) * std::sin(y)};
    const Vec3 fwd = normalize(c - cam.pos);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwd.y)));
    cam.yaw = std::atan2(fwd.z, fwd.x);
}

static float room_diag(const Room& r) {
    const Vec3 d = r.mesh.bmax - r.mesh.bmin;
    return std::max(std::sqrt(dot(d, d)), 1.0f);
}

// The live camera: the engine's region camera and its cutscene ops, src/cleria/camrig
// (build/event_re/CAMERA_OPS.md): 64 editable slots, the region pick (sticky-aware, forced), the
// smoothstep blend over the evaluated eye / target / offset / fov between the "from" and current
// slots, Camera_Set / Return, CopyCamera_Set / Camera_Change, the scroll (smoothstep at priority 0),
// the camera-space offset and the shake. One rig step per 60 Hz tick (update_camera); the frame
// reads the last output (apply_cam_state). [not modelled] the render interpolation between steps.
struct CamState {
    float fov = 0.5236f;
    bool on = false;
};
static CamState g_cam;
static CamRig g_rig;
static std::string g_rig_room;                      // the scene whose SCM the rig holds
static CamRig::Out g_cam_out;
static std::function<std::uint32_t()> g_cam_rng;   // the game's xorshift128 (the shake's draws)

// FUN_0042bb10's boss06 path (build/boss_re/ZAVA_CG.md §G.2), run before each rig step: while a fight is on and the
// GoMesh "boss06a" has its block, the hook FUN_005453d0 rewrites the current slot (2..6) from the fight and the step
// is claimed (the region pick is skipped; also with slot 0 current). The game installs it (view_boss06.inl); true =
// claimed.
static std::function<bool()> g_boss06_cam_hook;
// ... and its boss05 path [EXPORT 0042bb10.c:74-123]: while a fight is on and "boss05" has its block, slot 0 current
// claims the step and slot 3 current is rewritten from Pictimos's preset (view_boss05.inl).
static std::function<bool()> g_boss05_cam_hook;
// ... and its boss07 path (build/boss_re/DALLES_CD1.md §3.2): while flag 156 and the GoMesh "boss07" has its block,
// the hook FUN_0042c4f0 rewrites the current slot (1..5) and claims slots 0..4 (view_boss07.inl).
static std::function<bool()> g_boss07_cam_hook;
// ... and its boss08 path (build/boss_re/DARM_CE.md §4.7): while the GoMesh "boss08" has its block, the hook
// FUN_0055f800 rewrites the current slot (1..5) and claims every step (view_boss08.inl).
static std::function<bool()> g_boss08_cam_hook;

// The mods' camera (cleria.camera.set, API 7; CleriaCore's own): applied to the rig's result, so the game's own
// camera (regions, cutscenes, shakes) runs untouched under it. dist scales the eye's distance to its target, fov the
// field of view; off moves both.
struct ModCam { float dist = 1.0f, fov = 1.0f; Vec3 off{}; };
static ModCam g_mod_cam;

// The look point L: [H] feet + 1.0 stands in for player+0x230 + player+0x324 (CAMERA_OPS §5.1).
static constexpr float kLookHeight = 1.0f;

// One rig step. A new scene reloads the camera file (the Camera_Change edits last until then);
// `snap` = the engine's c0 (an instant region select: room entry, a skipped cutscene).
static void update_camera(const Room& room, const Vec3& feet, float /*dt*/, bool snap = false) {
    if (!room.cam.ok()) { g_cam.on = false; return; }
    if (g_rig_room != room.name || !g_rig.ok()) { g_rig.reset(room.cam); g_rig_room = room.name; }
    if (snap) g_rig.hard_cut();
    if (g_boss05_cam_hook && g_boss05_cam_hook()) g_rig.claim();
    if (g_boss06_cam_hook && g_boss06_cam_hook()) g_rig.claim();
    if (g_boss07_cam_hook && g_boss07_cam_hook()) g_rig.claim();
    if (g_boss08_cam_hook && g_boss08_cam_hook()) g_rig.claim();
    g_cam_out = g_rig.step(feet, {feet.x, feet.y + kLookHeight, feet.z}, g_cam_rng);
    if (g_mod_cam.dist != 1.0f || g_mod_cam.fov != 1.0f || g_mod_cam.off.x != 0 || g_mod_cam.off.y != 0 || g_mod_cam.off.z != 0) {
        const Vec3 d = g_cam_out.eye - g_cam_out.target;
        g_cam_out.target = {g_cam_out.target.x + g_mod_cam.off.x, g_cam_out.target.y + g_mod_cam.off.y, g_cam_out.target.z + g_mod_cam.off.z};
        g_cam_out.eye = {g_cam_out.target.x + d.x * g_mod_cam.dist, g_cam_out.target.y + d.y * g_mod_cam.dist, g_cam_out.target.z + d.z * g_mod_cam.dist};
        g_cam_out.fov *= g_mod_cam.fov;
    }
    g_cam.fov = g_cam_out.fov;
    g_cam.on = true;
}
static void camera_set(int id, int frames = 0) { g_rig.set(id, frames); }            // 0xC5
static void camera_return(int frames = 0) { g_rig.ret(frames); }                     // 0xC6
static void camera_copy_set(int id) { g_rig.copy_set(id); }                          // 0xEB
static void camera_change(float d, float y, float p, float f, int frames) { g_rig.change(d, y, p, f, frames); }  // 0xEC

// Optional overlay for the headless --play path: main can set this to draw UI (e.g. the
// real cutscene dialogue box via TitleFx::draw_dialogue) over the room after the HUD, so a
// preview goes through the actual render code rather than a re-implementation.
static std::function<void(Gpu&, WGPUTextureView, WGPUTextureView, std::uint32_t, std::uint32_t)> g_play_overlay;
// 0xC3 / 0xC4 (priority: 1026 of 1029 sites are 0; [not modelled] the host does not carry it).
static void scroll_moveto(float x, float y, float z, int frames) { g_rig.scroll_moveto({x, y, z}, frames, 0); }
static void scroll_return(int frames) { g_rig.scroll_return(frames, 0); }
// A skipped cutscene: the scroll back on the player at once.
static void scroll_reset() { g_rig.scroll_return(0, 0); }
// 0xCF: the shake's random offset is drawn in the rig step from the game's xorshift.
static void shake(float ax, float ay, float az, int frames) { g_rig.shake({ax, ay, az}, frames); }

static void apply_cam_state(Camera& cam, const Vec3& /*feet*/) {
    cam.pos = g_cam_out.eye;
    const Vec3 f = normalize(g_cam_out.target - g_cam_out.eye);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    cam.yaw = std::atan2(f.z, f.x);
    g_fov = g_cam_out.fov;
}

// Pick the fog region the player is standing in and ease `g_fog` toward it. The
// game cross-fades region changes at 0.02/frame (~0.83s at 60fps) instead of
// popping; `snap` jumps straight there (room entry / headless shots).
// 0x129 ChangeFog (FUN_0042e8f0, ZAVA.md §2.1): the script-selected record and its blend speed (fogT += speed a
// frame). It holds while the player stands in no fog zone (the updater FUN_0042e040's `cand`). The record count is
// checked in update_fog (an index past it is ignored there, as the op ignores it).
// [approx] the blend is this file's ease toward the record at that speed, not the engine's eased fogT lerp from
// fogPrev; a zone's own blend speed (+0x50) is not modelled.
static int g_fog_sel = -1;
static float g_fog_speed = 0.02f;
static void fog_script(int idx, float speed) {
    if (idx < 0) return;
    g_fog_sel = idx;
    if (speed != 3.4028235e38f) g_fog_speed = speed;               // FLT_MAX (0x6bdf90) keeps the speed
}
static void fog_script_reset() { g_fog_sel = -1; g_fog_speed = 0.02f; }
// fogCur as update_fog below picks it: the zone holding the player, else the script's record, else record 0 (-1 =
// the room has no fog). Zava's heads read and tint that record's colour (ZAVA_CG.md §G.3).
static int fog_current(const Room& room, const Vec3& player) {
    const FogRegion* f = room.fog.pick(player);
    if (!f) return -1;
    if (g_fog_sel > 0 && std::size_t(g_fog_sel) < room.fog.regions.size() && f == &room.fog.regions[0]) return g_fog_sel;
    return int(f - room.fog.regions.data());
}
static void update_fog(const Room& room, const Vec3& player, float dt, bool snap = false) {
    const FogRegion* f = room.fog.pick(player);
    if (!f) { g_fog.on = false; return; }
    if (g_fog_sel > 0 && std::size_t(g_fog_sel) < room.fog.regions.size() && f == &room.fog.regions[0])
        f = &room.fog.regions[std::size_t(g_fog_sel)];              // no zone holds the player: the script's record
    const float t = snap ? 1.0f : std::min(1.0f, dt * 60.0f * (g_fog_sel >= 0 ? g_fog_speed : 0.02f));
    auto ease = [&](float cur, float tgt) { return cur + (tgt - cur) * t; };
    if (!g_fog.on) {  // first sight of fog: start there rather than fading from black
        g_fog = {f->r, f->g, f->b, f->fog_near, f->fog_far, true};
        return;
    }
    g_fog.r = ease(g_fog.r, f->r);
    g_fog.g = ease(g_fog.g, f->g);
    g_fog.b = ease(g_fog.b, f->b);
    g_fog.near_ = ease(g_fog.near_, f->fog_near);
    g_fog.far_ = ease(g_fog.far_, f->fog_far);
}

