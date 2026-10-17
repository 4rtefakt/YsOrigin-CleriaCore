// Nygtilger (boss02) — see boss02.hpp and build/boss_re/NYGTILGER.md.
#include "cleria/boss02.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cleria {

namespace boss02 {

const char* const kClipNames[6] = {"walk", "stop", "attack", "damage", "roll_a", "roll_c"};

namespace {
const char* const kNames[kObjects] = {
    "boss02_h",   "boss02_b01", "boss02_b02", "boss02_b03", "boss02_b04", "boss02_b05",
    "boss02_b06", "boss02_b07", "boss02_b08", "boss02_b09", "boss02_b10", "boss02_b11",
    "boss02_b12", "boss02_b13", "boss02_b14", "boss02_b15", "boss02_b16", "boss02_b17",
    "boss02_c18", "boss02_d01", "boss02_d02", "boss02_d03", "boss02_d04", "boss02_d05",
    "boss02_d06", "boss02_d07", "boss02_d08", "boss02_d09", "boss02_d10", "boss02_d11",
    "boss02_d12", "boss02_d13", "boss02_d14", "boss02_d15", "boss02_d16", "boss02_d17",
};
}  // namespace

const char* object_name(int o) { return (o >= 0 && o < kObjects) ? kNames[o] : ""; }
const char* model_suffix(int o) {
    if (o == 0) return "";
    if (o < kPieces - 1) return "b";
    if (o == kPieces - 1) return "c";
    return "d";
}

}  // namespace boss02

using namespace boss02;

namespace {

// The engine's float constants (read from yso_win.exe with --f32 / --f64; § refs in the spec).
constexpr float kPi = 3.14159274f;         // 0x6bd6ac
constexpr float kHalfPi = 1.57079637f;     // 0x6bd4e4
constexpr float k2Pi = 6.28318548f;        // 0x6bd898
constexpr float kDeg = 0.0174532924f;      // 0x6bd0dc
constexpr float kFloor = 1.65f;            // 0x6bd4f4
constexpr float kDef999 = 999.0f;          // 0x4479c000
constexpr float kOff = 1.59697628f;        // 0x6bd4e8: pi/2 + 1.5 deg (mode 0/1 heading offset)
constexpr int kTicksDescend = 1080;        // 0x438
constexpr int kTicksHome = 1640;           // compare < 0x669

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
// FUN_0042cc50-style normalise: multiply by the reciprocal; zero stays zero.
Vec3 norm0(const Vec3& v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l == 0.0f) return {0, 0, 0};
    const float k = 1.0f / l;
    return {v.x * k, v.y * k, v.z * k};
}
float atan2f_(float y, float x) { return float(std::atan2(double(y), double(x))); }   // FUN_0066f80a
float cosf_(float a) { return float(std::cos(double(a))); }                            // FUN_0066fd70
float sinf_(float a) { return float(std::sin(double(a))); }                            // FUN_00670d30
// FUN_005ef010: into [0, 2pi].
float norm_2pi(float x) {
    constexpr double k2PiD = 6.2831854820251465;
    if (x > k2PiD) return float(std::fmod(double(x), k2PiD));
    if (x < 0) return float(std::fmod(double(x), k2PiD) + k2PiD);
    return x;
}
// The inline wrap after a norm-difference (-pi 0x6be0ac / 2pi 0x6bd898).
float wrap_d(float d) {
    if (d >= 0.0f) { if (kPi < d) d -= k2Pi; }
    else if (d < -kPi) d += k2Pi;
    return d;
}
// FUN_005eec50(a, b) = wrap(norm(b) - norm(a)).
float angle_diff(float a, float b) { return wrap_d(norm_2pi(b) - norm_2pi(a)); }
// FUN_00444660: into [-pi, pi).
float wrap_pi(float x) {
    if (x >= kPi) return float(std::fmod(double(x + kPi), double(k2Pi))) - kPi;
    if (x < -kPi) return float(std::fmod(double(x - kPi), double(k2Pi))) + kPi;
    return x;
}
// FUN_005eecc0(rate, cur, tgt): step cur by +-rate the way that ends nearer tgt, snap when
// within the rate; result folded to [-pi, pi].
float turn_toward(float rate, float cur, float tgt) {
    float r = tgt;
    if (rate < kPi && cur != tgt) {
        const float up = cur + rate, dn = cur - rate;
        const float dup = std::fabs(wrap_d(norm_2pi(up) - norm_2pi(tgt)));
        float best = std::fabs(wrap_d(norm_2pi(dn) - norm_2pi(tgt)));
        r = dn;
        if (dup < best) { r = up; best = dup; }
        if (best <= rate) r = tgt;
    }
    if (kPi <= r || r < -kPi) r = wrap_pi(r);
    return r;
}
// The smoothstep the driver / hoop write out in float (= 3t^2 - 2t^3), §4 helpers.
float smooth(float t) {
    if (!(0.0f < t)) return 0.0f;
    if (!(t < 1.0f)) return 1.0f;
    const float a = 1.0f / (1.0f - t);
    return ((a - 2.0f) + 1.0f / t) * t * t * t + ((3.0f - (a + a)) - 1.0f / t) * t * t + a * t;
}

// D3DX row-vector matrices.
Mat4 mat_translate(const Vec3& t) { Mat4 m; m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z; return m; }
Mat4 mat_rot_x(float a) {
    const float c = cosf_(a), s = sinf_(a);
    Mat4 m; m.m[5] = c; m.m[6] = s; m.m[9] = -s; m.m[10] = c; return m;
}
Mat4 mat_rot_y(float a) {
    const float c = cosf_(a), s = sinf_(a);
    Mat4 m; m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c; return m;
}
Mat4 mat_rot_z(float a) {
    const float c = cosf_(a), s = sinf_(a);
    Mat4 m; m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c; return m;
}
// D3DXMatrixRotationYawPitchRoll: roll (Z), then pitch (X), then yaw (Y).
Mat4 mat_ypr(float yaw, float pitch, float roll) { return mat_rot_z(roll) * mat_rot_x(pitch) * mat_rot_y(yaw); }
void set_rows3(Mat4& dst, const Mat4& src) { for (int i = 0; i < 12; ++i) dst.m[std::size_t(i)] = src.m[std::size_t(i)]; }
void set_pos(Mat4& m, const Vec3& p) { m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z; }

