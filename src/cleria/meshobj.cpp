// Scripted map objects at runtime: build/event_re/WAITOPS.md §4. See meshobj.hpp.
#include "cleria/meshobj.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace cleria {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
float len3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
}  // namespace

MeshObj& MeshWorld::add(const std::string& name, const Vec3& spawn, std::vector<MeshClip> clips) {
    MeshObj& m = objs_[lower(name)];
    m = MeshObj{};
    m.name = lower(name);
    m.spawn = m.pos = spawn;
    m.clips = std::move(clips);
    return m;
}

MeshObj* MeshWorld::find(const std::string& name) {
    auto it = objs_.find(lower(name));
    return it == objs_.end() ? nullptr : &it->second;
}

const MeshObj* MeshWorld::find(const std::string& name) const {
    auto it = objs_.find(lower(name));
    return it == objs_.end() ? nullptr : &it->second;
}

// FUN_00572d50 / FUN_00572dc0: speed = |op5|; radius = op4 > 0 ? op4 : speed; MoveOffset adds
// the current world position to the destination.
void MeshWorld::move_to(const std::string& name, const Vec3& to, float radius, float speed, bool offset) {
    MeshObj* m = find(name);
    if (!m) return;                                                // silently ignored
    m->speed = std::fabs(speed);
    m->radius = radius > 0.0f ? radius : m->speed;
    m->dest = offset ? Vec3{to.x + m->pos.x, to.y + m->pos.y, to.z + m->pos.z} : to;
}

// FUN_00572f70: the clip request is deferred to the mesh update; the play counter resets now.
void MeshWorld::set_motion(const std::string& name, const std::string& clip, bool blend, std::int32_t start) {
    MeshObj* m = find(name);
    if (!m) return;
    m->pending = clip;
    m->pending_blend = blend;
    m->pending_start = start;
    if (!m->clips.empty()) m->count = 0;
}

void MeshWorld::stop_motion(const std::string& name, std::int32_t on) {
    if (MeshObj* m = find(name)) m->frozen = (on == 0);
}
void MeshWorld::sleep(const std::string& name, bool on) {
    if (MeshObj* m = find(name)) m->sleeping = on;
}
// FUN_00572ee0 writes the position directly; an active move continues from there.
void MeshWorld::set_position(const std::string& name, const Vec3& p) {
    if (MeshObj* m = find(name)) { m->pos = p; m->dirty_pos = true; }
}
void MeshWorld::reset_position(const std::string& name) {
    if (MeshObj* m = find(name)) { m->pos = m->spawn; m->dirty_pos = true; }
}
void MeshWorld::set_rotation(const std::string& name, const Vec3& rad) {
    if (MeshObj* m = find(name)) { m->rot = rad; m->dirty_pose = true; }
}
void MeshWorld::set_material_color(const std::string& name, const std::string& material, const Vec3& rgb) {
    MeshObj* m = find(name);
    if (!m || m->clips.empty()) return;                            // no object / no model (m+0x268)
    std::string k = material;
    for (char& c : k) c = char(std::tolower(static_cast<unsigned char>(c)));
    m->mat_color[k] = rgb;
    m->dirty_color = true;
}
void MeshWorld::reset_rotation(const std::string& name) {
    if (MeshObj* m = find(name)) { m->rot = m->spawn_rot; m->dirty_pose = true; }
}
void MeshWorld::rotate(const std::string& name, const Vec3& deg, std::int32_t frames) {
    MeshObj* m = find(name);
    if (!m) return;
    const float k = 0.017453292f;                                  // 0x6bd0dc
    m->rot_delta = {deg.x * k, deg.y * k, deg.z * k};
    m->rot_steps = frames > 0 ? frames : -1;                       // cmovle
}
void MeshWorld::enable_event(const std::string& name, bool on) {
    if (MeshObj* m = find(name)) m->event_on = on;
}
void MeshWorld::set_time_scale(const std::string& name, float s) {
    if (MeshObj* m = find(name)) m->time_scale = s;
}

