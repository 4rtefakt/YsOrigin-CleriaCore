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

// The live camera, driven by the room's SCM regions. build/camera_re/CAMERA.md
// CONFIRMED this model against the disassembly (writer FUN_0042a3d0, orbit FUN_0042b6e0)
// and re-verified our scm.cpp field-for-field: pick() tests regions 1..N-1 as rotatable
// AABBs, first-containing wins, region 0 is the never-tested fallback; point A = follow
// target (mode 1), point B = orbit eye (mode 3 at yaw/pitch/dist); FOV modal 30deg,
// blend modal 0.015/frame -- both verified in the SCM corpus. The follow is INSTANT
// (eye re-derived from the player every frame), which is right: it is a static authored
// shot re-aimed at the player, an orbit rig pivoted on the player -- no dead-zone, no
// trailing lerp. apply_cam_state now calls the shared scm_eye (auto-yaw included).
//
// ** One sourced refinement DEFERRED (needs interactive verification, and --play can't
// run headless): ** the engine's region CHANGE lerps the EYE/TARGET/FOV positions on a
// bounded cubic-eased t:0->1 over ~0.015/frame, re-computing BOTH regions' eyes from the
// live player each frame (so neither endpoint lags). We instead lerp the orbit PARAMS
// exponentially -- a close approximation that avoids lag, but not the exact transition
// curve. Also unadopted: region STICKINESS (+0x3A8 bit0) and a target-Y low-pass
// (0.35/frame). All sourced in CAMERA.md; left until the camera can be watched in motion.
struct CamState {
    float fov = 0.5236f, yaw = 0, pitch = 0, dist = 24;
    // Captured from the current region so apply_cam_state can call the shared scm_eye
    // (which does the auto-yaw for flagged regions) instead of re-deriving the orbit.
    std::uint32_t eye_mode = 3, eye_flags = 0;
    Vec3 eye_fixed{};
    bool on = false;
};
static CamState g_cam;

// The game aims at ground + a per-actor offset (player+0x230 + player+0x324),
// which we don't model yet; a fixed chest-height offset stands in for it.
static constexpr float kLookHeight = 1.0f;

static void update_camera(const Room& room, const Vec3& feet, float dt, bool snap = false) {
    const CameraRegion* r = room.cam.pick(feet);
    if (!r) { g_cam.on = false; return; }
    g_cam.eye_mode = r->eye.mode;      // mode/flags/fixed track the current region
    g_cam.eye_flags = r->eye.flags;
    g_cam.eye_fixed = r->eye.fixed_pos;
    if (!g_cam.on || snap) {
        g_cam.fov = r->fov; g_cam.yaw = r->eye.yaw;
        g_cam.pitch = r->eye.pitch; g_cam.dist = r->eye.dist;
        g_cam.on = true;
        return;
    }
    // Ease the region's parameters only; the follow stays instant.
    const float t = std::min(1.0f, dt * 60.0f * r->blend);
    auto L = [&](float c, float n) { return c + (n - c) * t; };
    g_cam.fov = L(g_cam.fov, r->fov);
    g_cam.yaw = L(g_cam.yaw, r->eye.yaw);
    g_cam.pitch = L(g_cam.pitch, r->eye.pitch);
    g_cam.dist = L(g_cam.dist, r->eye.dist);
}

// Place `cam` from the blended parameters and the player's CURRENT position, using
// the engine's orbit math (FUN_0042b6e0): a = -PI/2 - yaw, then
// eye = target + (-cos(a)*d*cos(pitch), sin(pitch)*d, sin(a)*d*cos(pitch)).
// Event-camera SCROLL override (CUTSCENE_CAMERA.md §9): op 0xc3 Scroll_MoveTo slides the
// camera's LOOK-AT point (not the eye) to a scripted world target over `frames`, linearly;
// 0xc4 Scroll_Return slides it back to the player. The orbit rig (scm_eye) then frames the
// scripted point automatically. State + the last follow point (the scroll_base source) live
// here so apply_cam_state can lerp; scroll_advance() steps `t` once per 60 Hz frame.
struct ScrollCam { Vec3 target{}, base{}; float t = 0, t_dest = 0, rate = 1; bool active = false; };
static ScrollCam g_scroll;
static Vec3 g_last_look{};