// D3DX quaternions (x, y, z, w) for the follow's slerp branch.
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
Quat quat_from(const Mat4& a) {   // D3DXQuaternionRotationMatrix (rows 0-2)
    const auto& m = a.m;
    const float tr = m[0] + m[5] + m[10];
    Quat q;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s; q.x = (m[6] - m[9]) / s; q.y = (m[8] - m[2]) / s; q.z = (m[1] - m[4]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q.w = (m[6] - m[9]) / s; q.x = 0.25f * s; q.y = (m[1] + m[4]) / s; q.z = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q.w = (m[8] - m[2]) / s; q.x = (m[1] + m[4]) / s; q.y = 0.25f * s; q.z = (m[6] + m[9]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q.w = (m[1] - m[4]) / s; q.x = (m[8] + m[2]) / s; q.y = (m[6] + m[9]) / s; q.z = 0.25f * s;
    }
    return q;
}
Mat4 mat_from(const Quat& q) {    // D3DXMatrixRotationQuaternion
    Mat4 m;
    m.m[0] = 1 - 2 * (q.y * q.y + q.z * q.z); m.m[1] = 2 * (q.x * q.y + q.z * q.w); m.m[2] = 2 * (q.x * q.z - q.y * q.w);
    m.m[4] = 2 * (q.x * q.y - q.z * q.w); m.m[5] = 1 - 2 * (q.x * q.x + q.z * q.z); m.m[6] = 2 * (q.y * q.z + q.x * q.w);
    m.m[8] = 2 * (q.x * q.z + q.y * q.w); m.m[9] = 2 * (q.y * q.z - q.x * q.w); m.m[10] = 1 - 2 * (q.x * q.x + q.y * q.y);
    return m;
}
Quat slerp(const Quat& a, Quat b, float t) {   // D3DXQuaternionSlerp (shortest arc)
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { d = -d; b = {-b.x, -b.y, -b.z, -b.w}; }
    float wa = 1.0f - t, wb = t;
    if (d < 0.9999f) {
        const float th = std::acos(d), s = std::sin(th);
        wa = std::sin((1.0f - t) * th) / s;
        wb = std::sin(t * th) / s;
    }
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Part offsets T(0, 1.15, DAT_0069ceb4[role]) [BYTES 0x5196b5].
constexpr float kPartZ[3] = {0.35f, 1.0f, 0.1f};

}  // namespace

// ---------------------------------------------------------------------------
// FUN_00513d80 (§2.2).
static boss02::Stats make_stats(const Boss02Setup& s) {
    boss02::Stats t;
    if (!s.diff_easy) {
        if (s.diff_index == 1) {
            t = {750, 375, 102, 107, 117, 81, 99, 102, 98, 125, 260, 90, 14, 220, 0.0075f, 115, 103, false};
        } else if (s.diff_index == 2) {
            t = {800, 400, 104, 113, 121, 82, 100, 102, 99, 110, 250, 60, 15, 200, 0.01f, 119, 104, true};
        }
        // diff_index 0 (and, in the engine, any other value leaves the globals stale): Normal.
        return t;
    }
    t = {600, 275, 98, 101, 108, 75, 93, 100, 97, 170, 350, 180, 10, 290, 0.0015f, 110, 102, false};
    if (!s.time_attack) {
        float n;
        if (!s.diff_very_easy) n = float(s.retries < 20 ? (s.retries >> 1) : s.retries - 10);
        else n = std::pow(float(s.retries), 1.25f);   // FUN_0041b790 [H U11: powf 1.25 as Velagunder]
        // Each value only lowered; STRs clamped to >= 1, DEFs to >= 0 [BYTES 0x513f40..0x5141b3].
        auto lower = [](float base, float v, float lo, float& dst) {
            if (lo <= v) { if (v < base) dst = v; } else dst = lo;
        };
        lower(98.0f, 98.0f - n, 1.0f, t.head_str);
        lower(101.0f, 101.0f - n, 1.0f, t.body_str);
        lower(108.0f, 108.0f - (n + n), 1.0f, t.charged_str);
        lower(75.0f, 75.0f - n, 0.0f, t.head_def_open);
        lower(93.0f, 93.0f - n, 0.0f, t.tail_def);
    }
    return t;
}

// ---------------------------------------------------------------------------
// Construction: FUN_00514200 for boss02_h, then the 35 pieces it builds (§2.1), then the
// tail's pre-roll FUN_005136f0(2).
Boss02::Boss02(const Boss02Setup& s) {
    st_ = make_stats(s);
    char_ = s.character;
    bb5c_ = s.diff_index;
    easy_ = s.diff_easy;
    time_attack_ = s.time_attack;
    level_ = s.level;
    rec3_ = s.rec3;
    rec4_ = float(int(s.rec4));
    exp_b0_ = s.exp_b0;
    const bool toal = char_ == 3;
    const Mat4 at = mat_translate(s.pos);   // every piece is created at the head position, rot 0

    for (int o = 0; o < kObjects; ++o) {
        Boss02Piece& p = obj_[std::size_t(o)];
        p.world = at;
        if (o == 0) { p.role = kHead; p.idx = 0; }
        else if (o < kPieces - 1) { p.role = kBody; p.idx = o; p.twin = kPieces - 1 + o; }
        else if (o == kPieces - 1) { p.role = kTail; p.idx = kPieces - 1; }
        else { p.role = kTwin; p.owner = o - (kPieces - 1); p.idx = p.owner + 1; }   // idx = vector size at creation
        p.kp = p.idx * 30 + 5;                                   // blk+8
        p.wob = float(p.idx) * 0.95f;                            // blk+0x24 (0x6bd3f8)
        p.f298 |= 0x2000;
        if (p.role != kTwin) p.f28c |= 8;
        if (p.role == kHead) p.f28c |= 4;
        p.f60 = 0x88;
        p.stone = 1.0f;                                          // blk+0x210: the statue
        p.clip = kStop;                                          // "stop" (blend 0), blk+0x28 = 1
        p.cracked = p.role == kTail;                             // the tail has no crack stage
        p.entries_struck = s.entries_struck;
        if (p.role == kHead) {
            p.max_hp = st_.head_hp;                              // overwrites the EEX HP (N4)
            p.str = s.eex_str;                                   // the brain rewrites it every frame
            p.def = kDef999;
            p.frozen = true;                                     // obj+0x28f = 1 until woken
        } else {
            p.max_hp = st_.seg_hp;
            p.str = float(int(s.eex_str));                       // FUN_00501a30(.., (int)head STR, ..) [H]
            p.def = p.role == kBody ? kDef999 : st_.tail_def;
        }
        p.hp = p.max_hp;
        p.last_hp = p.max_hp;
        if (toal) { p.resist[0] = -0.333333313f; p.resist[2] = -0.666666627f; }   // 0xbeaaaa3b / 0xbf2aaae3
        if (p.role == kHead || p.role == kTail) {
            p.has_part = true;
            p.part.alive = true;
            p.part.hp = p.hp * 10.0f;                            // 0x6bd928
            p.part.def = p.def;
            p.part.ce = true;
            for (int k = 0; k < 4; ++k) p.part.resist[k] = p.resist[k];
        }
        if (p.role != kTwin) {                                   // the pool G+4 / G+8 (§3.3)
            g_.pool += p.max_hp;
            g_.pool_max = g_.pool;
        }
    }
    // Update order [H U14]: the head (the script's object), then each b followed by its d (the
    // ctor creates d right after b), the tail last.
    int n = 0;
    order_[std::size_t(n++)] = 0;
    for (int j = 1; j < kPieces - 1; ++j) { order_[std::size_t(n++)] = j; order_[std::size_t(n++)] = kPieces - 1 + j; }
    order_[std::size_t(n++)] = kPieces - 1;

    // The trail: 600 records of {head pos, angles 0, flags 0} (§2.1 step 5).
    for (Boss02TrailRec& r : trail_) r = {s.pos, 0, 0, 0, 0};
    // Created last, the tail lays the worm out: G+0x14/18/1c = 0 and FUN_005136f0(2).
    g_.mode = g_.t = g_.t2 = 0;
    preroll(2);
}

// ---------------------------------------------------------------------------
std::uint32_t Boss02::draw() { return in_ && in_->rng ? in_->rng() : 0u; }
float Boss02::U() {
    const std::uint32_t w = (draw() & 0x7fffffu) | 0x3f800000u;
    float f;
    static_assert(sizeof f == sizeof w, "float");
    std::memcpy(&f, &w, sizeof f);
    return f - 1.0f;
}

int Boss02::nvec() const { return kPieces; }

void Boss02::set_clip(int o, int clip, int blend, float start) {
    if (out_) out_->clips.push_back({o, clip, blend, start});
    clip_reset_[std::size_t(o)] = true;              // FUN_005f5c10 zeroes the completion count
}

void Boss02::debris(int o, const Vec3& p, int edx, int s1, int s2, bool flag) {
    if (in_ && in_->cosmetic) {
        Boss02Cosmetic c;
        c.kind = Boss02Cosmetic::kDebris;
        c.object = o; c.pos = p; c.edx = edx; c.s1 = s1; c.s2 = s2; c.flag = flag;
        in_->cosmetic(c);
    }
}

Boss02Effect& Boss02::fx(int id, int o, const Vec3& p) {
    Boss02Effect e;
    e.id = id;
    e.object = o;
    e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}

// FUN_0051ad80 (and the driver's inline copy): 0 both loops off, 1 crawl (216), 2 roll (217).
void Boss02::loop_state(int s) {
    if (g_.loop == s) return;
    g_.loop = s;
    if (s == 0) { se_stop(216, 1000); se_stop(217, 1000); }
    else if (s == 1) { se_loop(216); se_stop(217, 1000); }
    else if (s == 2) { se_stop(216, 1000); se_loop(217); }
}

// ---------------------------------------------------------------------------
// One engine frame (§5.3).
Boss02Output Boss02::tick(const Boss02Input& in) {
    Boss02Output out;
    in_ = &in;
    out_ = &out;
    player_now_ = in.player_pos;
    player_hp_ = in.player_hp;
    f156_ = in.fight;
    wake_ = in.wake;
    flag221_ = in.flag221;
    balls_spawned_ = 0;
    clip_reset_.fill(false);
    for (int o : order_) {
        if (!obj_[std::size_t(o)].exists) continue;
        brain(o);
        if (obj_[std::size_t(o)].exists) common_update(o);
    }
    in_ = nullptr;
    out_ = nullptr;
    return out;
}

// [STAND-IN order] the GoMesh common update FUN_00503190 that runs after the brain in
// FUN_00562990: the alpha fade (+0x418 toward +0x41c by +0x420) and Mesh_Fall (as
// src/cleria/meshobj.cpp ports it).
void Boss02::common_update(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.alpha != p.alpha_target) {
        if (p.alpha_target <= p.alpha) {
            p.alpha -= p.alpha_step;
            if (!(p.alpha_target < p.alpha)) p.alpha = p.alpha_target;
        } else {
            p.alpha += p.alpha_step;
            if (!(p.alpha < p.alpha_target)) p.alpha = p.alpha_target;
        }
    }
    if (p.fall_e != 0.0f) {
        Vec3 pos = p.pos();
        p.fall_vel -= 0.02f;                       // 0x6bd0ec
        pos.y += p.fall_vel;
        const float under = p.fall_start - pos.y;
        if (under >= 0.0f) {
            p.fall_vel = -(p.fall_e * p.fall_vel);
            pos.y = p.fall_e * under + p.fall_start;
            if (p.fall_vel < 0.025f) { p.fall_e = 0.0f; pos.y = p.fall_start; }   // 0x6bd0f8
        }
        set_pos(p.world, pos);
    }
}

// ---------------------------------------------------------------------------
// The brain FUN_00518ae0 for one object.
void Boss02::brain(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    p.start_pos = p.pos();                          // blk+0x204
    if (g_.phase == 4) {
        if (death_block(o)) return;                 // hidden: nothing else this frame
    } else {
        if (g_.awake) stone_roll(o);
        if (p.role == kHead) {
            if (f156_) player_ring();
            driver();
            if (g_.hold) --g_.hold;
            if (!p.frozen) {
                if (!g_.dormant) trail_shift();
                follow(o, false, -1.0f, 1.2f);      // 0x6be058 / 0x6bd484
            }
        } else {
            p.frozen = obj_[0].frozen;
            if (p.role == kTwin) {
                if (p.owner >= 0) p.world = mat_translate({0, 1.25f, 0}) * obj_[std::size_t(p.owner)].world;
            } else {
                if (!p.frozen) {
                    seg_extras(o);
                    follow(o, false, -1.2f, 1.2f);  // 0x6be064 / 0x6bd484
                }
                // "The statue stirs" (intro flag 167) [BYTES 0x518dfb..0x518e43].
                if (!g_.awake && in_->stir && R(250) == 0)
                    debris(o, p.world.transform_point({0, 1.75f, 0}), 1, 5, 5, false);
            }
        }
        // blk+0x54 (the slerp smoothing) bookkeeping [BYTES 0x518e3b..0x518e96].
        if (g_.hs == 2) p.smooth = 100;
        else if (p.smooth != 0) {
            if (p.smooth == 100) { if (p.pos().y < 2.65f) p.smooth = 99; }   // 0x6bd63c
            else --p.smooth;
        }
        // The head's flinch when a segment died (G+0xd1).
        if (p.role == kHead && g_.flinch) {
            if (p.clip == kDamage) g_.flinch = false;
            else if (p.clip == kStop || p.clip == kWalk) { p.clip = kDamage; set_clip(o, kDamage, 1); }
        }
        // Walk / stop [BYTES 0x518ede..0x518f78].
        if (!(g_.trail_mask & 1)) {
            if (p.clip != kStop) { p.clip = kStop; set_clip(o, kStop, 1); }
        } else {
            const bool done = in_->clip_done[std::size_t(o)] && !clip_reset_[std::size_t(o)];
            if (((p.clip == kAttack || p.clip == kDamage || p.clip == kRollC) && done) || p.clip == kStop) {
                p.clip = kWalk;
                set_clip(o, kWalk, 1, float(2 * ((p.idx >> 2) + (p.idx & 3))));   // desynchronised gait
            }
        }
    }

    // ---- LAB_0051968a: the shared tail ----
    bool died = false;
    if (p.role == kTwin) {
        bool gone = false;
        twin_tail(o, gone);
        if (gone) return;
    } else {
        parts_and_crack(o);
    }
    if (p.flash != 0.0f) {                          // flash decay 0.075 (0x6bd174)
        p.flash -= 0.075f;
        if (p.flash < 0.0f) p.flash = 0.0f;
    }
    if (p.role == kTwin) {
        if (p.owner >= 0) p.glow_mat = obj_[std::size_t(p.owner)].glow_mat;   // the owner's material [visual]
    } else {
        glow_plasma_husk(o);
    }
    if (p.role == kHead && p.has_broken) head_glow_update(o);
    if (p.role == kTail) tail_bullets();
    hp_change(o, died);
    if (p.role == kHead && g_.phase == 3 && p.has_broken) died = true;   // the head's shell breaks at phase 3
    if (p.role == kHead || p.role == kBody)        // FUN_0051c180: the live arcs age (visual)
        for (int& l : p.arc_life) if (l != 0) --l;
    if (died && p.has_broken) shell_swap(o);
    if (p.role == kHead) {
        // Phases [BYTES 0x51ab1a..0x51ab85].
        int ph = g_.phase;
        if (ph < 2 && g_.pool <= g_.pool_max * 0.5f) { g_.phase = 2; ph = 2; }
        if (ph == 2) {
            if (g_.pool <= p.max_hp) {
                g_.phase = 3;
                if (g_.pool < 1.0f) g_.phase = 4;
            }
        } else if (ph == 3) {
            if (g_.pool < 1.0f) g_.phase = 4;
        }
        // The HP bar (§10): value = the pool, max = the pool max.
        if (f156_ && bar_last_ != g_.pool) {
            bar_last_ = g_.pool;
            out_->gauge = Boss02Gauge{g_.pool, g_.pool_max, 4500.0f, 2};
        }
    }
    stat_block(o);
}