// FUN_005f5c80 (the blend path [H-med] is not modelled): the section, its start time
// (startFrame < 0 = its end), the counter cleared, then the time applied.
void MeshWorld::apply_clip(MeshObj& m, int idx, std::int32_t start) {
    m.sect = idx;
    const MeshClip& s = m.clips[std::size_t(idx)];
    const float t = start < 0 ? float(s.end) : float(s.start) + float(start);
    m.count = 0;
    set_time(m, t);
}

// FUN_005f5e10: chain sections run on, a loop section wraps (counting each wrap), a stop
// section clamps at its end and sets the counter to 1.
void MeshWorld::set_time(MeshObj& m, float t) {
    m.time = t;
    for (int guard = 0; guard < 64 && m.sect >= 0 && std::size_t(m.sect) < m.clips.size(); ++guard) {
        const MeshClip& s = m.clips[std::size_t(m.sect)];
        if (s.next >= 0 && std::size_t(s.next) < m.clips.size()) {
            if (t < float(s.end)) return;
            m.sect = s.next;
            t = float(m.clips[std::size_t(s.next)].start) + float(s.end);   // as written (WAITOPS U4)
            m.time = t;
            continue;
        }
        if (s.next == -2) {
            const float span = float(s.end - s.start + 1);
            while (m.time > float(s.end + 1)) { m.time -= span; ++m.count; }
        } else if (m.time >= float(s.end)) {
            m.time = float(s.end);
            m.count = 1;
        }
        return;
    }
}

void MeshWorld::fall(const std::string& name, float e, float h) {
    if (MeshObj* m = find(name)) {
        m->fall_start = m->pos.y;          // +0x44c = y before the lift
        m->fall_vel = 0.0f;
        m->fall_e = e;
        m->pos.y += h;
        m->dirty_pos = true;
    }
}

void MeshWorld::set_clip_now(const std::string& name, const std::string& clip, std::int32_t start) {
    MeshObj* m = find(name);
    if (!m) return;
    const std::string want = lower(clip);
    for (std::size_t i = 0; i < m->clips.size(); ++i)
        if (lower(m->clips[i].name) == want) { apply_clip(*m, int(i), start); m->dirty_pose = true; return; }
}

void MeshWorld::set_clip_index(const std::string& name, int idx, std::int32_t start) {
    MeshObj* m = find(name);
    if (!m || idx < 0 || std::size_t(idx) >= m->clips.size()) return;
    apply_clip(*m, idx, start);
    m->dirty_pose = true;
}

void MeshWorld::alpha_target(const std::string& name, float a, int frames) {
    if (MeshObj* m = find(name)) {
        m->alpha_target = a;
        m->alpha_step = frames == 0 ? 1.0f : 1.0f / float(frames);
    }
}

void MeshWorld::alpha_current(const std::string& name, float a) {
    if (MeshObj* m = find(name)) {
        const float was = m->alpha;
        m->alpha = a;                                                     // FUN_00572ba0: +0x418 = a
        if ((was > 0.0f) != (a > 0.0f)) m->dirty_pose = true;             // the alpha-0 collapse
        if (a != was) m->dirty_alpha = true;
    }
}

void MeshWorld::distance_to_alpha(const std::string& a, const std::string& b, float div, float k) {
    MeshObj* ma = find(a);
    if (!ma) return;
    const MeshObj* mb = find(b);
    if (!mb) return;
    // [BYTES 0x573acf..0x573b71] d = |yB - yA| (FUN_0041f170 = fabs via cvtss2sd/andps);
    // d /= div unless div == 0; f = 1 - d; 0 > f -> 0, else min(1, f).
    float d = std::fabs(mb->frame_y - ma->frame_y);
    if (div != 0.0f) d /= div;
    const float f = 1.0f - d;
    const float t = (0.0f > f) ? 0.0f : std::min(1.0f, f);
    const float was = ma->alpha;
    ma->alpha = (mb->fade + 1.0f) * 0.5f * k * mb->alpha * t;    // 0x6bd438 = 1.0, 0x6bd304 = 0.5
    if (ma->alpha != was) {
        ma->dirty_alpha = true;
        if ((was > 0.0f) != (ma->alpha > 0.0f)) ma->dirty_pose = true;   // the alpha-0 collapse
    }
}