// Optional overlay for the headless --play path: main can set this to draw UI (e.g. the
// real cutscene dialogue box via TitleFx::draw_dialogue) over the room after the HUD, so a
// preview goes through the actual render code rather than a re-implementation.
static std::function<void(Gpu&, WGPUTextureView, WGPUTextureView, std::uint32_t, std::uint32_t)> g_play_overlay;
static void scroll_moveto(float x, float y, float z, int frames) {
    g_scroll.target = {x, y, z}; g_scroll.base = g_last_look;
    g_scroll.t = 0; g_scroll.t_dest = 1; g_scroll.rate = frames > 0 ? 1.0f / frames : 1.0f;
    g_scroll.active = true;
    if (g_scroll.rate >= 1.0f) g_scroll.t = 1.0f;   // instant
}
static void scroll_return(int frames) {
    g_scroll.base = g_last_look; g_scroll.t_dest = 0;
    g_scroll.rate = frames > 0 ? 1.0f / frames : 1.0f;
    if (g_scroll.rate >= 1.0f) { g_scroll.t = 0; g_scroll.active = false; }
}
static void scroll_advance() {
    if (!g_scroll.active) return;
    const float d = g_scroll.t_dest - g_scroll.t, step = std::min(1.0f, g_scroll.rate);
    if (std::abs(d) <= step) { g_scroll.t = g_scroll.t_dest; if (g_scroll.t == 0.0f) g_scroll.active = false; }
    else g_scroll.t += (d > 0 ? step : -step);
}

// Event-camera SHAKE (CUTSCENE_CAMERA.md §6): op 0xcf jitters the camera for `frames` frames
// by a fresh random per-axis offset in [-amp,+amp], decaying to nothing when the counter hits
// 0. shake_advance() regenerates the offset + counts down once per 60 Hz frame; apply_cam_state
// folds the offset into the look-at. The exact geometric axis (basis vs eye) is [H]; the
// random-per-frame decay is the certain, code-grounded part. (PRNG is a local xorshift — the
// game's exact seed isn't matched, noted rather than hidden.)
struct ShakeCam { Vec3 amp{}, off{}; int frames = 0; std::uint32_t rs = 0x9e3779b9u; };
static ShakeCam g_shake;
static void shake(float ax, float ay, float az, int frames) {
    g_shake.amp = {ax, ay, az}; g_shake.frames = frames; g_shake.off = {0, 0, 0};
}
static void shake_advance() {
    if (g_shake.frames <= 0) { g_shake.off = {0, 0, 0}; return; }
    auto nextf = [&] {                       // xorshift32 -> [-1, 1)
        std::uint32_t x = g_shake.rs;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_shake.rs = x;
        return (float(x) / 4294967296.0f) * 2.0f - 1.0f;
    };
    g_shake.off = {nextf() * g_shake.amp.x, nextf() * g_shake.amp.y, nextf() * g_shake.amp.z};
    --g_shake.frames;
}

static void apply_cam_state(Camera& cam, const Vec3& feet) {
    Vec3 target = {feet.x, feet.y + kLookHeight, feet.z};   // A.mode 1: follow
    g_last_look = target;                                    // remember the follow point (scroll base)
    if (g_scroll.active && g_scroll.t != 0.0f)              // event scroll: lerp toward the scripted point
        target = {g_scroll.base.x + (g_scroll.target.x - g_scroll.base.x) * g_scroll.t,
                  g_scroll.base.y + (g_scroll.target.y - g_scroll.base.y) * g_scroll.t,
                  g_scroll.base.z + (g_scroll.target.z - g_scroll.base.z) * g_scroll.t};
    if (g_shake.frames > 0) { target.x += g_shake.off.x; target.y += g_shake.off.y; target.z += g_shake.off.z; }
    // Use the shared orbit (scm_eye), built from the BLENDED yaw/pitch/dist plus the
    // current region's mode/flags. This is the engine's FUN_0042b6e0 -- including the
    // auto-yaw for flagged regions, which the old hand-rolled copy here dropped.
    CameraPoint b;
    b.mode = g_cam.eye_mode;
    b.yaw = g_cam.yaw;
    b.pitch = g_cam.pitch;
    b.dist = g_cam.dist;
    b.flags = g_cam.eye_flags;
    b.fixed_pos = g_cam.eye_fixed;
    cam.pos = scm_eye(b, target, feet);
    const Vec3 f = normalize(target - cam.pos);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    cam.yaw = std::atan2(f.z, f.x);
    g_fov = g_cam.fov;
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