// The stone shell roll (§5.5) [BYTES 0x518b58..0x518caa].
void Boss02::stone_roll(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.role == kTwin) {
        if (p.owner >= 0) p.stone = obj_[std::size_t(p.owner)].stone;
        return;
    }
    if (p.stone == 1.0f) {
        if (R(300) < 3) {
            p.stone = 0.999899983f;                    // 0x3f7ff972
            const Vec3 at = p.world.transform_point({0, 1.0f, 0});
            Boss02Effect& e = fx(0x156, o, at);       // 342 bs_02_shot01, no stat
            e.flag201 = true;
            debris(o, at, 2, 5, 5, g_.mode == 1);
            const int id = 224 + int(~std::uint32_t(g_.shots) & 1u);
            ++g_.shots;
            se3d(id, at);
        }
    } else {
        p.stone -= 0.015f;                             // 0x6bd0c0
        if (p.stone < 0.0f) p.stone = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// The twin plate's half of the tail (§3.4) [EXPORT 00518ae0.c:476-539].
void Boss02::twin_tail(int o, bool& gone) {
    Boss02Piece& d = obj_[std::size_t(o)];
    const float lost = d.last_hp - d.hp;
    if (0.0f < lost) {
        d.last_hp = d.hp;
        if (d.owner >= 0) {
            Boss02Piece& b = obj_[std::size_t(d.owner)];
            b.hp -= lost;                              // damage passes to the segment
            if (b.hp < 2.0f) b.hp = 0.0f;
            b.flash = 1.65f;
        }
        d.flash = 1.65f;
    }
    if (d.owner >= 0) {
        Boss02Piece& b = obj_[std::size_t(d.owner)];
        if (b.cracked && d.has_broken) {               // swap to BBOSS02D, SE 212
            d.has_broken = false;
            d.broken = true;
            out_->swaps.push_back({o});
            se2d(212);
        }
        if (b.hp < 2.0f) {                             // the segment is dead: fade, uncollide
            d.alpha_target = 0.0f;
            d.alpha_step = 0.03f;                      // 0x3cf5c28f
            d.hurts = false;
            d.f298 |= 0x40;
            d.f60 &= ~0x80u;
            d.entries_struck = false;
        }
    }
    if (d.alpha <= 0.0f) {                             // unlink and destroy itself
        if (d.owner >= 0) obj_[std::size_t(d.owner)].twin = -1;
        d.owner = -1;
        d.exists = false;
        d.f60 &= ~0x80u;
        gone = true;
    }
}

// Parts, damage consumption, the crack latch (§3.1, §3.4) [EXPORT 00518ae0.c:541-595].
void Boss02::parts_and_crack(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.has_part && p.part.alive) {
        const Vec3 c = (mat_translate({0, 1.15f, kPartZ[p.role]}) * p.world).transform_point({0, 0, 0});
        p.part.pos = c;                                // +0x50, +0x6c (hurt centre), +0xdc
        if (p.part.dmg != 0.0f) {
            p.hp -= p.part.dmg;
            p.part.dmg = 0.0f;
            p.flash = 1.65f;                           // 0x3fd33333
            if (p.twin >= 0) obj_[std::size_t(p.twin)].flash = 1.65f;
        }
    }
    if (!p.cracked && p.hp < p.max_hp * 0.75f) {       // 0x6bd390
        p.cracked = true;
        p.crack_t = 12;
    }
    if (p.crack_t != 0 && --p.crack_t == 0) {
        if (p.has_part && p.part.alive) p.part.ce = false;
        Boss02Effect& e = fx(0x15a, o, add(p.pos(), {0, 1.25f, 0}));   // 346 bs_02_armorbreak
        e.stat = Boss02Effect::kPiece;
        e.power = 0.0f; e.knock = 1.0f;
    }
}

// Glow timers, the plasma caster, the husk's poison charge (§5.6, §7.1, §7.2)
// [EXPORT 00518ae0.c:608-764].
void Boss02::glow_plasma_husk(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.glow_t == 0) {
        p.glow = 0.0f;
        if (p.role == kBody && !obj_[0].frozen) {
            if (p.hp < 2.0f) {                         // a dead husk may spit a poison ball
                if ((!easy_ || g_.pool < g_.pool_max * 0.666f) && 1.0f <= g_.pool && p.husk == 0 &&
                    g_.balls <= st_.ball_cap && R(std::uint32_t(st_.husk_roll)) < 1) {
                    ++p.husk;
                    p.glow_t = 50;
                }
            } else if (g_.mode != 4) {
                const std::uint32_t n = g_.mode == 1 ? 130u : 150u;
                if (R(n) == 0) {
                    p.glow_t = 50;
                    const Vec3 v{0, 2.7f, 0.8f};
                    if (p.fire && g_.fire_ok) {        // balls fly only from the coil (mode 1)
                        plasma_spawn(o, v);
                        const int id = 224 + int(~std::uint32_t(g_.shots) & 1u);
                        ++g_.shots;
                        se3d(id, v);                   // the LOCAL vector used as a world position (original bug)
                    }
                }
            }
        }
    } else {
        const int v = --p.glow_t;
        if (v < 15) p.glow = float(v) / 15.0f;         // 0x6bd964
        else if (35 < v) p.glow = float(50 - v) / 15.0f;
        else p.glow = 1.0f;
        if (p.role == kBody && 2.0f <= p.hp && R(20) == 0) {
            const int s = int((draw() >> 27) & 1u);
            if (p.arc_life[std::size_t(s)] == 0) arc_spawn(o, s);
        }
    }
    if (p.husk != 0) {
        const int k = ++p.husk;
        if (k == 2) {
            p.husk_h[0] = U() * 0.5f + 0.0f + 2.5f;
            p.husk_h[1] = U() * 0.5f + 0.0f + p.husk_h[0];
            p.husk_h[2] = U() * 0.5f + 0.0f + p.husk_h[1];
        }
        for (int j = 0; j < 3; ++j) {                  // the arc targets (visual)
            const Vec3 q = p.world.transform_point({0, p.husk_h[j], 0});
            if (k == 2) p.husk_q[j] = q;
            else p.husk_q[j] = add(p.husk_q[j], mul(q - p.husk_q[j], 0.35f));
        }
        if (k == 30 || k == 60 || k == 90) arc_spawn(o, 0);
        if (k == 90 && 1.0f <= g_.pool) {
            poison_spawn(o, p.world.transform_point({0, 1.0f, 0}));
            se2d(226);
        }
        if (p.husk == 300) p.husk = 0;
    }
    float gsum = p.glow;
    if (1.0f < p.glow) { p.glow = 1.0f; gsum = 1.0f; }
    if (p.idx != 0) gsum = obj_[std::size_t(p.idx - 1)].glow * 0.32f + gsum;   // 0x6bd28c
    if (p.idx < nvec() - 1) gsum = obj_[std::size_t(p.idx + 1)].glow * 0.32f + gsum;
    p.glow = gsum;
    p.glow_mat = std::min(gsum, 1.0f);
}

// The head's electric glow (laser / blink / bump) [EXPORT 00518ae0.c:765-813] — visual.
void Boss02::head_glow_update(int o) {
    const Boss02Piece& p = obj_[std::size_t(o)];
    int i = g_.glow_laser;
    if (i != 0) g_.glow_laser = --i;
    float a;
    if (i < 40) a = float(i) / 40.0f;
    else {
        a = 1.0f;
        if (79 < i) { a = 0.0f; if (i < 140) a = float(120 - i) / 60.0f; }
    }
    int j;
    if (g_.glow_blink == 0) {
        j = 0;
        if (!p.frozen && p.stone <= 0.01f) { g_.glow_blink = 64; j = 64; }
    } else {
        j = --g_.glow_blink;
    }
    float b = 0.0f;
    if (j < 19) b = float(j) / 19.0f;
    else if (j < 38) b = float(38 - j) / 19.0f;
    b *= 0.6f;
    float c = 0.0f;
    if (g_.glow_bump != 0) {
        --g_.glow_bump;
        c = (float(g_.glow_bump) / 12.0f) * 0.8f;
    }
    head_glow_ = std::min(c + b + a, 1.0f);
}

// The tail's bullet bookkeeping (§7.3) [EXPORT 00518ae0.c:815-940].
void Boss02::tail_bullets() {
    g_.balls = 0;
    std::vector<int> L;
    for (int i = int(in_->bullets.size()) - 1; i >= 0; --i) {
        const Boss02BulletView& b = in_->bullets[std::size_t(i)];
        if (b.kind == 0x24) { if (60 < b.n) L.push_back(i); }
        else if (b.kind == 0x23) ++g_.balls;
    }
    g_.balls += balls_spawned_;                        // [H] this frame's balls are in the list by now
    g_.markers = int(L.size());
    for (std::size_t a = 0; a < L.size(); ++a) {
        const Vec3 pa = in_->bullets[std::size_t(L[a])].pos;
        Vec3 acc{};
        for (std::size_t b = 0; b < L.size(); ++b) {
            if (a == b) continue;
            const Vec3 d = in_->bullets[std::size_t(L[b])].pos - pa;
            const float d2 = d.y * d.y + d.x * d.x + d.z * d.z;
            if (d2 < 1.0f && d2 != 0.0f) {             // [H] the engine divides by |d| unguarded
                const float l = std::sqrt(d2), k = 1.0f / l, s = -(1.0f - l);
                acc.x += k * d.x * s * 0.015f;
                acc.y += k * d.y * s * 0.015f;
                acc.z += k * d.z * s * 0.015f;
            }
        }
        const float r = std::sqrt(pa.x * pa.x + pa.z * pa.z);
        if (r - 9.5f < 0.0f) {                         // 0x6bd910: pushed out past the pillar
            float fx = pa.x / r, fz = pa.z / r;
            if (r != 0.0f) { fx /= r; fz /= r; }
            acc.x += fx * 0.25f;                       // 0x6bd244
            acc.z += fz * 0.25f;
        }
        out_->nudges.push_back({L[a], acc});
    }
}