void MeshWorld::sync_pos(const std::string& a, const std::string& b, bool x, bool y, bool z) {
    MeshObj* ma = find(a);
    if (!ma) return;
    const MeshObj* mb = find(b);
    if (!mb) return;
    const Vec3 was = ma->pos;
    if (x) ma->pos.x = mb->pos.x;
    if (y) ma->pos.y = mb->pos.y;
    if (z) ma->pos.z = mb->pos.z;
    if (ma->pos.x != was.x || ma->pos.y != was.y || ma->pos.z != was.z) ma->dirty_pos = true;
}

void MeshWorld::step() {
    for (auto& [nm, m] : objs_) {
        if (m.sleeping) continue;
        // 0. the alpha fade (FUN_00503190): +0x418 steps by +0x420 toward +0x41c.
        if (m.alpha != m.alpha_target) {
            const float was = m.alpha;
            if (m.alpha_target <= m.alpha) {
                m.alpha -= m.alpha_step;
                if (!(m.alpha_target < m.alpha)) m.alpha = m.alpha_target;
            } else {
                m.alpha += m.alpha_step;
                if (!(m.alpha < m.alpha_target)) m.alpha = m.alpha_target;
            }
            if ((was > 0.0f) != (m.alpha > 0.0f)) m.dirty_pose = true;
            if (m.alpha != was) m.dirty_alpha = true;
        }
        // 1. the anim advance (0x503fc8): time += rate x dt(1) x time scale
        if (!m.frozen && m.sect >= 0 && std::size_t(m.sect) < m.clips.size()) {
            const float before = m.time;
            set_time(m, m.time + m.clips[std::size_t(m.sect)].rate * 1.0f * m.time_scale);
            if (m.time != before) m.dirty_pose = true;
        }
        // 2. the scripted move (FUN_00503190 0x5032e4): constant speed, step then test; it
        //    stops on an overshoot or within the radius, snapping only when radius < 0.01.
        if (m.speed != 0.0f) {
            Vec3 d{m.dest.x - m.pos.x, m.dest.y - m.pos.y, m.dest.z - m.pos.z};
            const float l0 = len3(d);
            if (l0 != 0.0f) { const float k = 1.0f / l0; d = {d.x * k, d.y * k, d.z * k}; }
            m.pos = {m.pos.x + d.x * m.speed, m.pos.y + d.y * m.speed, m.pos.z + d.z * m.speed};
            const float l1 = len3({m.dest.x - m.pos.x, m.dest.y - m.pos.y, m.dest.z - m.pos.z});
            if (l1 > l0 || l1 <= m.radius) {
                if (m.radius < 0.01f) m.pos = m.dest;
                m.speed = 0.0f;
            }
            m.dirty_pos = true;
        }
        // 2b. the fall (FUN_00503190): v -= 0.02; y += v; at the start height it bounces with
        //     v = -e*v, y = start + e*(start - y), and stops once the bounce is under 0.025.
        if (m.fall_e != 0.0f) {
            m.fall_vel -= 0.02f;                                   // 0x6bd0ec
            m.pos.y += m.fall_vel;
            const float under = m.fall_start - m.pos.y;
            if (under >= 0.0f) {
                m.fall_vel = -(m.fall_e * m.fall_vel);
                m.pos.y = m.fall_e * under + m.fall_start;
                if (m.fall_vel < 0.025f) { m.fall_e = 0.0f; m.pos.y = m.fall_start; }   // 0x6bd0f8
            }
            m.dirty_pos = true;
        }
        // 2c. Mesh_Rotate (0x5034fa): while +0x414 != 0 (counting down when > 0) rot += delta, FUN_00503990(&rot).
        if (m.rot_steps != 0) {
            if (m.rot_steps > 0) --m.rot_steps;
            m.rot = {m.rot.x + m.rot_delta.x, m.rot.y + m.rot_delta.y, m.rot.z + m.rot_delta.z};
            m.dirty_pose = true;
        }
        // 3. the deferred clip (0x50354f): a case-insensitive section search; an unknown name
        //    leaves the old clip playing.
        if (!m.pending.empty()) {
            const std::string want = lower(m.pending);
            for (std::size_t i = 0; i < m.clips.size(); ++i)
                if (lower(m.clips[i].name) == want) {
                    apply_clip(m, int(i), m.pending_start);
                    m.dirty_pose = true;
                    break;
                }
            m.pending.clear();
        }
    }
}

