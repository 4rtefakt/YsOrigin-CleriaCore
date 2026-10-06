// The region camera: build/event_re/CAMERA_OPS.md. See camrig.hpp.
#include "cleria/camrig.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cleria {

namespace {

float smooth(float t) {                       // FUN_0042ca10: 3t^2 - 2t^3, clamped
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
Vec3 lerp3(const Vec3& a, const Vec3& b, float e) {
    return {a.x + (b.x - a.x) * e, a.y + (b.y - a.y) * e, a.z + (b.z - a.z) * e};
}
float rate_of(int frames) { return frames ? 1.0f / float(frames) : 1.0f; }
// A random word mapped to [-1, 1): 2 * (1.mantissa - 1) - 1 (FUN_00427c90 0x427f44).
float unit(std::uint32_t w) {
    const std::uint32_t bits = (w & 0x7fffffu) | 0x3f800000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return 2.0f * (f - 1.0f) - 1.0f;
}

}  // namespace

void CamRig::reset(const Scm& scm) {
    for (Slot& s : slots_) s = Slot{};
    count_ = int(std::min<std::size_t>(scm.regions.size(), kSlots));
    for (int i = 0; i < count_; ++i) {
        slots_[std::size_t(i)].rec = scm.regions[std::size_t(i)];
        slots_[std::size_t(i)].populated = true;
    }
    forced_ = -1; rate_ = 0.1f; t_ = 1.0f; c0_ = true;
    cur_ = from_ = 0;
    sc_target_ = sc_base_ = {}; sc_dest_ = sc_t_ = sc_rate_ = 0; sc_prio_ = 0;
    sh_frames_ = 0;                            // FUN_00431260 @0x43154e
}

void CamRig::copy_set(int id) { copy_ = id; }

void CamRig::change(float dist, float yaw_deg, float pitch_deg, float fov_deg, int frames) {
    // FUN_0056edb0: clone the current slot into the copy slot, blend from it, edit the current.
    if (cur_ < 0 || cur_ >= kSlots || copy_ < 0 || copy_ >= kSlots) return;
    from_ = copy_;
    if (copy_ != cur_) slots_[std::size_t(copy_)] = slots_[std::size_t(cur_)];
    CameraRegion& r = slots_[std::size_t(cur_)].rec;
    constexpr float kD2R = 0.017453292f;      // 0x6bd0dc
    if (dist >= 0) r.eye.dist = dist;
    if (yaw_deg >= 0) r.eye.yaw = yaw_deg * kD2R;
    if (pitch_deg >= 0 || pitch_deg <= -1.1f) r.eye.pitch = pitch_deg * kD2R;   // 0x6be060
    if (fov_deg >= 0) r.fov = fov_deg * kD2R;
    t_ = 0.0f;
    rate_ = rate_of(frames);
    sc_prio_ = 0;
}

void CamRig::set(int id, int frames) { forced_rate_ = rate_of(frames); forced_ = id; }
void CamRig::ret(int frames) { forced_rate_ = rate_of(frames); forced_ = -2; }

void CamRig::scroll_moveto(const Vec3& target, int frames, int prio) {
    sc_rate_ = rate_of(frames);
    sc_target_ = target;
    sc_base_ = pub_.target;                    // the published (on-screen) target
    sc_prio_ = prio;
    sc_t_ = 0.0f;
    sc_dest_ = 1.0f;
    if (sc_rate_ == 1.0f) c0_ = true;          // frames <= 1: a hard cut
}

void CamRig::scroll_return(int frames, int prio) {
    sc_rate_ = rate_of(frames);
    sc_base_ = last_look_;
    sc_prio_ = prio;
    sc_dest_ = 0.0f;
    if (sc_rate_ == 1.0f) c0_ = true;
}

void CamRig::shake(const Vec3& amp, int frames) { sh_amp_ = amp; sh_frames_ = frames; }

void CamRig::write_fixed(int k, const Vec3& eye, const Vec3& target, float dist, float pitch, float fov) {
    if (k < 0 || k >= kSlots) return;
    CameraRegion& r = slots_[std::size_t(k)].rec;
    r.eye.fixed_pos = eye;   r.eye.mode = 0;       // slot+0x288 / +0x268
    r.target.fixed_pos = target; r.target.mode = 0;   // slot+0xb4 / +0x94
    r.eye.dist = dist;       // +0x274
    r.eye.pitch = pitch;     // +0x270
    r.fov = fov;             // +0x418
}

void CamRig::evaluate(Slot& s, const Vec3& look, const Vec3& scrolled) {
    const CameraRegion& r = s.rec;
    // A: mode 1 follows the (scrolled) look point; mode 0 / 3 = its fixed point [mode 3 not modelled].
    const Vec3 a = r.target.mode == 1 ? scrolled : r.target.fixed_pos;
    // B: the orbit around A (scm_eye = FUN_0042b6e0). With B.flags != 0 the yaw comes from
    // (scrolled L - A) and the PIVOT becomes the scrolled L [BYTES 0x42b84b..0x42b85d: the
    // pivot words are overwritten with L's], so the eye sits `dist` beyond the player.
    Vec3 eye = scm_eye(r.eye, a, scrolled);
    if (r.eye.mode == 3 && r.eye.flags != 0)
        eye = {eye.x - a.x + scrolled.x, eye.y - a.y + scrolled.y, eye.z - a.z + scrolled.z};
    // The target post-pass: B.flags >= 3 aims at the raw look point; the Y low-pass 0.35.
    Vec3 t = r.eye.flags >= 3 ? look : a;
    if (s.snap) { s.lp_y = t.y; s.snap = false; }
    else s.lp_y += (t.y - s.lp_y) * 0.35f;
    s.target = {t.x, s.lp_y, t.z};
    s.eye = eye;
}

CamRig::Out CamRig::step(const Vec3& player, const Vec3& look, const std::function<std::uint32_t()>& rng) {
    if (count_ <= 0) return pub_;
    last_look_ = look;
    // The shake (FUN_00427c90): three draws per step while the counter runs.
    if (sh_frames_ != 0) {
        const float r0 = unit(rng ? rng() : 0u), r1 = unit(rng ? rng() : 0u), r2 = unit(rng ? rng() : 0u);
        sh_off_ = {r0 * sh_amp_.x, r1 * sh_amp_.y, r2 * sh_amp_.z};
        --sh_frames_;
    }
    // 2. the scroll ramp
    if (sc_rate_ != 0.0f) {
        const float st = std::min(sc_rate_, 1.0f);
        if (sc_t_ < sc_dest_) sc_t_ = std::min(sc_dest_, sc_t_ + st);
        else if (sc_t_ > sc_dest_) sc_t_ = std::max(sc_dest_, sc_t_ - st);
        if (sc_t_ == sc_dest_) sc_rate_ = 0.0f;
    }
    // 2b. FUN_0042bb10's boss path: the current slot is refreshed from the presets and claimed.
    bool claimed = false;
    if (boss_on_) {
        constexpr float kDist = 27.0f, kPitch = 0.24434610f, kFov = 0.61086524f, kFov2 = 0.78539819f;
        if (cur_ >= 1 && cur_ <= 6) {
            write_fixed(cur_, boss_.eye[std::size_t(cur_)], boss_.target[std::size_t(cur_)], kDist, kPitch,
                        cur_ == 2 ? kFov2 : kFov);
            claimed = true;
        } else if (cur_ == 0xb) {
            write_fixed(cur_, boss_.snap_eye, boss_.snap_target, kDist, kPitch, kFov);
            claimed = true;
        }
    }
    // 3. select
    int sel = cur_;
    if (!claimed) {
        sel = slots_[std::size_t(cur_)].rec.sticky ? cur_ : 0;
        for (int i = 1; i < count_; ++i)
            if (slots_[std::size_t(i)].populated && slots_[std::size_t(i)].rec.contains(player)) { sel = i; break; }
    }
    if (forced_ >= 0 && forced_ < kSlots) sel = forced_;
    // 4. a region change
    if (c0_ || sel != cur_) {
        cur_ = sel;
        Slot& s = slots_[std::size_t(cur_)];
        if (c0_ || s.rec.blend >= 1.0f) {
            t_ = rate_ = 1.0f;
            c0_ = false;
            s.snap = true;
        } else {
            t_ = 0.0f;
            rate_ = forced_ >= 0 ? forced_rate_ : s.rec.blend;
            if (rate_ < 1.19e-7f) rate_ = 0.015f;
        }
    }
    // 5. a pending return
    if (forced_ == -2) {
        if (t_ == 1.0f) forced_ = -1;
        else rate_ = forced_rate_;
    }
    // 6. evaluate the current and the "from" slot
    Vec3 scrolled = look;
    if (sc_t_ != 0.0f) {
        const float s = sc_prio_ == 0 ? smooth(sc_t_) : sc_t_;
        scrolled = lerp3(sc_base_, sc_target_, s);
    }
    Slot& cs = slots_[std::size_t(cur_)];
    evaluate(cs, look, scrolled);
    Slot& fs = slots_[std::size_t(std::clamp(from_, 0, kSlots - 1))];
    if (&fs != &cs) evaluate(fs, look, scrolled);
    // 7 / 8. blend or settle
    Out o;
    Vec3 off;
    bool blended = false;
    if (t_ != 1.0f) {
        t_ += std::min(rate_, 1.0f);
        if (t_ < 1.0f) {
            const float e = smooth(t_);
            o.target = lerp3(fs.target, cs.target, e);
            o.eye = lerp3(fs.eye, cs.eye, e);
            off = lerp3(fs.rec.offset, cs.rec.offset, e);
            o.pitch = fs.rec.eye.pitch + (cs.rec.eye.pitch - fs.rec.eye.pitch) * e;
            o.fov = fs.rec.fov + (cs.rec.fov - fs.rec.fov) * e;
            blended = true;
        } else {
            t_ = 1.0f;
        }
    }
    if (!blended) {
        o.target = cs.target; o.eye = cs.eye; off = cs.rec.offset;
        o.pitch = cs.rec.eye.pitch; o.fov = cs.rec.fov;
        from_ = cur_;
    }
    pub_ = o;                                  // the published (pre-offset) target: the scroll base
    // 9. FUN_0042a080: the camera-space offset (+ the shake) moves eye and target together.
    if (sh_frames_ != 0) off = {off.x + sh_off_.x, off.y + sh_off_.y, off.z + sh_off_.z};
    Vec3 f = {o.target.x - o.eye.x, o.target.y - o.eye.y, o.target.z - o.eye.z};
    const float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    if (fl > 1e-6f) f = {f.x / fl, f.y / fl, f.z / fl}; else f = {};
    const Vec3 r = normalize(cross(Vec3{0, 1, 0}, f));
    const Vec3 u = cross(f, r);
    const Vec3 w = {r.x * off.x + u.x * off.y + f.x * off.z,
                    r.y * off.x + u.y * off.y + f.y * off.z,
                    r.z * off.x + u.z * off.y + f.z * off.z};
    o.eye = {o.eye.x + w.x, o.eye.y + w.y, o.eye.z + w.z};
    o.target = {o.target.x + w.x, o.target.y + w.y, o.target.z + w.z};
    return o;
}

}  // namespace cleria