// The per-frame HP delta, the pool, a piece's death (§3.3) [EXPORT 00518ae0.c:966-1007].
void Boss02::hp_change(int o, bool& died) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.last_hp == p.hp) return;
    if (p.role == kHead) {
        g_.pool -= p.last_hp - p.hp;
        if (g_.pool < 1.0f) out_->rumble.push_back(0);
    }
    const float thr = p.role == kHead ? 1.0f : 2.0f;   // 0x6bd438 / 0x6bd580
    if (p.hp < thr) {
        p.hp = 0.0f;
        died = true;
        if (!p.cracked) { p.cracked = true; p.crack_t = 12; }
        if (p.role != kHead) {                         // a segment dies: the pool loses its whole max
            g_.flinch = true;
            g_.pool -= p.max_hp;
        }
        if (p.has_part && p.part.alive) p.part.alive = false;   // count--, +0x11 |= 2, slot = 0
        if (g_.pool < 1.0f) g_.pool = 0.0f;
    }
    p.last_hp = p.hp;
}

// The shell swap to BBOSS02<s> (§3.4) [EXPORT 00518ae0.c:1029-1102].
void Boss02::shell_swap(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    p.has_broken = false;
    p.broken = true;
    out_->swaps.push_back({o});
    // FUN_005f5c80(blk+0x28, 0): the clip id re-applied as a section INDEX of the new model [H].
    out_->clips.push_back({o, p.clip, 0, 0.0f, true});
    clip_reset_[std::size_t(o)] = true;
    Boss02Effect& e = fx(0x159, o, p.world.transform_point({0, 1.25f, 0}));   // 345 bs_02_crushsmoke
    e.stat = Boss02Effect::kPiece;
    e.power = 0.0f; e.knock = 1.0f;
    se2d(211);
    const Vec3 dp = add(p.pos(), {0.0f, 1.55f, -0.5f});   // 0x6bd4dc / 0x6bd304
    debris(o, dp, 3, 0, 5, false);
    if (p.role == kHead) {                             // 354 bs_02_eye on node "Bone02"
        Boss02Effect& eye = fx(0x162, o, {0, 0, 0});
        eye.stat = Boss02Effect::kPiece;
        eye.power = 0.0f; eye.knock = 1.0f;
        eye.attach = Boss02Effect::kBone02;
        eye.attach_off = {0.7f, 0.0f, 1.5f};
        eye.slot = Boss02Effect::kEye;
        g_.eye_fx = true;
    }
    if (in_->fx_option) {                              // FUN_0058a5f0 screen debris (2 jitter draws)
        Vec3 q = dp;
        q.x = (U() * 3.0f - 1.5f) + q.x;               // 0x6bd680 / 0x6bd4d4
        q.z = (U() * 3.0f - 1.5f) + q.z;
        if (in_->cosmetic) {
            Boss02Cosmetic c;
            c.kind = Boss02Cosmetic::kScreenDebris;
            c.object = o;
            c.pos = q;
            in_->cosmetic(c);
        }
    }
}

// The per-frame stat writes (§3.5) [EXPORT 00518ae0.c:1127-1189].
void Boss02::stat_block(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    p.hurts = false;
    if (!(1.0f <= g_.pool)) return;
    if (p.role == kHead) {
        p.str = st_.head_str;
        if (g_.phase == 3) {
            p.f28c = std::uint8_t(p.f28c & 0xf7u);
            p.def = st_.head_def_open;                 // the shell is broken: DEF 80
        } else {
            p.def = kDef999;
            p.hurts = true;                            // the head always hurts on touch
            if (g_.mode == 4) p.f28c = std::uint8_t(p.f28c & 0xfbu);
            else p.f28c |= 4;
        }
        if (player_hp_ < 1.0f) p.def = kDef999;
        p.f298 &= ~0x20000u;
        if (g_.charged) {
            p.str = st_.charged_str;
            p.hurts = true;
            p.f298 |= 0x20000u;
        }
        if (p.has_part && p.part.alive) p.part.def = p.def;
    } else if (p.role != kTwin) {
        if (p.role == kBody) p.str = st_.body_str;
        else { p.entries_struck = true; p.hurts = false; }   // the tail: every entry +0x8a = 1
        p.f298 &= ~0x20000u;
        if (g_.charged) {
            p.hurts = true;
            p.f298 |= 0x20000u;
            p.str = st_.charged_str;
        }
    }
}

