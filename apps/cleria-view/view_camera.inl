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

// The look point L: [H] feet + 1.0 stands in for player+0x230 + player+0x324 (CAMERA_OPS §5.1).
static constexpr float kLookHeight = 1.0f;

// One rig step. A new scene reloads the camera file (the Camera_Change edits last until then);
// `snap` = the engine's c0 (an instant region select: room entry, a skipped cutscene).
static void update_camera(const Room& room, const Vec3& feet, float /*dt*/, bool snap = false) {
    if (!room.cam.ok()) { g_cam.on = false; return; }
    if (g_rig_room != room.name || !g_rig.ok()) { g_rig.reset(room.cam); g_rig_room = room.name; }
    if (snap) g_rig.hard_cut();
    g_cam_out = g_rig.step(feet, {feet.x, feet.y + kLookHeight, feet.z}, g_cam_rng);
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
static void update_fog(const Room& room, const Vec3& player, float dt, bool snap = false) {
    const FogRegion* f = room.fog.pick(player);
    if (!f) { g_fog.on = false; return; }
    const float t = snap ? 1.0f : std::min(1.0f, dt * 60.0f * 0.02f);
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