void MeshWorld::finish_scripted() {
    for (auto& [nm, m] : objs_) {
        if (m.sleeping) continue;
        if (m.alpha != m.alpha_target) {
            if ((m.alpha > 0.0f) != (m.alpha_target > 0.0f)) m.dirty_pose = true;
            m.alpha = m.alpha_target;
            m.dirty_alpha = true;
        }
        // The move, the fall and the counted rotation by step()'s own arithmetic, so each ends where it would.
        for (int guard = 0; guard < 100000 && m.speed != 0.0f; ++guard) {
            Vec3 d{m.dest.x - m.pos.x, m.dest.y - m.pos.y, m.dest.z - m.pos.z};
            const float l0 = len3(d);
            if (l0 != 0.0f) { const float k = 1.0f / l0; d = {d.x * k, d.y * k, d.z * k}; }
            m.pos = {m.pos.x + d.x * m.speed, m.pos.y + d.y * m.speed, m.pos.z + d.z * m.speed};
            const float l1 = len3({m.dest.x - m.pos.x, m.dest.y - m.pos.y, m.dest.z - m.pos.z});
            if (l1 > l0 || l1 <= m.radius) {
                if (m.radius < 0.01f) m.pos = m.dest;
                m.speed = 0.0f;
            }
            m.dirty_pos = true;
        }
        for (int guard = 0; guard < 100000 && m.fall_e != 0.0f; ++guard) {
            m.fall_vel -= 0.02f;
            m.pos.y += m.fall_vel;
            const float under = m.fall_start - m.pos.y;
            if (under >= 0.0f) {
                m.fall_vel = -(m.fall_e * m.fall_vel);
                m.pos.y = m.fall_e * under + m.fall_start;
                if (m.fall_vel < 0.025f) { m.fall_e = 0.0f; m.pos.y = m.fall_start; }
            }
            m.dirty_pos = true;
        }
        for (; m.rot_steps > 0; --m.rot_steps) {
            m.rot = {m.rot.x + m.rot_delta.x, m.rot.y + m.rot_delta.y, m.rot.z + m.rot_delta.z};
            m.dirty_pose = true;
        }
        if (!m.pending.empty()) {
            const std::string want = lower(m.pending);
            for (std::size_t i = 0; i < m.clips.size(); ++i)
                if (lower(m.clips[i].name) == want) {
                    apply_clip(m, int(i), m.pending_start);
                    m.dirty_pose = true;
                    break;
                }
            m.pending.clear();
        }
        // The clip a Wait_MeshMotionFinish stood on: a chain section runs on into its next, a stop section lands
        // on its end (set_time's own rules); a loop section is left where it is.
        for (int guard = 0; guard < 64 && !m.frozen && m.sect >= 0 && std::size_t(m.sect) < m.clips.size(); ++guard) {
            const MeshClip& s = m.clips[std::size_t(m.sect)];
            if (s.next == -2 || (s.next < 0 && m.time >= float(s.end))) break;
            const int was = m.sect;
            set_time(m, float(s.end));
            m.dirty_pose = true;
            if (m.sect == was) break;
        }
    }
}

bool MeshWorld::moving(const std::string& name, bool* found) const {
    const MeshObj* m = find(name);
    if (found) *found = m != nullptr;
    return m && m->speed != 0.0f;
}

bool MeshWorld::motion_finished(const std::string& name, std::uint32_t n, bool* found) const {
    const MeshObj* m = find(name);
    if (found) *found = m != nullptr;
    if (!m || m->clips.empty()) return false;                      // no model: rewinds forever
    return m->count >= n;
}

}  // namespace cleria