// ---------------------------------------------------------------------------
// The death sequence (§8) [EXPORT 00518ae0.c:128-330]. true = the piece is hidden and the
// brain returned.
bool Boss02::death_block(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.clip != kStop) {                             // the first frame (blk+0x28 != 1)
        p.clip = kStop;
        set_clip(o, kStop, 1);
        if (p.role == kHead) {
            if (1.0f <= player_hp_) {
                out_->player_iframes = true;           // +0x734 = max(+0x734, 9999), FUN_00461120(2)
                if (!g_.sting) { g_.sting = true; se2d(228); }
                if (!time_attack_ && flag221_ == 0) {  // cmove: only if it was 0
                    flag221_ = 1;
                    out_->flags.push_back({221, 1});
                }
            }
            loop_state(0);
            se_stop(213, 1000);
            se_stop(218, 1000);
            se_stop(220, 1000);
        }
        p.death_delay = int((draw() >> 21) & 15u) + 1;   // note >> 21
        if (g_.rolling_fx) { out_->kill_slots.push_back(Boss02Effect::kRolling); g_.rolling_fx = false; }
        if (g_.eye_fx) { out_->kill_slots.push_back(Boss02Effect::kEye); g_.eye_fx = false; }
        out_->kill_all_effects = true;                 // every GoEffect: +0x11 |= 2, +0x60 &= ~0x80
    }
    const int k = ++p.death_k;
    bool started = false;
    if (k == p.death_delay && kFloor <= p.pos().y) {   // start the fall and the tumble
        p.fall_e = 0.35f;                              // +0x454 / +0x450 = 0x3eb33333
        p.fall_vel = 0.35f;
        p.fall_start = 1.9f;                           // +0x44c = 0x3ff33333
        // S() = ((x - 1) + (x - 1)) - 1 with x = the [1, 2) float of one draw: 5 draws.
        auto S = [&]() { const float x = U() + 1.0f; return ((x - 1.0f) + (x - 1.0f)) - 1.0f; };
        Vec3 spin;
        spin.x = S();
        spin.y = S();
        spin.z = S();
        p.spin = mul(norm0(spin), 0.2f);               // 0x6bd210
        Vec3 dr;
        dr.x = S();
        dr.y = 0.0f;
        dr.z = S();
        p.drift = mul(norm0(dr), 0.15f);               // 0x6bd1d8
        started = true;
    }
    if (p.fall_e != 0.0f) {
        p.spin = mul(p.spin, 0.96f);                   // 0x6bd408
        p.drift = mul(p.drift, 0.986f);                // 0x6bd41c
        p.world = mat_ypr(p.spin.y, p.spin.x, p.spin.z) * p.world;
        Vec3 pos = add(p.pos(), p.drift);
        // [U12] the out-param is an uninitialised stack slot: on the start frame it still holds
        // the last U() (non-zero); otherwise read as 0 unless the clamp writes it.
        int out = started ? 1 : 0;
        annulus(pos, out);
        set_pos(p.world, pos);
        if (out != 0) {
            const Vec3 n = norm0({pos.x, 0.0f, pos.z});
            float d = p.drift.y * n.y + p.drift.x * n.x + p.drift.z * n.z;
            if (d < 0.0f) {                            // moving into the wall: reflect
                d *= -2.0f;                            // 0x6be098
                p.drift = add(p.drift, mul(n, d));
            }
        }
    }
    if (p.death_delay <= k && p.alpha <= 0.75f) {     // uncollide
        p.f298 |= 0x40;
        p.f60 &= ~0x80u;
        p.entries_struck = false;
    }
    if (k == p.death_delay + 120) {                    // 534 ivent\bs02_soul, no stat
        Boss02Effect& e = fx(0x216, o, p.pos());
        e.power = 0.0f; e.knock = 1.0f;
    }
    if (p.role == kHead && (k == p.death_delay + 120 || k == p.death_delay + 180)) out_->rumble.push_back(4);
    if (p.death_delay + 150 <= k) {
        if (k == p.death_delay + 150) { p.alpha_target = 0.0f; p.alpha_step = 0.00833333377f; }   // 0x3c088889
        if (p.alpha <= 0.0f) {
            p.f298 |= 0x20;
            p.hidden = true;
            return true;
        }
    }
    if (p.role == kHead && p.alpha <= 0.2f) {          // 0x6bd210
        if (!time_attack_) {
            if (1.0f <= player_hp_ && !g_.ended) {
                g_.ended = true;
                out_->end_block = true;                // 0x76a828 = 1, FUN_00461160, FUN_00461120(0)
                out_->gauge_hide = true;               // DAT_006d5fd8 = 0
                out_->award_exp = true;                // FUN_004fa4a0(head+0x94)
            }
            out_->steam_achievement = true;            // FUN_00403cd0 (every frame from here)
        } else {
            f156_ = false;
            out_->flags.push_back({156, 0});
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// FUN_00518a10: the player ring clamp while flag 156 (§9).
void Boss02::player_ring() {
    Vec3 P = player_now_;
    bool changed = false;
    const float r2 = P.x * P.x + P.z * P.z;
    if (84.64f <= r2) {                                // 0x6bdad4
        if (378.302521f < r2) {                        // 0x6bdc84
            const float r = std::sqrt(r2);
            P.x = P.x * (19.45f / r);                  // 0x6bd990
            P.z = P.z * (19.45f / r);
            changed = true;
        }
    } else if (r2 != 0.0f) {
        const float r = std::sqrt(r2);
        P.x = P.x * (9.2f / r);                        // 0x6bd904
        P.z = P.z * (9.2f / r);
        changed = true;
    } else {
        P.x = 0.0f;
        P.z = -9.2f;                                   // 0x6be0d4
        changed = true;
    }
    if (P.y < 0.0f) { P.y = 0.0f; changed = true; }
    if (changed) {
        player_now_ = P;
        out_->player_pos = P;
    }
}

// ---------------------------------------------------------------------------
// FUN_00513490: the leader / segment annulus clamp in x-z (§4 helpers).
void Boss02::annulus(Vec3& p, int& out) const {
    const float x = p.x, z = p.z;
    const float r = std::sqrt(x * x + z * z);
    float inv = 1.0f;
    if (r != 0.0f) inv = 1.0f / r;
    const float rmin = (3.25f - (g_.coil + g_.coil)) + 8.0f;   // 0x6bd6c0 / 0x6bd8e8
    if (r < rmin) {
        p.x = x * rmin * inv;
        p.z = z * rmin * inv;
        out = -1;
    }
    if (16.25f < r) {                                  // 0x6bd970
        p.x = p.x * 16.25f * inv;
        p.z = p.z * 16.25f * inv;
        out = 1;
    }
}

// FUN_00513590: the leader step.
void Boss02::leader_step(int& out) {
    const float cp = cosf_(g_.pitch);
    const Vec3 v{cp * cosf_(g_.yaw), sinf_(g_.pitch), cp * sinf_(g_.yaw)};
    g_.lead = add(g_.lead, mul(v, g_.speed));
    out = 0;
    if (g_.clamp) annulus(g_.lead, out);
    g_.ahead = add(g_.lead, v);
}

// FUN_005151b0: the first vector piece u (|u - self| >= 2) under Q within r.
int Boss02::body_under(int self, const Vec3& q, float r) const {
    for (int u = 0; u < nvec(); ++u) {
        if (std::abs(u - self) < 2) continue;
        const Vec3 pu = obj_[std::size_t(u)].pos();
        const float dx = pu.x - q.x, dz = pu.z - q.z;
        const float dy = (pu.y - q.y) * 0.5f;          // 0x6bd304
        if (dy <= 0.05f && dx * dx + dy * dy + dz * dz < r * r) return u;   // 0x6bd13c
    }
    return -1;
}

// FUN_00515100: shift the trail by the mask G+0x2c (§5.1).
void Boss02::trail_shift() {
    const std::uint8_t m = g_.trail_mask;
    for (int k = kTrail - 1; k >= 1; --k) {
        Boss02TrailRec& a = trail_[std::size_t(k)];
        const Boss02TrailRec& b = trail_[std::size_t(k - 1)];
        if (m & 1) { a.pos.x = b.pos.x; a.pos.z = b.pos.z; a.flags = b.flags; }
        if (m & 2) a.pos.y = b.pos.y;
        if (m & 4) { a.pitch = b.pitch; a.yaw = b.yaw; a.roll = b.roll; }
    }
    if (m & 8) trail_[0] = {g_.lead, g_.pitch, g_.yaw, g_.roll, g_.trail_flags};
}

// FUN_00518220(piece, snap; xmm1 a, xmm2 b): the follow (§5.2).
void Boss02::follow(int o, bool snap, float a, float b) {
    Boss02Piece& p = obj_[std::size_t(o)];
    const Vec3 T = p.role == kHead ? g_.ahead : obj_[std::size_t(p.idx - 1)].rear;
    const int kh = p.kp >> 1;
    const Boss02TrailRec& rh = trail_[std::size_t(kh)];
    float roll = rh.roll;
    Vec3 pos = p.pos();
    if (p.role == kHead || snap) pos = trail_[std::size_t(p.kp)].pos;
    bool posed = false;
    Mat4 M = p.world;
    if (!g_.dormant && (p.role != kHead || g_.hold == 0)) {
        posed = (rh.flags & 2) != 0;
        if (!posed) {                                  // NORMAL: aim at T
            const Vec3 d = T - pos;
            const float l = std::sqrt(d.y * d.y + d.x * d.x + d.z * d.z);
            Vec3 n{0, 0, 0};
            if (l != 0.0f) { const float k = 1.0f / l; n = {d.x * k, d.y * k, d.z * k}; }
            const float yaw = -kHalfPi - atan2f_(n.z, n.x);                       // 0x6be078
            float pitch = kHalfPi - atan2f_(std::sqrt(n.x * n.x + n.z * n.z), n.y);
            if (!p.frozen) {
                if (p.role == kHead) {
                    p.wob += 0.0225f;                  // 0x6bd0f0
                    pitch += sinf_(p.wob) * 0.0523598790f;   // 0x6bd148: +-3 deg
                } else {
                    p.wob += 0.015f;                   // 0x6bd0c0
                    roll += sinf_(p.wob) * 0.0392699093f;    // 0x6bd128: +-2.25 deg
                }
            }
            if (p.smooth != 0) {                       // keep 85 % of the current rotation (0x3f59999a)
                const Quat q = slerp(quat_from(mat_ypr(yaw, pitch, roll)), quat_from(p.world), 0.85f);
                set_rows3(M, mat_from(q));
            } else {
                set_rows3(M, mat_ypr(yaw, pitch, roll));
                if (p.role != kHead) { int dummy = 0; annulus(pos, dummy); }   // segments clamped
            }
            set_pos(M, pos);
        } else {                                       // SCRIPTED POSE (the hoop, §6)
            const float t = g_.pose_blend;
            pos = {(p.pose_pos.x - pos.x) * t + pos.x, (p.pose_pos.y - pos.y) * t + pos.y,
                   (p.pose_pos.z - pos.z) * t + pos.z};
            M = mat_ypr(-kHalfPi - p.pose_yaw, p.pose_pitch, p.pose_roll);
            set_pos(M, pos);
        }
    } else {                                           // DORMANT wobble about the snapshot
        float v;
        if (p.role == kHead) {
            p.wob += 0.0225f;
            v = sinf_(p.wob) * 0.0523598790f;
        } else {
            p.wob += 0.015f;
            const float s = sinf_(p.wob);
            v = s * 0.0523598790f + s * 0.0523598790f;
        }
        M = mat_rot_x(v + 0.0f) * p.snapshot;          // YPR(0, v, 0) x blk+0x220
    }
    p.world = M;
    pos = p.pos();
    p.front = M.transform_point({0, 0, a});
    p.rear = M.transform_point({0, 0, b});
    p.fire = (rh.flags & 1) || posed;                  // the posed bit is 0 in the dormant branch [BYTES 0x518832]
    if (p.role != kHead) {                             // CHAIN PULL: 70 % onto the previous rear point
        const Vec3 dl = mul(T - p.front, 0.7f);        // 0x6bd36c
        p.front = add(p.front, dl);
        p.rear = add(p.rear, dl);
        pos = add(pos, dl);
    }
    if (g_.mode == 1 && g_.cycles == 0 && g_.t < kTicksDescend) {   // the first coil hugs r = 9.6
        const float r = std::sqrt(pos.x * pos.x + pos.z * pos.z);
        if (9.6f < r) {                                // 0x6bd914
            const float dx = (pos.x * 9.6f) / r - pos.x;
            const float dz = (pos.z * 9.6f) / r - pos.z;
            p.front.x += dx; p.front.z += dz;
            pos.x += dx; pos.z += dz;
            p.rear.x += dx; p.rear.z += dz;
        }
    }
    if (p.front.y < kFloor) p.front.y = kFloor;
    if (p.rear.y < kFloor) p.rear.y = kFloor;
    if (pos.y < kFloor) pos.y = kFloor;
    set_pos(p.world, pos);
}

// FUN_00517f30: segment extras (§5.4).
void Boss02::seg_extras(int o) {
    Boss02Piece& p = obj_[std::size_t(o)];
    if (p.role == kBody) {
        if (p.pile != 0) --p.pile;
        if (g_.mode == 1) {
            if (p.stone == 0.0f && R(200) == 0)        // inline xorshift
                debris(o, p.world.transform_point({0, -0.5f, 0}), 0 /* edx = ebp [UNKNOWN] */, 5, 5, true);
        } else if (g_.mode == 3) {                     // pieces pile over each other
            Vec3 pos = p.pos();
            const int u = body_under(p.idx, {pos.x, pos.y - 1.0f, pos.z}, 2.0f);
            if (u < 0) {
                if (kFloor < pos.y) {
                    if (p.pile == 0) pos.y -= 0.025f;  // 0x6bd0f8
                    if (pos.y < kFloor) pos.y = kFloor;
                }
            } else {
                const float lim = (obj_[std::size_t(u)].hp < 1.0f ? 0.0f : 0.5f) + 3.4f;   // 0x6bd6f8
                if (pos.y < lim) pos.y += 0.025f;
                p.pile = 5;
            }
            set_pos(p.world, pos);
        }
    }
    if (p.role != kBody && p.role != kTail) return;
    if (!st_.nightmare) {                              // byte 0x767bae
        if (g_.hs == 2) {
            if (p.clip == kWalk) { p.clip = kRollA; set_clip(o, kRollA, 1); }
        } else if (p.clip == kRollA) {
            p.clip = kRollC;
            set_clip(o, kRollC, 1);
        }
    }
    // The two collision entries: one-way platforms (+0x8c != 0 = excluded) [H-med on intent].
    if (!in_) return;
    for (int k = 0; k < 2; ++k) {
        bool off = false;
        if (!g_.charged) {
            const float by = in_->entry_bone_y[std::size_t(o)][std::size_t(k)];
            const float d = by - p.pos().y;
            if (d < -0.5f) off = true;                 // 0x6be014
            else if (d <= 0.5f && player_now_.y < by + 1.45f) off = true;   // 0x6bd4d0
        }
        p.entry_off[std::size_t(k)] = off;
    }
}

// FUN_005136f0(variant): lay the worm out by simulating it (§5.8).
void Boss02::preroll(int variant) {
    Boss02Piece& h = obj_[0];
    g_.cw = false;
    g_.clamp = true;
    auto layout = [&](float y0) {
        follow(0, false, -1.0f, 1.2f);                 // only for the head's rear point
        trail_[0].pos = {g_.lead.x, y0, g_.lead.z};
        const Vec3 dl = g_.lead - h.rear;
        Vec3 c = trail_[0].pos;
        for (int k = 1; k < kTrail; ++k) {
            c = add(c, dl);
            trail_[std::size_t(k)].pos = c;
        }
        g_.trail_mask = 0x0f;
    };
    auto follows = [&]() {
        follow(0, false, -1.0f, 1.2f);
        for (int u = 1; u < nvec(); ++u) {
            seg_extras(u);
            follow(u, false, -1.2f, 1.2f);
        }
    };
    int out = 0;
    if (variant == 2) {                                // the story statue: coiled on the pillar
        g_.coil = 1.0f;
        const double a = 0.49087387323379517;          // 0x6bd508 (f64)
        g_.lead = {float(std::cos(a)) * 9.25f, 7.15f, float(std::sin(a)) * 9.25f};   // 0x6bd908, 0x40e4cccd
        g_.pitch = 0.0f;
        g_.yaw = 2.06171608f;                          // 0x4003f268
        g_.roll = kHalfPi;
        g_.trail_flags = 1;
        layout(7.15f);
        for (int i = 0; i < 645; ++i) {                // 0x285
            g_.ph1 += 0.037f;                          // 0x6bd118
            g_.ph2 += 0.03f;                           // 0x6bd100
            const float c2 = cosf_(g_.ph2) * 0.025f;
            g_.speed = 0.07f;
            g_.lead.y = g_.lead.y - ((cosf_(g_.ph1) * 0.075f * 1.5f + c2) + c2);
            g_.yaw = atan2f_(g_.lead.z, g_.lead.x) + kHalfPi + kDeg;
            leader_step(out);
            trail_shift();
            follows();
        }
    } else if (variant == 0) {                         // boss rush, select 1
        g_.coil = 1.0f;
        const double a = -0.9817476272583008;          // 0x6be090 (f64)
        g_.lead = {float(std::cos(a)) * 9.25f, kFloor, float(std::sin(a)) * 9.25f};
        g_.pitch = 0.0f;
        g_.yaw = 0.589048624f;                         // 0x3f16cbe6
        g_.roll = kHalfPi;
        g_.trail_flags = 1;
        layout(3.0f);                                  // 0x40400000
        for (int i = 0; i < 600; ++i) {
            g_.lead.y += 0.011f;                       // 0x6bd08c
            g_.speed = 0.07f;
            g_.yaw = atan2f_(g_.lead.z, g_.lead.x) + kHalfPi + kDeg;
            leader_step(out);
            trail_shift();
            follows();
        }
    } else if (variant == 1) {                         // boss rush, selects 3 / 4: on the floor
        for (Boss02TrailRec& r : trail_) r.pos.y = kFloor;
        const double a = 4.908738613128662;            // 0x6bd5e8 (f64)
        g_.lead = {float(std::cos(a)) * 11.0f, kFloor, float(std::sin(a)) * 11.0f};   // 0x6bd938
        g_.pitch = 0.0f;
        g_.yaw = 6.47953367f;                          // 0x40cf585a
        g_.roll = 0.0f;
        g_.trail_flags = 0;
        g_.trail_mask = 0x0f;
        for (int i = 0; i < 600; ++i) {
            g_.yaw += 0.009f;                          // 0x6bd074
            g_.speed = 0.13f;                          // 0x3e051eb8
            leader_step(out);
            trail_shift();
        }
    }
    // Snap every piece onto its trail record; the loop tests the HEAD's role, so every piece
    // gets (a, b) = (-1.0, 1.2) [BYTES 0x513d21..0x513d4a].
    for (int u = 0; u < nvec(); ++u) follow(u, true, -1.0f, 1.2f);
    // 0x76c214 = 2: the HP-bar strip (Boss02Gauge::strip).
}

// FUN_0051b850: an arc spawn. Only its RNG draws and life matter outside the renderer:
// 2 + 5 x 6 + 9 = 41 draws for the head, 39 for a segment; life 24 [EXPORT 0051b850.c].
void Boss02::arc_spawn(int o, int slot) {
    Boss02Piece& p = obj_[std::size_t(o)];
    p.arc_life[std::size_t(slot)] = 24;
    const int n = p.role == kHead ? 41 : 39;
    for (int i = 0; i < n; ++i) draw();                // the jittered points [not modelled: geometry]
}

// FUN_0051c8c0(seg, &v, 0): the plasma ball (kind 0x24, §7.1).
void Boss02::plasma_spawn(int o, const Vec3& v) {
    const Boss02Piece& p = obj_[std::size_t(o)];
    const Vec3 P = p.world.transform_point(v);
    const float l = std::sqrt(P.x * P.x + 0.0f + P.z * P.z);
    Vec3 u{0, 0, 0};
    if (l != 0.0f) u = {P.x * (1.0f / l), 0.0f, P.z * (1.0f / l)};
    const float s = U() * 8.0f + 1.0f;                 // 0x6bd8e8
    const Vec3 pos = p.pos();
    Boss02Bullet b;
    b.kind = 0x24;
    b.effect = 0x15c;                                  // 348 bs_02_plasma03b
    b.carrier = o;
    b.proxy_owner = true;
    b.pos = P;
    b.dir = u;
    b.f[1] = 0.01f;                                    // 0x3c23d70a
    b.f[4] = 2.0f;                                     // knock
    b.local = v;
    b.target = {pos.x + u.x * s, 10.0f, pos.z + u.z * s};
    out_->bullets.push_back(b);
}

// FUN_0051c8c0(seg, &p, 1): the poison ball (kind 0x23, §7.2).
void Boss02::poison_spawn(int o, const Vec3& p) {
    Boss02Bullet b;
    b.kind = 0x23;
    b.effect = 0x157;                                  // 343 bs_02_doku_ball
    b.carrier = o;
    b.pos = p;
    b.dir = {0, 1.0f, 0};
    b.f[0] = 0.0166666675f;                            // 0x3c888889
    b.f[2] = 0.0163333323f;                            // 0x3c85cd7c
    b.f[3] = 10.0f;                                    // power
    b.f[4] = 2.0f;                                     // knock
    b.f[9] = 1.0f;
    b.a4 = st_.ball_a4;
    out_->bullets.push_back(b);
    ++balls_spawned_;
}

// ---------------------------------------------------------------------------
// FUN_00516740: the head's group driver (§4).
void Boss02::driver() {
    Boss02Piece& h = obj_[0];
    g_.trail_mask = 0x0f;
    g_.trail_flags = 0;
    g_.fire_ok = false;
    auto all_struckable = [&]() {
        for (int u = 0; u < nvec(); ++u) {
            obj_[std::size_t(u)].f298 |= 8;
            const int t = obj_[std::size_t(u)].twin;
            if (t >= 0) obj_[std::size_t(t)].f298 |= 8;
        }
    };
    auto set_mode = [&](int m) { g_.mode = m; g_.t = 0; g_.t2 = 0; };   // FUN_00514ff0
    if (in_->rush_room) {
        if (in_->rush_select != 0) {                   // boss rush: flag 165 picks the entry state
            out_->flags.push_back({166, 1});
            wake_ = 1;
            out_->flags.push_back({156, 1});
            f156_ = true;
            h.frozen = false;
            all_struckable();
            bool latch = true;
            switch (in_->rush_select) {
                case 1: set_mode(0); preroll(0); h.frozen = false; break;
                case 2: set_mode(1); break;
                case 3: set_mode(3); preroll(1); h.frozen = false; break;
                case 4: set_mode(3); preroll(1); g_.sub = 2; g_.roll_wait = 0; h.frozen = false; break;
                default: latch = false; break;
            }
            if (latch) { g_.awake = true; g_.dormant = false; g_.started = true; }
            out_->flags.push_back({165, 0});
        }
    } else if (!g_.started && f156_) {                 // the fight start (Battle_* sets 156)
        g_.started = true;
        h.frozen = false;
        set_mode(1);
        g_.awake = true;
        g_.dormant = false;
        all_struckable();
    }
    if (g_.mode == 4) { hoop(); return; }              // nothing below, not even G+0x18++
    if (!g_.awake && 0 < wake_) {                      // the intro wake (flag 166)
        g_.awake = true;
        set_mode(0);
        h.frozen = false;
        g_.dormant = true;
        for (int u = 0; u < nvec(); ++u) obj_[std::size_t(u)].snapshot = obj_[std::size_t(u)].world;
    }
    g_.hp_at = h.pos();
    g_.mouth = mat_rot_y(-0.0872664601f) * mat_translate({0, 0.5f, -3.75f}) * h.world;   // 0xbdb2b8c2
    if (g_.freeze) --g_.freeze;

    // The mouth laser (§4.2) [BYTES 0x516ba9..0x516eb0].
    if (g_.laser == 0) {
        if (g_.laser_cd != 0 && --g_.laser_cd != 0) g_.laser_fx = false;
    } else {
        const int n = --g_.laser;
        if (n == 40) {
            Boss02Effect& e = fx(0x15f, 0, h.pos());   // 351 bs_02_lazer [if the pool has room]
            e.stat = Boss02Effect::kPieceStr;
            e.str = st_.laser_str;
            e.attach = Boss02Effect::kMouth;
            h.glow_t = 50;
            out_->rumble.push_back(3);
        }
        if (39 < n) {
            for (int i = 0; i < 2; ++i)
                if (R(100) < 12 && h.arc_life[std::size_t(i)] == 0) arc_spawn(0, i);
        }
    }

    if (g_.mode == 1) {
        mode1();
    } else {
        if (0.0f < g_.coil) {
            g_.coil -= 0.02f;                          // 0x6bd0ec
            if (g_.coil < 0.0f) g_.coil = 0.0f;
        }
        if (g_.mode == 0) {                            // the waking statue (§4.4)
            const float rollT = g_.cw ? -kHalfPi : kHalfPi;
            const float off = g_.cw ? -kOff : kOff;
            g_.trail_mask = 0;
            g_.speed = 0.0f;
            g_.yaw = atan2f_(g_.lead.z, g_.lead.x) + off;
            g_.roll = rollT;
            if (!h.frozen) {
                g_.ph1 += 0.065f;                      // 0x6bd158
                g_.ph2 += 0.04f;                       // 0x6bd12c
            }
            loop_state(0);
        } else if (g_.mode == 2) {                     // [U6] no writer
            g_.trail_mask = 0;
            g_.speed = 0.0f;
            loop_state(0);
        } else if (g_.mode == 3) {
            if (!mode3_body()) return;                 // it entered mode 4
        }
    }
    ++g_.t;
}

// Mode 1: coil around the pillar, climb, slide down (§4.5) [BYTES 0x516ec7..0x5172e2].
void Boss02::mode1() {
    if (g_.coil < 1.0f) {
        g_.coil += 0.02f;
        if (1.0f < g_.coil) g_.coil = 1.0f;
    }
    const float old_yaw = g_.yaw;
    g_.trail_flags |= 1;
    float off = kOff, rollT = kHalfPi;
    if (g_.cw) { rollT = -kHalfPi; off = -kOff; }
    g_.fire_ok = true;
    g_.speed = 0.07f;                                  // 0x3d8f5c29
    const float base = atan2f_(g_.lead.z, g_.lead.x);
    g_.ph2 += 0.04f;
    g_.ph1 += 0.065f;
    g_.yaw = base + off;                               // the tangent + 1.5 deg inward
    float y = g_.lead.y;
    if (3.15f < y) {                                   // 0x6bd6b0: undulate
        y = y - (cosf_(g_.ph1) * 0.075f + cosf_(g_.ph2) * 0.025f);
        g_.lead.y = y;
    }
    bool descend = false;
    if (g_.t < kTicksDescend + 1) {
        if (g_.sub == -1) descend = true;
        else if (g_.sub == 1) {                        // CLIMB
            g_.lead.y = y + 0.04f;
            const float top = (g_.cycles & 1) ? 6.0f : 8.0f;   // 0x6bd880 / 0x6bd8e8
            if (top + kFloor < y + 0.04f) { g_.t = 0; g_.sub = 0; }
            g_.yaw = turn_toward(0.05f, old_yaw, g_.yaw);       // 0x6bd13c
            ++g_.t2;
            g_.roll = rollT * smooth(float(g_.t2) / 90.0f) + 0.0f;
        } else {
            g_.t2 = 0;
            g_.roll = rollT;
        }
    } else {
        g_.sub = -1;
        descend = true;
    }
    if (descend) {                                     // slide down and right itself
        if (y < kFloor) {
            if (y < 5.65f) ++g_.t2;                    // 0x6bd878
        } else {
            y -= 0.04f;
            g_.lead.y = y;
            if (kFloor <= y) { if (y < 5.65f) ++g_.t2; }
            else { g_.lead.y = kFloor; ++g_.t2; }
        }
        g_.roll = (0.0f - rollT) * smooth(float(g_.t2) / 90.0f) + rollT;
        if (90 < g_.t2) {
            g_.circle = st_.circle_after1;
            g_.climb_cd = 0;
            g_.sub = 0;
            if (g_.rolls_left) --g_.rolls_left;
            ++g_.cycles;
            g_.mode = 3; g_.t = 0; g_.t2 = 0;
        }
    }
    g_.clamp = true;
    int out = 0;
    leader_step(out);
    if (out < 0) g_.bump = 10;
    if (g_.speed < 0.001f) g_.trail_mask &= 0xf8;      // 0x6bcff0
    loop_state(1);
}

// Mode 3: the hunt (§4.6) [BYTES 0x51742a..0x517ea9]. false = it switched to mode 4 (which
// already ran this frame) and the driver returns.
bool Boss02::mode3_body() {
    Boss02Piece& h = obj_[0];
    const Vec3 P = player();
    const Vec3 Hp = h.pos();
    const Vec3 Tg = g_.sub == 1 ? Vec3{0, 0, 0} : P;
    const Vec3 d = Tg - g_.ahead;
    const float tgt_yaw = atan2f_(d.z, d.x);
    const float err = wrap_d(norm_2pi(tgt_yaw) - norm_2pi(g_.yaw));
    // The pillar (r 7) between the head and the player? [0x6bda5c]
    bool blocked = false;
    {
        const float px = P.x - Hp.x, pz = P.z - Hp.z;
        const float ox = 0.0f - Hp.x, oz = 0.0f - Hp.z;
        const float lp = pz * pz + px * px;
        float dist = oz * oz + ox * ox;
        if (dist < lp) {
            const float t = oz * pz + ox * px;
            if (0.0f <= t) {
                if (t <= lp) dist = dist - (oz * (t / lp) * pz + ox * (t / lp) * px);
                else {
                    const float qx = 0.0f - P.x, qz = 0.0f - P.z;
                    dist = qz * qz + qx * qx;
                }
            }
            blocked = dist <= 49.0f;
        }
    }
    g_.in_range = false;
    float range = 2.5f;                                // 0x6bd624
    const float ae = std::fabs(err);
    if (0.523598790f < ae) { if (ae <= 1.04719758f && !blocked) range = 5.5f; }   // 0x6bd314 / 0x6bd454
    else if (!blocked) range = 15.5f;                  // 0x6bd968
    if (d.x * d.x + d.y * d.y + d.z * d.z < range * range) g_.in_range = true;
    if (g_.circle) --g_.circle;
    if (g_.climb_cd) --g_.climb_cd;
    if (1 < g_.phase && g_.rolls_left == 0 && g_.sub == 0) {   // ROLL armed (pool <= 50 %)
        g_.sub = 2;
        g_.roll_wait = 0;
        g_.rolls_left = 2;
    }
    int s = g_.sub;
    if (player_hp_ < 1.0f) {                           // the player died: go home
        if (s == 0) { g_.sub = 1; s = 1; }
        g_.in_range = false;
    }
    bool around = blocked || s == 2;
    bool home = false;
    if (kTicksHome < g_.t) {                           // 27.3 s in mode 3: go climb
        g_.sub = 1;
        g_.coil = 1.0f;
        home = true;
    } else if (s == 1) {
        home = true;
    }
    if (home) { g_.roll_wait = 0; around = false; g_.in_range = false; s = 1; }
    bool chase;
    if (g_.bump == 0) {
        if (around) { ++g_.around_n; chase = false; }
        else {
            if (g_.around_n != 0) g_.around_n = std::max(g_.around_n - 10, 0);
            g_.roll_wait = 0;
            chase = true;
        }
    } else {
        --g_.bump;
        chase = !around;
    }
    if (chase) {                                       // (c1)
        float tgt = tgt_yaw;
        if (g_.circle) {                               // circle the pillar for a while
            tgt = atan2f_(Hp.z, Hp.x) + kHalfPi;
            if (kHalfPi < std::fabs(angle_diff(tgt, g_.yaw))) tgt += kPi;
        }
        if (g_.freeze == 0) g_.yaw = turn_toward(0.015f, g_.yaw, tgt);   // 0x6bd0c0
        g_.speed = g_.in_range ? 0.115f : 0.09f;       // 0x6bd1a8 / 0x6bd190
        if (g_.in_range && h.clip == kWalk) {
            if (g_.laser == 0 && R(100) < 7) {         // the BITE lunge (inline xorshift)
                h.clip = kAttack;
                set_clip(0, kAttack, 1);
            }
            if (h.clip == kWalk && g_.laser_cd == 0) { // the LASER start
                se2d(213);
                g_.laser_cd = st_.laser_cd;
                g_.laser = 90;
                g_.glow_laser = 140;
                g_.freeze = 40;
                Boss02Effect& e = fx(0x161, 0, {0, 0, 0});   // 353 bs_02_plasma03c, head stat block
                e.stat = Boss02Effect::kPiece;
                e.attach = Boss02Effect::kMouth;
                e.slot = Boss02Effect::kLaserCharge;
                g_.laser_fx = true;
            }
        }
    } else {                                           // (c2) around the pillar / roll pending
        const int half = st_.circle_rev / 2;
        if (s == 2 && half < g_.roll_wait) {
            g_.roll_wait = half + 1;
            if (Hp.y < 1.75f) {                        // 0x6bd510: the roll starts this frame
                g_.sub = 0;
                g_.hs = 0;
                g_.mode = 4; g_.t = 0; g_.t2 = 0;
                hoop();
                return false;
            }
        }
        ++g_.roll_wait;
        float tgt;
        if (st_.circle_rev < g_.roll_wait) {           // reverse direction
            tgt = g_.circ_tgt + kPi;
            if (std::fabs(angle_diff(g_.yaw, tgt)) < kDeg) g_.roll_wait = 0;
        } else {
            tgt = atan2f_(Hp.z, Hp.x) + kHalfPi;       // the pillar tangent nearest the heading
            if (kHalfPi < std::fabs(angle_diff(tgt, g_.yaw))) tgt += kPi;
            g_.circ_tgt = tgt;
        }
        g_.yaw = turn_toward(0.016f, g_.yaw, tgt);     // 0x6bd0d0
        g_.speed = 0.1f;                               // 0x3dcccccd
    }
    // (d) climbing over its own body.
    {
        const Vec3 n = norm0(h.front - g_.lead);
        const Vec3 Q = add(g_.lead, mul(n, 1.5f));     // 0x6bd4d4
        const int u = body_under(0, Q, 2.75f);         // 0x6bd654
        float y = g_.lead.y;
        bool check = true;
        if (u < 0) {
            if (kFloor < y) {
                y -= 0.075f;
                g_.lead.y = y;
                if (y < kFloor) { g_.lead.y = kFloor; check = false; }
            }
        } else {
            const float lim = (obj_[std::size_t(u)].hp < 1.0f ? 0.0f : 0.5f) + 3.4f;
            if (y < lim) { y += 0.125f; g_.lead.y = y; }   // 0x6bd1b8
        }
        if (check && 3.35f < y && !g_.circle && !g_.climb_cd) {   // 0x6bd6e4
            g_.climb_cd = 180;
            g_.circle = 120;
        }
    }
    // (e) move.
    g_.clamp = true;
    int out = 0;
    leader_step(out);
    if (out < 0) {
        g_.bump = 10;
        if (g_.sub == 1) { g_.mode = 1; g_.t = 0; g_.t2 = 0; }   // reached the pillar: climb
    }
    {
        const float l = std::sqrt(Hp.x * Hp.x + Hp.z * Hp.z);
        float hx = Hp.x, hz = Hp.z;
        if (l != 0.0f) { hx /= l; hz /= l; }           // D3DXVec2Normalize
        g_.cw = wrap_d(norm_2pi(g_.yaw) - norm_2pi(atan2f_(hz, hx))) < 0.0f;
    }
    if (g_.speed < 0.001f) g_.trail_mask &= 0xf8;
    loop_state(1);
    return true;
}

// ---------------------------------------------------------------------------
// FUN_00515250: mode 4, the rolling hoop (§6).
void Boss02::hoop() {
    Boss02Piece& h = obj_[0];
    if (g_.hs < 2 && g_.hs == 0) {                     // S 0 -> 1 (the same frame)
        g_.hs = 1;
        g_.theta = 0.0f;
        g_.wp = 0.02f;                                 // 0x3ca3d70a
        g_.wy = 0.0f;
        g_.psi = atan2f_(g_.hp_at.z, g_.hp_at.x) + kHalfPi;
        const float r = std::sqrt(g_.hp_at.x * g_.hp_at.x + g_.hp_at.z * g_.hp_at.z);
        g_.pose_blend = 0.0f;
        g_.flip = g_.cw;
        g_.R = r;
        g_.r_from = r;
        g_.r_to = r;
        g_.se220 = false;
        g_.braking = false;
        g_.smoke_acc = 0.0f;
        g_.smoke_last = 0.0f;
        g_.in_range = false;
    }
    // Placement (every frame, all S) §6.2.
    float phi = g_.theta;
    const float psim = g_.psi - kHalfPi;
    float delta = k2Pi / (float(nvec()) + 1.6f);       // 0x6bd4ec
    if (g_.flip) { delta = delta * -1.0f; phi = -phi; }
    phi = phi - kHalfPi;
    const float old_cx = g_.hp_at.x, old_cz = g_.hp_at.z;
    g_.hp_at.x = cosf_(psim) * g_.R;
    g_.hp_at.z = sinf_(psim) * g_.R;
    const Mat4 M = mat_rot_y(-g_.psi);
    g_.pose_blend = 1.0f;
    // The timeline §6.3.
    const int n = g_.hn;
    bool roll01 = false, dust = false, reaim = false, off = false, spindown = false;
    int dir = 0;
    float fxrot = kHalfPi;
    const int bb5c = bb5c_;                            // g_flags[144]
    if (n < 840) {
        roll01 = n == 120;
        dust = unsigned(n - 75) < 586u;
        if (n == 150) { g_.charged = true; reaim = true; }
        if (-1 < bb5c && n == 390) reaim = true;
        if (1 < bb5c && n == 270) reaim = true;
        off = n == 570;
        dir = 1;
        spindown = 570 < n;
        fxrot = -kHalfPi;
    } else if (n < 1650) {
        roll01 = n == 960;
        dir = -1;
        dust = unsigned(n - 915) < 556u;
        if (n == 990) { g_.charged = true; reaim = true; }
        if (-1 < bb5c && n == 1230) reaim = true;
        if (1 < bb5c && n == 1110) reaim = true;
        off = n == 1380;
        spindown = 1380 < n;
        fxrot = kHalfPi;
    } else if (n < 2460) {
        roll01 = n == 1770;
        dust = unsigned(n - 1725) < 556u;
        if (n == 1800) { g_.charged = true; reaim = true; }
        if (-1 < bb5c && n == 2040) reaim = true;
        if (1 < bb5c && n == 1920) reaim = true;
        if (g_.rolls == 0) {                           // the first roll gets a third cycle
            off = n == 2190;
            spindown = 2190 < n;
            dir = 1;
            fxrot = -kHalfPi;
        }
    }
    g_.hoop = mat_rot_x(fxrot) * M;
    set_pos(g_.hoop, {g_.hp_at.x, 7.25f, g_.hp_at.z});
    for (int i = 0; i < nvec(); ++i) {
        Boss02Piece& p = obj_[std::size_t(i)];
        const Vec3 v = M.transform_point({cosf_(phi) * 7.25f, sinf_(phi) * 7.25f, 0.0f});   // 0x6bd8c8
        p.pose_pos = {g_.hp_at.x + v.x, v.y + 8.9f, g_.hp_at.z + v.z};   // 0x6bd8fc
        if (!g_.flip) { p.pose_pitch = phi + kHalfPi; p.pose_roll = 0.0f; }
        else { p.pose_pitch = phi - kHalfPi; p.pose_roll = kPi; }
        phi = phi - delta;
        p.pose_yaw = g_.psi;
    }
    g_.lead = h.pose_pos;
    g_.pitch = h.pose_pitch;
    g_.yaw = h.pose_yaw;
    g_.roll = h.pose_roll;
    int lout = 0;
    leader_step(lout);
    set_pos(h.world, g_.lead);
    g_.trail_flags |= 2;                               // the scripted-pose flag rides down the trail
    if (roll01) {                                      // 349 bs_02_rolling01 + SE 218
        Boss02Effect& e = fx(0x15d, 0, {0, 0, 0});
        e.attach = Boss02Effect::kHoop;
        e.slot = Boss02Effect::kRolling;
        g_.rolling_fx = true;
        se2d(218);
    }
    if (off && g_.rolling_fx) { g_.rolling_fx = false; g_.charged = false; }
    if (reaim && g_.r_t == 0.0f) {                     // only the radius steers
        g_.r_t = 1.0f;
        g_.r_from = g_.R;
        const Vec3 P = player();
        const float r = std::sqrt(P.x * P.x + P.z * P.z);
        g_.r_to = r;
        if (9.75f < r) { if (16.75f <= r) g_.r_to = 16.75f; }   // 0x6bd91c / 0x6bd97c
        else g_.r_to = 9.75f;
    }
    if (dust) {                                        // the smoke puffs (damaging)
        const float dx = g_.hp_at.x - old_cx, dz = g_.hp_at.z - old_cz;
        const float acc = std::sqrt(dz * dz + dx * dx) * 0.5f + g_.smoke_acc;
        g_.smoke_acc = acc;
        if (1.0f <= acc - g_.smoke_last) {
            g_.smoke_last = std::floor(acc);
            const float f = std::fabs(g_.wp) * 1.65999997f + 1.0f;   // 0x6bd4fc
            Boss02Effect& e = fx(0x15e, 0, {g_.hp_at.x * f, 0.15f, g_.hp_at.z * f});   // 350 bs_02_rollingsmoke
            e.stat = Boss02Effect::kPieceStr;
            e.str = st_.smoke_str;
            e.power = 1.0f; e.knock = 1.5f; e.rtype = 1; e.status = 0; e.cls = -1;
        }
    }
    int S = g_.hs;
    if (S == 1) {
        if (5.49778748f < std::fabs(g_.theta)) {       // 0x6bd870: 7pi/4 of wind-up
            g_.pose_blend = 1.0f;
            g_.hs = 2; S = 2;
            g_.hn = 0;
            loop_state(2);
        }
    } else if (S == 2) {
        if (0 < dir) {
            float tp = 0.06f, ty = -0.04f, k = 0.005f;   // 0x6bd150 / 0x6bdfc4 / 0x6bd044
            if (spindown) { tp = 0.0f; ty = 0.0f; k = 0.0125f; }
            g_.wp = (tp - g_.wp) * k + g_.wp;
            g_.wy = (ty - g_.wy) * k + g_.wy;
        } else if (dir < 0) {
            float tp = -0.06f, ty = 0.04f, k = 0.005f;
            if (spindown) { tp = 0.0f; ty = 0.0f; k = 0.0125f; }
            g_.wp = (tp - g_.wp) * k + g_.wp;
            g_.wy = (ty - g_.wy) * k + g_.wy;
        } else {                                       // brake
            g_.braking = true;
            const float sp = 0.0f <= g_.wp ? 1.0f : -1.0f, sy = 0.0f <= g_.wy ? 1.0f : -1.0f;
            g_.wp = (sp * 0.012f - g_.wp) * 0.0125f + g_.wp;   // 0x6bd0a0 / 0x6bd0a8
            g_.wy = (sy * 0.008f - g_.wy) * 0.0125f + g_.wy;   // 0x6bd06c
        }
    } else if (S == 3) {                               // the exit to mode 3 §6.4
        g_.r_t = 0.0f;
        g_.charged = false;
        g_.sub = 0;
        g_.roll_wait = 0;
        h.pose_pitch = h.pose_yaw = h.pose_roll = 0.0f;
        g_.pitch = 0.0f;
        g_.roll = 0.0f;
        g_.circle = 180;
        g_.climb_cd = 0;
        float y = atan2f_(g_.lead.z, g_.lead.x) + kHalfPi;
        if (g_.flip) y = y + kPi;
        g_.yaw = y;
        g_.trail_mask |= 8;
        g_.speed = 0.07f;
        g_.lead = h.world.transform_point({0, 0, 1.0f});
        int o2 = 0;
        leader_step(o2);
        h.smooth = 40;
        g_.hold = 22;
        h.snapshot = h.world;
        g_.g184 = 4;
        const float tk[6] = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};   // 0x6bd210 / 2d4 / 334 / 3a8
        for (int k = 0; k < 6; ++k) {
            Boss02TrailRec& r = trail_[std::size_t(k)];
            r.pos = {(g_.lead.x - g_.ahead.x) * tk[k] + g_.ahead.x, (g_.lead.y - g_.ahead.y) * tk[k] + g_.ahead.y,
                     (g_.lead.z - g_.ahead.z) * tk[k] + g_.ahead.z};
            r.pitch = g_.pitch; r.yaw = g_.yaw; r.roll = g_.roll;
        }
        ++g_.rolls;
        g_.rolling_fx = false;
        g_.mode = 3; g_.t = 0; g_.t2 = 0;
        return;
    }
    // Integration (S 1, 2) [BYTES LAB_00516348].
    if (0.06f < g_.wp) g_.wp = 0.06f;
    else if (g_.wp < -0.06f) g_.wp = -0.06f;
    if (0.04f < g_.wy) g_.wy = 0.04f;
    else if (g_.wy < -0.04f) g_.wy = -0.04f;
    float kk = (g_.R - 9.75f) / 7.0f;                  // 0x6bd8b8
    kk = kk < 0.0f ? 0.0f : (1.0f < kk ? 1.0f : kk);
    kk = kk * 0.5f + 0.75f;
    g_.theta = kk * g_.wp + g_.theta;
    g_.psi = (g_.flip ? -1.0f : 1.0f) * kk * g_.wy + g_.psi;
    if (0.0f < g_.r_t) {
        float rt = g_.r_t - st_.hoop_rate;
        if (rt < 0.0f) rt = 0.0f;
        g_.r_t = rt;
        g_.R = (g_.r_to - g_.r_from) * smooth(1.0f - rt) + g_.r_from;
    }
    if (S == 2) ++g_.hn;
    if (g_.braking) {                                  // the end test
        if (std::fabs(wrap_d(norm_2pi(0.0f) - norm_2pi(g_.theta))) < 0.0261799395f) {   // 0x6bd0fc
            g_.theta = 0.0f;
            g_.hs = 3;
            g_.wp = 0.0f;
            g_.wy = 0.0f;
        }
    }
    // SE 220 + Rumble(5) when the hoop's inward direction nears the camera's side: needs the
    // camera vector at 0x7b33f4 [not modelled]; only its 60-frame pause of the dust rumble
    // (G+0x188) would follow.
    if (dust) {
        if (g_.rumble_cd == 0) { g_.rumble_cd = 12; out_->rumble.push_back(0); return; }   // FUN_005d3500(0) [BYTES 0x5166fa]
        --g_.rumble_cd;
    }
}

// ---------------------------------------------------------------------------
// The callbacks (§3.6).
bool Boss02::accept_hit(int o, const Vec3& contact, std::uint32_t now_ms, Boss02Output& fb,
                        const std::function<void(const Boss02Cosmetic&)>& cosmetic, bool fx_option) {
    const Boss02Piece& p = obj_[std::size_t(o)];
    if (p.frozen) return false;                        // the dormant statue cannot be hit
    if (!p.entries_struck) return false;
    const std::uint32_t dt = fb_time_ > now_ms ? fb_time_ - now_ms : now_ms - fb_time_;
    if (dt < 151) return true;                         // the 150 ms feedback throttle (real time)
    if (p.def < 499.0f) {                              // 0x6bdcf0: soft pieces throw debris + SE 92
        if (cosmetic) {
            Boss02Cosmetic c;
            c.kind = Boss02Cosmetic::kDebris;
            c.object = o; c.pos = contact; c.edx = 1; c.s1 = 0; c.s2 = 3; c.flag = false;
            cosmetic(c);
        }
        fb.sounds.push_back({Boss02Sound::k3D, 92, contact, 0});
    }
    Boss02Effect e;                                    // 344 bs_02_taieki (body fluid), no stat
    e.id = 0x158;
    e.object = o;
    e.pos = contact;
    fb.effects.push_back(e);
    bool sparks = false;
    if (p.role == kHead) sparks = p.def < 499.0f;
    else if (p.role == kTwin) sparks = p.owner >= 0 && obj_[std::size_t(p.owner)].cracked;
    if (sparks && fx_option && cosmetic) {             // FUN_0058a5f0(2, 0x18, 1.95, 2.7, contact, 0.65)
        Boss02Cosmetic c;
        c.kind = Boss02Cosmetic::kScreenDebris;
        c.object = o;
        c.pos = contact;
        cosmetic(c);
    }
    fb_time_ = now_ms;
    return true;
}

void Boss02::add_damage(int o, float dmg) { obj_[std::size_t(o)].hp -= dmg; }

void Boss02::add_part_damage(int o, float dmg, float dmg_hp) {
    Boss02Part& pt = obj_[std::size_t(o)].part;
    pt.dmg += dmg;
    pt.hp -= dmg_hp;
}

bool Boss02::part_hittable(int o) const {
    const Boss02Piece& p = obj_[std::size_t(o)];
    return p.has_part && p.part.alive && p.part.hp > 1.0f && p.part.radius > 0.0f;
}

bool Boss02::mesh_hittable(int o) const {
    const Boss02Piece& p = obj_[std::size_t(o)];
    return p.exists && (p.f298 & 0x88u) == 8u && !p.frozen && (p.f60 & 0x80u) && p.entries_struck;
}

Boss02Touch Boss02::touch(int o, float player_hp) const {
    const Boss02Piece& p = obj_[std::size_t(o)];
    Boss02Touch t;
    if (player_hp < 1.0f || !p.hurts) return t;
    if (p.f298 & 0x880u) return t;                     // the dispatcher FUN_0045a4a0's gate
    t.damage = true;
    t.str = p.str;
    return t;
}

bool Boss02::touch_applied(int o, float player_hp_after) {
    if (g_.charged && 1.0f <= player_hp_after) return true;   // section 9 forced reaction [U2]
    if (obj_[std::size_t(o)].role == kHead) g_.glow_bump = 12;
    return false;
}

}  // namespace cleria
