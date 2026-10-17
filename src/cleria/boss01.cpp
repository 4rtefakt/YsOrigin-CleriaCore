// Velagunder (boss01) — see boss01.hpp and build/boss_re/VELAGUNDER.md.
#include "cleria/boss01.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cleria {

namespace boss01 {

const ClipInfo kClipTable[kClips] = {
    {"WAIT", 220, 339, true},          {"RUN_R", 20, 39, true},
    {"RUN_R_end", 50, 69, false},      {"RUN_L", 20, 39, true},
    {"RUN_L_end", 50, 69, false},      {"ATTACK_01_R", 400, 455, false},
    {"ATTACK_01_L", 400, 455, false},  {"ATTACK_04_1", 220, 249, false},
    {"ATTACK_04_2", 250, 259, true},   {"ATTACK_04_3", 260, 269, false},
    {"ATTACK_05", 220, 279, true},     {"ATTACK_06", 220, 269, true},
    {"ATTACK_07_1", 350, 376, false},  {"ATTACK_07_2", 380, 389, true},
    {"ATTACK_07_3", 390, 424, false},  {"ACTION_01", 460, 560, false},
    {"PIYOPIYO_1", 220, 279, false},   {"PIYOPIYO_2", 380, 479, true},
    {"PIYOPIYO_3", 580, 614, false},   {"ATTACK_10_1", 220, 249, false},
    {"ATTACK_10_2", 250, 259, true},   {"ATTACK_10_3", 260, 304, false},
};

const char* const kBoneNames[kBones] = {
    "Null(227)", "Null(228)", "Null(229)", "Null(230)", "Null(231)", "Null(232)",
    "Null(233)", "Null(234)", "Null(235)", "Null(236)", "Null(237)", "Null(238)",
    "Null(239)", "Null(240)", "Null(241)", "Null(242)", "Null(190)", "Null(197)",
    "Null(213)", "Null(215)", "Null(217)", "Null(219)", "Null(221)", "Null(223)",
};

const int kPartBone[kParts] = {10, 14, 15, 16, 17, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                               18, 19, 20, 21, 22, 23};

}  // namespace boss01

using namespace boss01;

namespace {

// The engine's float constants (read with exe_peek --f32).
constexpr float kPi = 3.14159274f;        // 0x6bd6ac
constexpr float kHalfPi = 1.57079637f;    // 0x6bd4e4
constexpr float k3HalfPi = 4.71238899f;   // 0x6bd858
constexpr float kDeg = 0.0174532924f;     // 0x6bd0dc
constexpr float k60 = 1.04719758f;        // 0x6bd454
constexpr float k50 = 0.872664630f;       // 0x6bd3c4
constexpr float k65 = 1.13446403f;        // 0x6bd470
constexpr float k95 = 1.65806282f;        // 0x6bd4f8
constexpr float k35 = 0.610865235f;       // 0x6bd338
constexpr float k145 = 2.53072739f;       // 0x6bd62c
constexpr float kQuarterPi = 0.785398185f;  // 0x6bd3a0
constexpr float kDef999 = 999.0f;         // 0x4479c000

constexpr int kCastPlayer = 0x0d, kCastM210 = 0x19, kCastM890 = 0xaf;

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float len(const Vec3& v) { return std::sqrt(v.y * v.y + v.x * v.x + v.z * v.z); }
// FUN_0042cc50-style normalise: multiply by the reciprocal; zero stays zero.
Vec3 norm0(const Vec3& v) {
    const float l = len(v);
    if (l == 0.0f) return {0, 0, 0};
    const float k = 1.0f / l;
    return {v.x * k, v.y * k, v.z * k};
}
// p * RotationY(a), D3DX row-vector convention: (x cos + z sin, y, -x sin + z cos).
Vec3 rot_y(const Vec3& p, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}
// FUN_00512f60 / the inline FUN_00444660 wrap of state 1: into [-pi, pi).
float wrap_pi(float x) {
    if (x >= kPi) return float(std::fmod(double(x + kPi), double(6.28318548f))) - kPi;
    if (x < -kPi) return float(std::fmod(double(x - kPi), double(6.28318548f))) + kPi;
    return x;
}
// FUN_005ef010: into [0, 2pi] (the same body enemy_ai.cpp ports).
float norm_2pi(float x) {
    constexpr double k2Pi = 6.2831854820251465;
    if (x > k2Pi) return float(std::fmod(double(x), k2Pi));
    if (x < 0) return float(std::fmod(double(x), k2Pi) + k2Pi);
    return x;
}
// FUN_005eec50(h, a) = norm(a) - norm(h), wrapped into [-pi, pi].
float angle_diff(float h, float a) {
    float d = norm_2pi(a) - norm_2pi(h);
    if (d < 0) { if (d < -kPi) d += 6.28318548f; }
    else if (d > kPi) d -= 6.28318548f;
    return d;
}
Mat4 mat_rot_y(float a) {
    const float c = std::cos(a), s = std::sin(a);
    Mat4 m;
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
    return m;
}
Mat4 mat_rot_x(float a) {
    const float c = std::cos(a), s = std::sin(a);
    Mat4 m;
    m.m[5] = c; m.m[6] = s; m.m[9] = -s; m.m[10] = c;
    return m;
}
Mat4 mat_translate(const Vec3& t) {
    Mat4 m;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction: FUN_00505810 (+ FUN_00443b80 for EEX record 0). §2.2.
Boss01::Boss01(const Boss01Setup& s) {
    char_ = s.character;
    bb5c_ = s.diff_index;
    bb58_ = s.diff_easy;
    bc48_ = s.diff_very_easy;
    time_attack_ = s.time_attack;
    pos_ = s.pos;

    // FUN_00443b80(record 0, obj+0x94).
    max_hp_ = s.hp;
    str_ = s.str;
    def_ = s.def;
    rec3_ = s.rec3;
    exp_b0_ = s.exp_b0;
    rec4_ = float(int(s.rec4));
    level_ = s.level;
    if (bb5c_ > 0 && bb5c_ <= 2) {            // table 0x68a4f0 = {-, 1.25, 1.5}
        static constexpr float kHpMul[3] = {1.0f, 1.25f, 1.5f};
        max_hp_ *= kHpMul[bb5c_];
    }
    if (!bc48_) {
        if (bb58_) def_ = s.def - 1.0f;         // Easy (and not Very Easy)
    } else {
        max_hp_ *= 0.75f;                       // 0x6bd390
        def_ = s.def - 2.0f;                    // 0x6bd580
        str_ = s.str - 1.0f;
    }
    hp_ = max_hp_;

    // Ctor deltas [BYTES 0x5058ff..0x505a9b].
    float dstr = 1.0f, ddef = 0.0f;
    if (bb5c_ == 0) dstr = bb58_ ? 2.0f : 1.0f;
    else if (bb5c_ == 2) { dstr = -4.0f; ddef = 1.0f; }   // 0x6be0b8
    else if (bb5c_ == 1) { dstr = -6.0f; ddef = 1.0f; }   // 0x6be0c0
    float st = str_ + dstr, df = def_ + ddef;
    str_ = st;
    def_ = df;
    // Retry softening, only outside time-attack (n = g_flags[196]).
    const int n = s.retries;
    if (!bc48_) {
        if (bb58_ && !time_attack_) {
            const int k = n < 20 ? (n >> 1) : n - 10;
            st -= float(k); df -= float(k);
            str_ = st; def_ = df;
        }
    } else if (!time_attack_) {
        const float p = std::pow(float(n), 1.25f);   // FUN_0041b790 [H-high: powf]
        st -= p; df -= p;
        str_ = st; def_ = df;
    }
    if (st <= 1.0f) str_ = 1.0f;
    if (df <= 1.0f) def_ = 1.0f;

    // blk initial values (memset 0, then 00505810.c:151-188).
    stun_ = max_hp_ * 0.8f;                       // 0x6bd3a8
    e0_ = e4_ = -1.57079637f;                     // 0xbfc90fdb
    fc_ = 0.959999979f;                           // 0x3f75be0b
    look_ = {0, 2.0f, 22.0f};
    home_ = {0, 2.0f, 33.0f};
    aim_ = s.player_pos;
    obj11c_ = 0;

    // The 21 parts (FUN_005a1a10({0, idx, 0.75, armor = idx > 1, 1, owner})).
    for (int i = 0; i < kParts; ++i) {
        Boss01Part& p = parts_[std::size_t(i)];
        p.bone = kPartBone[i];
        p.alive = true;
        p.hurt_radius = 0.75f;
        p.armor = i > 1;
        p.hp = hp_ * 10.0f;                       // part +0x98 = obj+0x98 x 10 (0x6bd928)
        p.str = str_;
        p.def = def_;
        p.power = 1.0f;                           // +0xfc = 1.0
        // obj+0xb4..0xc0 are 0 (not written by EEX / ctor [H-med]); then:
        if (char_ == 0 || char_ == 1) { p.resist[0] = 0.5f; p.resist[3] = 0.35f; }
        else if (char_ == 2 || char_ == 3) { p.resist[0] = 0.5f; p.resist[3] = 0.7f; }
        if (i > 1) p.def = kDef999;
    }
}

Mat4 Boss01::matrix() const { return mat_rot_y(d4_) * mat_translate(pos_); }

Vec3 Boss01::flash_tint() const {
    float f = flash_;
    if (f > 1.0f) f -= 0.65f;
    return {f * 107.0f / 255.0f * 0.5f, f * 55.0f / 255.0f * 0.5f, f * 19.0f / 255.0f * 0.5f};
}

void Boss01::add_part_damage(int i, float dmg, float dmg_hp) {
    Boss01Part& p = parts_[std::size_t(i)];
    p.dmg += dmg;
    p.hp -= dmg_hp;
}

bool Boss01::part_hittable(int i) const {
    const Boss01Part& p = parts_[std::size_t(i)];
    return p.alive && p.hp > 1.0f && p.hurt_radius > 0.0f;
}

// ---------------------------------------------------------------------------
std::uint32_t Boss01::draw() { return in_->rng(); }

void Boss01::commit(int s) {   // FUN_00509260
    state_ = s;
    bc_ = 0;
    c0_ = 0;
    c4_ = 0;
    drool_ = 0;
}

void Boss01::set_clip(int clip, int blend) {
    out_->clips.push_back({clip, blend});
    clip_ = clip;
    clip_count_ = 0;                              // FUN_005f5c80 zeroes +0x1c
    clip_frame_ = float(kClipTable[clip].start);
}

void Boss01::cam(int cur, int prev, float rate) {
    cam_.cur = cur;
    cam_.prev = prev;
    cam_.t = 0.0f;
    cam_.rate = rate;
    out_->camera_written = true;
}

Boss01Effect& Boss01::fx(int id, const Vec3& p) {
    Boss01Effect e;
    e.id = id;
    e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}

// ---------------------------------------------------------------------------
// One brain step, FUN_005062d0 (§4.1).
Boss01Output Boss01::tick(const Boss01Input& in) {
    Boss01Output out;
    if (destroyed_) return out;
    in_ = &in;
    out_ = &out;
    bones_ = in.bones;
    clip_ = in.clip;
    clip_count_ = in.clip_count;
    clip_frame_ = in.clip_frame;
    player_now_ = in.player_pos;

    if (hp_ < 1.0f) {
        death_visuals();                          // includes FUN_005093e0 while state == 9
    } else {
        parts_update();
        const Vec3 d = in.player_pos - pos_;
        e0_ = std::atan2(d.z, d.x);               // fld dz; fld dx [BYTES 0x50633b]
        d8_ = std::sqrt(d.y * d.y + d.x * d.x + d.z * d.z);
        dc_ = std::sqrt(d.x * d.x + 0.0f + d.z * d.z);
        aim_update();
        if (stun_flag_ == 1 && state_ != 3) {
            stun_entry();
        } else {
            switch (state_) {
                case 0: state0(); break;
                case 1: state1(); break;
                case 2: state2(); break;
                case 3: state3(); break;
                case 4: state4(); break;
                case 5: state5(); break;
                case 6: break;   // FUN_00511340: never committed (dead code, §5.6) — not ported
                case 7: state7(); break;
                case 8: state8(); break;
                case 9: state9(); break;
                case 10: state10(); break;
                default: break;
            }
        }
        common_tail();
        // Heading toward the FIXED point blk+0x100 [BYTES 0x50705e..0x5070d0].
        e4_ = std::atan2(look_.z - pos_.z, look_.x - pos_.x);
        d4_ = wrap_pi((kPi - e4_) + kHalfPi);
        // Armour toggles (§3.1, 005062d0.c:571-586).
        Boss01Part& p0 = parts_[0];
        Boss01Part& p1 = parts_[1];
        if (state_ == 3) { p1.def = kDef999; p1.armor = true; }
        else { p1.def = def_; p1.armor = false; }
        if (state_ == 3 && bc_ == 2) { p0.def = def_; p0.armor = false; }
        else { p0.def = kDef999; p0.armor = true; }
        // Counters.
        if (dc_ >= 8.0f) near_ = 0; else ++near_;   // 0x6bd8e8
        ++t1_;
        ++t2_;
        push_player();
    }
    tail_always();
    camera_presets();                             // every tick, the HP < 1 path included
    in_ = nullptr;
    out_ = nullptr;
    return out;
}

// ---------------------------------------------------------------------------
// The camera presets [BYTES 0x50766f..0x5082be], build/boss_re/BOSS_CAMERA.md §3. Rebuilt from
// scratch every tick; V0 = look_ (blk+0x100), V1 = home_ (blk+0x10c), dc = blk+0xdc (stale on the
// death path, as in the engine), yaw = blk+0xd4.
void Boss01::camera_presets() {
    const Vec3 P = in_->player_pos, B = pos_;
    constexpr float k3 = 0.33333334f;                       // 0x6bd29c
    const float dcl = std::min(20.0f, std::max(5.0f, dc_));
    const float f = (std::max(0.0f, dcl - 10.0f) / 10.0f) * 0.2f;
    const float h = (2.0f <= P.y - 2.0f) ? (P.y - 2.0f) - 2.0f : 0.0f;
    const float lift = h * 0.2f * h * 0.2f;
    auto tb = [&](float zo) {
        return Vec3{(B.x - look_.x) * k3 + 0.0f, (2.0f - look_.y) * k3 + 2.0f, (B.z - look_.z) * k3 + 18.0f + zo};
    };
    auto follow = [&](Vec3 T) {
        T.x = (P.x - T.x) * f + T.x;
        T.y = (2.0f - T.y) * f + T.y;
        T.z = (P.z - T.z) * f + T.z;
        return T;
    };
    auto clamp_y = [&](Vec3 T) { const float y = lift + T.y; T.y = P.y < y ? P.y : y; return T; };
    const Vec3 a = norm0(Vec3{B.x - look_.x, 2.0f - look_.y, B.z - look_.z});
    const Vec3 b = norm0(home_ - look_);
    const float s = (1.0f - (b.y * a.y + a.x * b.x + b.z * a.z)) * -2.0f;
    // 1: fight
    const Vec3 T1 = clamp_y(follow(tb(10.0f)));
    cam_tgt_[1] = T1;
    cam_eye_[1] = {T1.x + 0.0f, f * 15.0f + 15.0f + T1.y, f * -29.0f + -29.0f + s + T1.z};
    // 2: inhale / bite — the eye 17 + D past the boss along its facing, at y = 10
    cam_tgt_[2] = B;
    const float dx = P.x - B.x, dz = P.z - B.z;
    const float D = std::max(6.0f, std::sqrt(dx * dx + dz * dz));
    const Vec3 fwd = rot_y({0, 0, -1.0f}, d4_);
    Vec3 e2{fwd.x * D + B.x, fwd.y * D + 2.0f, fwd.z * D + B.z};
    e2 = {fwd.x * 17.0f + e2.x, e2.y + 8.0f, fwd.z * 17.0f + e2.z};
    cam_eye_[2] = e2;
    // 3: bubble spray
    const Vec3 T3 = clamp_y(follow(tb(8.0f)));
    const float z3off = s - (f * 28.0f + 28.0f);
    cam_tgt_[3] = T3;
    cam_eye_[3] = {T3.x + 0.0f, f * 19.0f + 19.0f + T3.y, T3.z + z3off};
    // 4: arm sweep
    const Vec3 T4 = clamp_y(follow(tb(12.0f)));
    cam_tgt_[4] = T4;
    cam_eye_[4] = {T4.x + 0.0f, f * 16.0f + 16.0f + T4.y, (s - (f * 32.0f + 32.0f)) + T4.z};
    // 5: stun — the follow term uses preset 1's final target (the engine's own operand)
    const float py = std::min(P.y, 13.0f);
    const float lx = P.x - B.x, ly = py - (B.y + 11.0f), lz = P.z - B.z;
    const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
    float nearv = 0.0f;
    if (len < 16.0f) nearv = std::min(0.7f, (16.0f - len) * 0.0625f);
    Vec3 T5 = tb(8.0f);
    T5 = {(P.x - T1.x) * f + T5.x, (2.0f - T1.y) * f + T5.y, (P.z - T1.z) * f + T5.z};
    T5.y += nearv * 9.0f;
    cam_tgt_[5] = T5;
    cam_eye_[5] = {T5.x + 0.0f, f * 17.0f + 17.0f + nearv * 9.0f + T5.y,
                   (nearv * 12.0f - (f * 30.0f + 30.0f)) + s + T5.z};
    // 6: laser
    const Vec3 T6 = clamp_y(follow(tb(9.0f)));
    cam_tgt_[6] = T6;
    cam_eye_[6] = {T6.x + 0.0f, f * 18.0f + 18.0f + T6.y, T6.z + z3off};
}

// ---------------------------------------------------------------------------
// FUN_005084a0: part placement, arming, damage consumption, phases, contact flags.
void Boss01::parts_update() {
    const int add8 = bb5c_ < 1 ? 8 : 0;
    const std::uint32_t lo = std::uint32_t((toal() ? 60 : 68) + add8);
    const std::uint32_t hi = std::uint32_t((toal() ? 100 : 108) + add8);
    const std::uint32_t blo = std::uint32_t((toal() ? 80 : 88) + add8);
    const std::uint32_t bhi = std::uint32_t((toal() ? 90 : 98) + add8);
    const std::uint32_t t = c0_;
    const bool a01 = clip_ == kAtk01L || clip_ == kAtk01R;
    const bool run = clip_ == kRunR || clip_ == kRunL;

    parts_[0].hp = max_hp_;
    parts_[1].hp = max_hp_;
    obj11c_ ^= 8;                                 // xor, 0x508579

    for (int i = 0; i < kParts; ++i) {
        Boss01Part& p = parts_[std::size_t(i)];
        const Vec3 b = bones_[std::size_t(kPartBone[i])];
        p.pos = b;
        p.hurt_centre = b;
        p.centre = b;
        p.hurt_radius = 1.0f;
        p.radius = 0.0f;
        p.power = 4.0f;                           // 0x40800000
        p.rtype = 2;
        p.flags = 0;
        p.armed = false;
        p.shape = 0;
        if (i == 0) {
            switch (char_) {
                case 0: case 1: p.resist[0] = 0.5f; p.resist[3] = 0.7f; break;
                case 2: p.resist[0] = 0.5f; p.resist[3] = 0.75f; break;
                case 3: p.resist[0] = 0.5f; p.resist[3] = 0.5f; break;
                default: break;
            }
        } else if (i == 1) {
            p.hurt_radius = 2.1f;                 // 0x40066666
            p.hurt_centre.y -= 0.5f;              // 0x6bd304
            if (a01 && !latch_ && blo <= t && t <= bhi) {
                p.armed = true;
                p.power = 20.0f;
                p.centre.y -= 4.0f;               // 0x6bd7c0
                p.radius = 8.0f;
                p.shape = 1;
                p.half_h = 8.0f;
                p.flags = 2;
            }
        } else if (i == 2) {
            if (clip_ == kAtk10_3 && t - 20u < 21u) {
                p.armed = true;
                p.power = 14.0f;
                const Vec3 dl = norm0(look_ - p.centre);
                p.centre.x += dl.x * 1.25f;       // 0x6bd494
                p.centre.z += dl.z * 1.25f;
                p.centre.y -= 1.25f;
                p.radius = 1.75f;
                p.shape = 1;
                p.half_h = 1.0f;
                p.flags = 2;
            }
        } else {
            if ((i == 3 || i == 4) && run) {
                p.armed = true;
                p.radius = 1.5f;
                p.power = 0.0f;
                p.rtype = 3;
                p.flags |= 2;
                p.flags |= 8;
            }
            if (i == 3) {
                if (clip_ == kAtk01L && lo < t && t < hi && !latch_) {
                    p.armed = true; p.power = 20.0f; p.radius = 1.5f; p.flags = 2;
                }
            } else if (i == 4) {
                if (clip_ == kAtk01R && lo < t && t < hi && !latch_) {
                    p.armed = true; p.power = 20.0f; p.radius = 1.5f; p.flags = 2;
                }
            } else {
                const bool lchain = i >= 5 && i <= 9 && clip_ == kAtk01L;
                const bool rchain = i >= 10 && i <= 14 && clip_ == kAtk01R;
                if ((lchain || rchain) && lo < t && t < hi && !latch_) {
                    p.armed = true;
                    p.power = 20.0f;
                    p.centre.y -= 2.5f;           // 0x6bd624
                    p.radius = 2.5f;
                    p.shape = 1;
                    p.half_h = 2.5f;
                    p.flags |= 2;
                }
                if (i >= 15) {
                    p.hurt_radius = 0.0f;
                    p.radius = 0.0f;
                    if (run) {
                        p.armed = true;
                        p.radius = 1.25f;          // 0x3fa00000
                        p.power = 0.0f;
                        p.rtype = 3;
                        p.flags |= 2;
                        p.flags |= 8;
                        obj11c_ |= 8;
                    }
                }
            }
        }

        // Consume the damage the swing scan accumulated (§3.3).
        if (p.dmg != 0.0f) {
            const float dmg = p.dmg + 0.0f;
            flash_ = 1.65f;                       // 0x3fd33333
            p.dmg = 0.0f;
            if (dmg > 0.0f) {
                if (i == 0) {
                    hp_ -= dmg;
                    dmg_since_stun_ += dmg;
                    if (hp_ < 1.0f) {
                        hp_ = 0.0f;
                        stun_flag_ = 0;
                        for (Boss01Part& q : parts_) { q.armed = false; q.alive = false; }
                        se3d(206, pos_);          // 0xce b_01_se16
                        if (fx_slot_[0]) kill_fx(0x3c);    // blk+0x1ec
                        if (fx_slot_[1]) kill_fx(0x142);   // blk+0x1f0
                        if (fx_slot_[8]) kill_fx(0x148);   // blk+0x20c
                        commit(9);                // inline, 0x508ecb
                    }
                    if (hp_ < max_hp_ * 0.75f && phase_ == 2) phase_ = 3;
                    if (hp_ < max_hp_ * 0.5f && phase_ == 3) phase_ = 4;
                    if (hp_ < max_hp_ * 0.25f && phase_ == 4) phase_ = 5;
                } else if (i == 1 && stun_ > 0.0f) {
                    ++n1_;
                    const float g = stun_ - dmg;
                    ++n2_;
                    stun_ = g;
                    if (phase_ == 0) phase_ = 1;
                    if (g < 1.0f) {
                        stun_ = 0.0f;
                        // (a TransformCoord of (0,2,0) by Null(242) into a dead local, [UNKNOWN] use)
                        if (phase_ == 1) phase_ = 2;
                        stun_flag_ = 1;
                        dmg_since_stun_ = 0.0f;
                    }
                }
            }
        }
    }

    // Mesh-contact flags (§3.4, 005084a0.c:346-391).
    contact_ = Boss01Contact{};
    if (!run) {
        const std::uint32_t mid = std::uint32_t(int(hi - lo) / 2) + lo;
        bool done = false;
        if (clip_ == kAtk01L && lo < t && t < hi && !latch_) {
            contact_.l_window = true;
            contact_.l_mid = contact_.mid80 = (t == mid);
            done = true;
        }
        if (!done) {
            if (clip_ == kAction01) contact_.action01 = true;
            else contact_.bite = clip_ == kAtk10_3;
        }
    }
    contact_.stunned = stun_flag_ == 1;
}

// ---------------------------------------------------------------------------
// The aim point blk+0x180 and the laser beam matrix (005062d0.c:206-290, §5.5).
void Boss01::aim_update() {
    if (state_ == 5 || blk134_ == 5) {
        const Vec3 pp{in_->player_pos.x, 1.9f, in_->player_pos.z};   // 0x6bd558
        const Vec3 dl = pp - aim_;
        float m = 5.0f;                           // 0x6bd868
        if (toal()) {
            // [H-med] atan2 argument order (C notes: atan2(dx, dz), a D3D-yaw angle).
            const float dd = std::fabs(angle_diff(d4_, std::atan2(dl.x, dl.z)));
            if (dd < k35 || dd > k145) {
                if (bb58_) m = 3.75f;             // 0x6bd760
                else m = bb5c_ > 0 ? 4.25f : 4.0f; // 0x6bd7f8 / 0x6bd7c0
            } else {
                m = 1.5f;
            }
        }
        const std::uint32_t c = std::min<std::uint32_t>(c0_, 60u);
        const float step = (float(c) / 60.0f) * m * 0.05f;   // 0x6bda94, 0x6bd13c
        const float l = len(dl);
        if (step <= l) {
            if (!toal() || (in_->player_section != 0xb && in_->player_section != 4)) {
                const Vec3 n = norm0(dl);
                aim_.x += n.x * step;
                aim_.z += n.z * step;
                aim_.y = 1.9f;
            }
        } else {
            aim_ = pp;
            aim_.y = 1.9f;
        }
        // Beam matrix blk+0x140 = RotX(pitch) * RotY(yaw) * T(Null(238)) [H-high axes].
        const Vec3 m238 = bones_[11];
        const Vec3 d = aim_ - m238;
        const float pitch = std::atan2(std::sqrt(d.x * d.x + d.z * d.z), -d.y) - kHalfPi;
        const float yaw = k3HalfPi - std::atan2(d.z, d.x);
        out_->beam = mat_rot_x(pitch) * mat_rot_y(yaw) * mat_translate(m238);
    } else if (stun_flag_ == 0) {
        aim_ = in_->player_pos;
    }
    // The head look-at (Bone(162) quaternion slerp 0.1 toward aim_) is cosmetic and not
    // ported here; the host can aim the head at aim().
}

// ---------------------------------------------------------------------------
// Stun entry [BYTES 0x506a65..0x506fdb].
void Boss01::stun_entry() {
    if (cam_.cur != 5) {
        if (cam_.t == 1.0f) cam_.prev = cam_.cur;
        else {
            // The snapshot of the running blend into slot 0xb (BOSS_CAMERA.md §4b), LINEAR in t,
            // from LAST tick's presets (the recompute runs later this tick). from == 5 reads
            // preset 4's fields [BYTES 0x506c6f]; outside 1..6 the engine reads stale stack [H].
            const int fi = cam_.prev == 5 ? 4 : cam_.prev, ci = cam_.cur;
            if (fi >= 1 && fi <= 6 && ci >= 1 && ci <= 6) {
                const float t = cam_.t;
                auto lerp = [t](const Vec3& p, const Vec3& q) {
                    return Vec3{(q.x - p.x) * t + p.x, (q.y - p.y) * t + p.y, (q.z - p.z) * t + p.z};
                };
                out_->cam_snapshot = std::make_pair(lerp(cam_eye_[std::size_t(fi)], cam_eye_[std::size_t(ci)]),
                                                    lerp(cam_tgt_[std::size_t(fi)], cam_tgt_[std::size_t(ci)]));
            }
            cam_.prev = 0xb;
        }
        cam_.rate = 0.01f;
        cam_.t = 0.0f;
        cam_.cur = 5;
        out_->camera_written = true;
    }
    if (fx_slot_[1]) kill_fx(0x142);
    if (fx_slot_[8]) kill_fx(0x148);
    if (clip_ == kRunR || clip_ == kRunREnd || clip_ == kRunL || clip_ == kRunLEnd) {
        const float e8 = e8_;
        arrived_ = 0;
        e8_ = 0.0f;
        e4_ += e8;
    }
    out_->zoom.rate = -0.01f;                     // 0xbc23d70a
    for (int id : {191, 198, 194, 195, 196, 199, 207}) se_stop(id, 0);   // FUN_005ce3d0(id, 0)
    commit(3);                                    // inline, 0x506fb3
}

// ---------------------------------------------------------------------------
// FUN_00512720: Bone(115) copy (host-side) + the state-7 Bone(114) bubble drool.
void Boss01::common_tail() {
    if (!drool_) return;
    const Vec3 b114 = in_->bone114;
    // 1. a drool bubble effect stuck to the Bone(115) matrix.
    const float th1 = float(45 - int(R(90))) * kDeg;
    const Vec3 v1 = rot_y({0, 0, -1.0f}, (k3HalfPi - e4_) + th1);
    {
        Boss01Effect& e = fx(0x143, b114);
        e.power = 0; e.knock = 1; e.rtype = 0; e.status = 0; e.cls = -1;
        // [H] the effect pool had room (FUN_00450880 != 0): the next two draws happen
        // only then.
        const float s = 0.5f - float(R(30)) / 100.0f;
        float m = std::fabs(th1) / kQuarterPi;
        if (m < 0.2f) m = 0.2f;
        const float r40 = float(R(40));
        e.scale = {s, s, s};
        e.scale_after = true;
        e.attach = Boss01Effect::kBone115;
        e.attach_mode = 2;
        e.attach_off = {m * v1.x * 1.5f, ((0.2f - r40 / 100.0f) + m * v1.y * 1.5f) - 0.25f,
                        m * v1.z * 1.5f};
        e.life = 10;
    }
    // 2. a falling drop: rejection-sample the angle toward the edges.
    float th2 = 0;
    for (;;) {
        th2 = float(35 - int(R(70))) * kDeg;
        float f = std::fabs(th2) / k35;
        if (f < 0.1f) f = 0.1f;
        if (float(R(100)) <= f * 10.0f) break;
    }
    const Vec3 v2 = rot_y({0, 0, -1.0f}, (k3HalfPi - e4_) + th2);
    Boss01Bullet bl;
    bl.kind = 0x1f;
    bl.effect = 0x143;
    bl.pos = add(add(b114, mul(v2, 1.5f)), {0, 0.25f, 0});
    bl.dir = norm0(add(v2, {0, -6.0f, 0}));
    const std::uint32_t ra = R(30), rb = R(3);
    bl.f[0] = float(rb) / 1000.0f + 0.012f;      // 0x6bd0a0
    bl.f[1] = -0.0002f;                          // 0xb951b717
    bl.f[10] = 0.4f - float(ra) / 100.0f;
    out_->bullets.push_back(bl);
}

// ---------------------------------------------------------------------------
// The HP < 1 branch of the brain [BYTES 0x50720f..0x507478] (§8.1).
void Boss01::death_visuals() {
    cam_.cur = cam_.prev = 5;
    out_->camera_written = true;
    const int b = bc_;
    if (b == 80) {
        Boss01Effect& e = fx(0x63, add(pos_, {0, 3.0f, 0}));
        e.side = 0; e.stat = Boss01Effect::kNone; e.power = 0; e.knock = 1; e.rtype = 0; e.cls = -1;
        se3d(576, pos_);
    }
    if (b == 180) {
        Boss01Effect& e = fx(0x58, add(pos_, {0, 3.0f, 0}));
        e.side = 1; e.power = 0; e.knock = 1; e.rtype = 0; e.cls = -1;
    }
    if (std::uint32_t(b) - 21u < 69u) {
        if (b % 12 == 0) {
            const std::uint32_t r = R(25);
            out_->death_sparks = Boss01Output::DeathSparks{0x57, b > 60 ? 2 : 1,
                                                           float(int(r)) / 100.0f + 0.5f};
        }
        float v = tint_v_;
        if (v < 1.0f) {
            v += 0.004f;                          // 0x6bd03c
            tint_v_ = v;
            if (v > 1.0f) { tint_v_ = 1.0f; v = 1.0f; }
        }
        const float s = v * 0.4f;
        out_->mesh_tint = Vec3{s, s, s};
    }
    if (state_ == 9) state9();
}

// FUN_005061e0: push the player out of the body (planar d < 4.2) unless stunned.
void Boss01::push_player() {
    float dx = player_now_.x - pos_.x;
    float dz = player_now_.z - pos_.z;
    const float d = std::sqrt(dx * dx + 0.0f + dz * dz);
    if (d != 0.0f) {
        const float k = 1.0f / d;
        dz = k * dz;
        dx = k * dx;
    }
    if (d < 4.2f) {                               // 0x6bd7e8
        float k = (4.2f - d) * 0.2f;              // 0x6bd210
        if (k < 0.0f) k = 0.0f;
        if (stun_flag_ == 0) {
            out_->player_push.x += dx * k;
            out_->player_push.z += dz * k;
        }
    }
}

// LAB_00507488: shadow (host), HP bar, flash decay, hit meshes.
void Boss01::tail_always() {
    if (!in_->fight || hp_ < 1.0f) {
        out_->gauge_hide = true;
    } else if (!(last_bar_hp_ == hp_)) {          // NaN counts as different
        last_bar_hp_ = hp_;
        out_->gauge = Boss01Gauge{hp_, max_hp_, 3500.0f, 1};
    }
    if (flash_ != 0.0f) {
        flash_ -= 0.075f;                         // 0x6bd174
        if (flash_ < 0.0f) flash_ = 0.0f;
    }
    const Mat4 m = matrix();
    out_->hit_mesh[0] = mat_translate({0, 0, -1.75f}) * m;   // 0x6be088
    out_->hit_mesh[1] = mat_translate({0, 0, -3.0f}) * m;    // 0x6be0a8
}

// ---------------------------------------------------------------------------
// State 0, FUN_005095c0: the decision (§4.3). RNG order: R1, R2 always; then optionally
// R(1000), R3, R(2).
void Boss01::state0() {
    if (!started_) {
        if (!in_->fight) return;                  // idle (WAIT from the ctor)
        started_ = 1;
        f7_ = 1;
        t2_ = t1_ = near_ = 0;
        commit(10);
        cam_.forced = -1;
        cam_.cur = 1;
        if (in_->continue_snap) { cam_.prev = 0; cam_.t = 1.0f; }
        else { cam_.prev = 0xc; cam_.t = 0.0f; cam_.rate = 0.02f; }   // 0x3ca3d70a
        out_->camera_written = true;
        return;
    }
    if (bc_ == 0) {
        set_clip(kWait, 1);
        ++bc_;
        c4_ = (phase_ > 1 || bb5c_ != 0) ? 44 : 88;
        if (f7_) c4_ = 0;
        if (after06_ == 1) c4_ = toal() ? 220 : 264;
        after06_ = 0;
        return;
    }
    if (bc_ != 1) return;
    if (c4_ != 0 && --c4_ != 0) return;

    if (in_->player_surface == 5 && in_->player_grounded && in_->player_floor > 4.0f) {
        commit(8);
        return;
    }
    const std::uint32_t r1 = R(100);
    const float ang = std::fabs(angle_diff(e4_ + e8_, e0_));
    std::uint32_t wlo, whi;
    switch (char_) {
        case 0: case 1: wlo = 6; whi = 16; break;
        case 2: wlo = 18; whi = 24; break;
        case 3: wlo = 10; whi = 22; break;
        default: wlo = 100; whi = 100; break;
    }
    if (phase_ > 3) wlo = std::uint32_t(int(float(wlo) * 0.7f));   // mulss + FUN_0066f350
    const std::uint32_t r2 = R(100);
    std::uint32_t prob = 0;
    auto reset = [&] { n1_ = n2_ = near_ = t1_ = 0; fr_ = 0; prob = 100; };
    if (phase_ < 2) {
        if ((n2_ >= whi + 11 || t1_ > 0x63f || fr_ || ang > k60) && !f7_) reset();
    } else if ((fr_ || (ang > k60 && r2 > 49)) && !f7_) {
        reset();
    }
    if (r1 < (phase_ != 0 ? prob : 0u)) {         // ARENA RUN
        commit(1);
        n1_ = 0;
        return;
    }
    if (phase_ > 1) {                             // INHALE
        bool grab;
        if (phase_ < 4) grab = n2_ >= whi || t1_ >= 1300 || (in_->spawn_count > 0 && t1_ > 1000);
        else grab = n2_ >= whi - 3 || t1_ > 999 || (in_->spawn_count > 0 && t1_ > 900);
        if (grab && !f7_) {
            commit(4);
            cam(2, 1, 0.03f);
            fr_ = 1;
            return;
        }
    }
    if (t2_ > 1000) {
        if (phase_ < 2) { if (!f7_) f7_ = 1; }
        else if (!f7_) { if (R(1000) < 10) f7_ = 1; }
    }
    if (f7_) {                                    // BUBBLE SPRAY
        f7_ = 0;
        t2_ = 0;
        commit(7);
        cam(3, cam_.cur, 0.025f);
        return;
    }
    if (phase_ >= 2 && dc_ < 12.0f) {             // MELEE BAND
        const bool p4 = phase_ >= 4;
        const bool pre = (n1_ == 0 || near_ <= (p4 ? 120u : 180u)) && n1_ < wlo;
        bool swipe = !pre;
        if (pre) {
            const bool stay = near_ <= (p4 ? 150u : 220u) && (ang <= k50 || r2 > 19) &&
                              (ang <= k60 || r2 > 49);
            swipe = !stay;
        }
        if (swipe) {                              // ARM SWEEP
            n1_ = 0;
            near_ = 0;
            commit(2);
            cam(4, cam_.cur, 0.04f);
            return;
        }
    }
    if (!(dc_ < 6.0f) && phase_ >= 2) {           // MOUTH LASER
        commit(5);
        cam(6, cam_.cur, 0.04f);
        return;
    }
    const std::uint32_t r3 = R(100);
    if (r3 < 40 && phase_ <= 1) {
        commit(5);
        cam(6, cam_.cur, 0.04f);
        return;
    }
    c4_ = int((draw() >> 17) % 2 + 1) * 44;       // 44 or 88 more frames
}

// ---------------------------------------------------------------------------
// State 1, FUN_00509df0: the arena run (§5.1).
void Boss01::state1() {
    const Vec3 C = look_;
    const Vec3 RC = home_ - C;
    const std::uint32_t P = phase_ < 2 ? 17u : 14u;
    if (bc_ == 0) {
        int dir;
        if (!in_->player_present) {
            dir = int((draw() >> 17) & 1);
        } else {
            f0_ = ec_;
            if (ec_ == 0) {
                const std::uint32_t w = draw();
                if (w & 0x20000) { ec_ = -100; dir = 1; } else { ec_ = 100; dir = 0; }
            } else if (ec_ == 100) {
                ec_ = R(5) < 2 ? 0 : -100; dir = 1;
            } else if (ec_ == -100) {
                ec_ = R(5) < 2 ? 0 : 100; dir = 0;
            } else {
                c4_ = -1;                         // stuck forever; not reachable
                return;
            }
        }
        c4_ = dir;
        set_clip(dir ? kRunR : kRunL, 1);
        const int d = std::abs(ec_ - f0_);
        ++bc_;
        arrived_ = 0;
        c0_ = 0;
        const int frames = phase_ < 2 ? (d == 200 ? 110 : 55) : (d == 200 ? 100 : 50);
        tilt(frames, {0.15f, 0.0f, 0.15f});
        se3d(191, pos_);
        out_->rumble.push_back(4);
        return;
    }
    if (bc_ != 1) {                               // stopping
        const int d = std::abs(ec_ - f0_);
        if (d == 200 ? bc_ == 2 : bc_ <= 3) {
            ++c0_;
            if (c0_ % P == 0) { se3d(191, pos_); ++bc_; }
        }
        if ((clip_ == kRunREnd || clip_ == kRunLEnd) && clip_count_ != 0) {
            arrived_ = 0;
            commit(0);                            // direct write 0x50b307 (same fields)
            const float e8 = e8_;
            e8_ = 0.0f;
            e4_ = e8 + e4_;
            speed(1.0f);
            f7_ = 1;                              // [BYTES 0x50b36b]
        }
        return;
    }
    // Sub 1: running.
    ++c0_;
    const int dd = std::abs(ec_ - f0_);
    if ((phase_ < 2 || dd == 200) && c0_ == 60) out_->rumble.push_back(phase_ < 2 ? (dd == 200 ? 4 : 2) : 2);
    const Vec3 b = pos_ - C;
    const float a = wrap_pi((kPi - std::atan2(b.z, b.x)) + kHalfPi);
    const Vec3 vel = rot_y(c4_ ? Vec3{1.0f, 0, 0} : Vec3{-1.0f, 0, 0}, a);   // table 0x7ddb98
    const Vec3 tg = rot_y(RC, float(ec_) * fc_ / 100.0f);   // 0x6bdafc
    const Vec3 pv = rot_y(RC, float(f0_) * fc_ / 100.0f);
    const Vec3 u = norm0(b), p = norm0(pv), t = norm0(tg);
    const float r = len(RC);
    const float arc = (360.0f / ((r * kPi + r * kPi) / 10.0f)) * kDeg;
    const float thr = float(std::cos(double(arc)));
    if (thr - 0.01f < dot(u, t) && clip_ != kRunREnd && clip_ != kRunLEnd) {
        const ClipInfo& ci = kClipTable[c4_ ? kRunR : kRunL];
        const float f = clip_frame_;
        if (float(ci.end) <= f || f <= float(ci.start + 1) ||
            (float(ci.start + 10) <= f && f <= float(ci.start + 11)))
            set_clip(c4_ ? kRunREnd : kRunLEnd, 1);
    }
    bool arrive = false;
    if (dot(p, t) <= dot(p, u)) {
        if (arrived_) {
            arrive = true;
        } else {
            const float spd = phase_ < 2 ? 0.22f : 0.26f;   // 0x6bd22c / 0x6bd254
            pos_ = add(pos_, mul(vel, spd));
            speed(phase_ < 2 ? 0.55f : 0.65f);
            const Vec3 dl = pos_ - C;
            const float l = len(dl), rr = len(RC);
            Vec3 o = dl;
            if (l < rr) o = mul(dl, l / rr);      // literal (normally unreachable)
            if (rr < l) o = mul(o, rr / l);
            pos_ = add(C, o);
        }
    } else {
        arrived_ = 1;
        arrive = true;
    }
    if (arrive) {
        ++bc_;
        pos_ = add(C, tg);
    }
    // M890 seeding (phase >= 2).
    if (phase_ >= 2) {
        const std::uint32_t N = phase_ < 4 ? 19u : 13u;
        if (c0_ % N == 0 && in_->spawn_count <= 7) {
            const Vec3 dir = rot_y({0, 0, 1.0f}, a);
            const int i = int(R(9));
            const Vec3 nd = bones_[std::size_t(i)];
            Boss01Spawn sp;
            sp.pos = add(nd, dir);
            sp.level = level_;
            sp.f330 = 0.05f;
            sp.knock_vel = mul(dir, 0.25f);
            sp.mode72c = in_->spawn_chaser_exists ? 2 : 1;
            const int sref = int(out_->spawns.size());
            out_->spawns.push_back(sp);
            Boss01Effect& ea = fx(0x147, nd);
            ea.power = 0; ea.knock = 1; ea.rtype = 0; ea.cls = -1;
            ea.attach = Boss01Effect::kBone; ea.attach_bone = i; ea.attach_mode = 2;
            const int slot = in_->spawn_count + 1;   // the count AFTER the factory's ++
            if (slot >= 1 && slot <= 8) fx_slot_[std::size_t(2 + slot)] = true;
            Boss01Effect& eb = fx(0x147, sp.pos);
            eb.stat = Boss01Effect::kSpawn; eb.power = 0; eb.knock = 1; eb.rtype = 0; eb.cls = -1;
            eb.attach = Boss01Effect::kSpawnActor; eb.attach_mode = 2; eb.spawn_ref = sref;
        }
    }
    if (c0_ % 10 == 0) {                          // dust
        for (const Vec3& q : {pos_, bones_[16], bones_[17]}) {
            Boss01Effect& e = fx(0x14a, {q.x, 2.0f, q.z});
            e.power = 0; e.knock = 1; e.rtype = 0; e.cls = -1;
        }
    }
    if (c0_ % P == 0) se3d(191, pos_);            // footsteps
}

// FUN_0050b390: the sweep side (1 = ATTACK_01_L, 0 = ATTACK_01_R). Always one draw.
int Boss01::side_choose() {
    const float d = angle_diff(e4_ + e8_, e0_);
    const int r = int(R(100)) + (d > 0 ? 10 : -10);
    if (std::fabs(d) > k65) return d > 0 ? 1 : 0;
    const int v = ec_ != 100 ? r : 0;
    return (ec_ == -100 || v > 49) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// State 2, FUN_0050b650: one arm sweep (§5.2).
void Boss01::state2() {
    const bool t3 = toal();
    const bool varC = !t3 && bb5c_ < 1;
    const bool varCD = !t3;
    const int s = t3 ? (bb5c_ == 0 ? 0 : -8) : (bb5c_ < 1 ? 8 : 0);
    const float sp0 = t3 ? (bb5c_ == 0 ? 0.7f : 0.8f) : (bb5c_ < 1 ? 0.6f : 0.7f);
    if (bc_ == 0) {
        const int side = side_choose();
        c4_ = side;
        set_clip(side ? kAtk01L : kAtk01R, 1);
        ++bc_;
        speed(sp0);
        latch_ = 0;
    } else if (bc_ == 1) {
        const int t = int(++c0_);
        const bool L = c4_ != 0;
        const int lead = L ? 16 : 17;
        const int chain[4] = {L ? 4 : 9, L ? 3 : 8, L ? 2 : 7, L ? 1 : 6};
        const int bones5[5] = {lead, chain[0], chain[1], chain[2], chain[3]};
        if (t == 28 + s) se3d(193, pos_);
        if (t == 43 + s) {                        // charge burst (no hit emitter)
            const float sc[5] = {3.0f, 1.5f, 1.25f, 1.0f, 0.75f};
            for (int k = 0; k < 5; ++k) {
                Boss01Effect& e = fx(0x141, bones_[std::size_t(bones5[k])]);
                e.scale = {sc[k], sc[k], sc[k]};
                e.power = 0; e.knock = 1; e.rtype = 2; e.status = 0; e.cls = 0xff;
            }
        }
        if (t >= 33 + s && t <= 108 + s) {        // red glow up
            float v = tint_v_;
            if (v < 1.0f) {
                v += 0.03f;                       // 0x6bd100
                tint_v_ = v;
                if (v > 1.0f) { tint_v_ = 1.0f; v = 1.0f; }
            }
            const float g = float(double(v) * 0.1);
            out_->mesh_tint = Vec3{v * 0.4f, g, g};
        }
        if (t >= 108 + s) {                       // glow down
            float v = tint_v_;
            if (v > 0.0f) {
                v -= 0.1f;
                tint_v_ = v;
                if (v < 0.0f) { tint_v_ = 0.0f; v = 0.0f; }
            }
            out_->mesh_tint = Vec3{v * 0.4f, v * 0.1f, v * 0.1f};
        }
        if (t == 70 + s) {
            speed(0.4f);
            out_->zoom.strength = 1e-4f;
            out_->zoom.rate = 0.0025f;
            out_->zoom.snapshot = true;
        }
        if (t == 80 + s) {                        // the slash (EF2 331 hit: em0 r 1.0 x scale)
            Boss01Effect& e = fx(0x14b, add(pos_, {0, 4.0f, 0}));
            e.rot = {L ? -2.87979317f : -0.261799395f, d4_ + (L ? kPi : 0.0f), 0};
            e.scale = {5, 5, 5};
            e.power = 20.0f; e.knock = 1; e.rtype = 2; e.status = 0; e.cls = 0xff;
            se3d(202, pos_);
        }
        if (t == 108 + s) {
            speed(0.8f);
            out_->zoom.rate = -0.005f;
            cam(1, 4, 0.04f);
        }
        out_->blur_centre = bones_[std::size_t(lead)];
        const int blo = varC ? 88 : 72 + s, bhi = varC ? 118 : 102 + s;
        if ((t & 1) == 0 && t >= blo && t <= bhi) {   // arm-trail beams (EF2 329)
            const float sc[5] = {1.4f, 1.0f, 0.7f, 0.5f, 0.4f};
            const bool hugo = char_ == 2;
            const float K = (varCD && hugo) ? (varC ? -1.0f : -2.0f) : 4.0f;
            for (int k = 0; k < 5; ++k) {
                const Vec3 bn = bones_[std::size_t(bones5[k])];
                const Vec3 dd = norm0({pos_.x - bn.x, 0.0f, pos_.z - bn.z});
                Vec3 q = add(bn, mul(dd, K));
                if (varCD) {
                    if (hugo) q.y -= 1.0f;
                    if (q.y < 3.0f) q.y = 3.0f;
                }
                Boss01Effect& e = fx(0x149, q);
                e.scale = {sc[k], sc[k], sc[k]};
                e.power = 20.0f; e.knock = 1; e.rtype = 2; e.status = 0; e.cls = 0xff;
                e.knock_from_facing = true;
            }
        }
        if (t >= 68 + s && t <= 108 + s && in_->player_took_damage) latch_ = 1;
    }
    if (clip_done()) {
        commit(0);
        speed(1.0f);
    }
}

// ---------------------------------------------------------------------------
// State 3, FUN_0050ea90: stun (§5.3).
void Boss01::state3() {
    ++c0_;
    const float M = max_hp_, D = dmg_since_stun_;
    switch (bc_) {
        case 0:
            speed(1.0f);
            set_clip(kPiyo1, 1);
            bc_ = 1;
            stun_flag_ = 1;
            drool_ = 0;
            se3d(200, pos_);
            se3d(201, pos_);
            break;
        case 1:
            if (clip_done()) {
                set_clip(kPiyo2, 0);
                bc_ = 2;
                Boss01Effect& e = fx(0x3c, bones_[10]);
                e.scale = {1.25f, 1.25f, 1.25f};
                e.power = 0; e.knock = 1; e.rtype = 0; e.status = 0; e.cls = 0xff;
                e.attach = Boss01Effect::kPart0;
                fx_slot_[0] = true;
                se3d(184, pos_);
                out_->minion_730 = 100;
            }
            break;
        case 2: {
            const std::uint32_t L = clip_count_;
            const int P = phase_;
            const bool G = !(P == 2 && hp_ == M);
            bool leave;
            if (bb5c_ == 0 && bb58_ && bc48_) {
                leave = D > 0.25f * M;
            } else if (bb5c_ == 0 && bb58_) {
                leave = G && ((L > 4 && P >= 2 && P <= 5) || D > 0.25f * M);
            } else if (bb5c_ == 2) {
                leave = G && ((L > 1 && P >= 2 && P <= 5) || D > 0.30f * M);
            } else if (bb5c_ == 1) {
                leave = G && ((L > 2 && P >= 2 && P <= 4) || (L > 1 && P == 5) || D > 0.30f * M);
            } else {
                leave = G && ((L > 4 && P == 2) || (L > 3 && P == 3) || (L > 2 && P == 4) ||
                              (L > 1 && P == 5) || D > 0.25f * M);
            }
            if (leave) {
                set_clip(kPiyo3, 0);
                bc_ = 3;
                kill_fx(60);
                se2d(203);
                out_->minion_730 = 1;
                stun_flag_ = 0;
            }
            break;
        }
        case 3:
            if (hp_ >= 1.0f && clip_count_ != 0) {
                commit(0);
                f7_ = 0;
                fr_ = 1;
                const bool tn = toal() && bb5c_ < 1;
                if (phase_ >= 2 && phase_ <= 4) {
                    float k = tn ? 0.6f : 0.5f;
                    if (!(D > M * 0.25f)) k -= 0.1f;
                    stun_ = k * M;
                } else if (phase_ == 5) {
                    float k = tn ? 0.7f : 0.6f;
                    if (!(D > M * 0.25f)) k -= 0.2f;
                    stun_ = k * M;
                }
                dmg_since_stun_ = 0.0f;
                cam(1, cam_.cur, 0.03f);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// State 4, FUN_0050f280: inhale -> eat Spawns -> bite + heal (§5.4).
Vec3 Boss01::pull_point() {
    Vec3 P = add(bones_[15], rot_y({0, 0, -1.5f}, d4_));
    P.y -= 0.5f;
    bones_[15] = P;                               // written back into Null(242) (this tick only)
    return P;
}

void Boss01::pull_actors(bool sub2) {
    const Vec3 P = pull_point();
    const int nact = int(in_->actors.size());
    for (int i = 0; i < nact; ++i) {
        Boss01Actor a = in_->actors[std::size_t(i)];
        // Apply the edits this tick already made (none before the pull; kept for clarity).
        if (a.cast == kCastM210) continue;
        bool moved = false, set330 = false, eaten = false;
        const Vec3 n = mul(norm0(a.pos - P), 15.0f);   // 0x6bd964
        const float diff = angle_diff(e4_, std::atan2(n.z, n.x));
        const bool live_player = a.cast == kCastPlayer && a.hp >= 1.0f;
        if (std::fabs(diff) >= k95) {             // out of the cone: the player only
            if (live_player) {
                a.pos.x -= n.x * 0.009f;          // 0x6bd074
                a.pos.z -= n.z * 0.009f;
                moved = true;
            }
        } else {
            if (live_player) {
                float k = -1.0f;
                if (sub2) {
                    if (char_ >= 0 && char_ <= 2) {
                        if (bb5c_ == 0 && bb58_) k = 0.0105f;            // 0x6bd088
                        else if (bb5c_ == 1 || bb5c_ == 2) k = 0.013f;    // 0x6bd0ac
                        else k = 0.0125f;                                  // 0x6bd0a8
                    } else if (char_ == 3) {
                        if (bb5c_ == 0 && bb58_) k = 0.0125f;            // 0x6bd0a8
                        else if (bb5c_ == 1 || bb5c_ == 2) k = 0.015f;    // 0x6bd0c0
                        else k = 0.0145f;                                  // 0x6bd0bc
                    }
                } else {
                    if (char_ >= 0 && char_ <= 2) k = 0.0135f;          // 0x6bd0b4
                    else if (char_ == 3) k = 0.0155f;                    // 0x6bd0c4
                }
                if (k >= 0.0f) {
                    a.pos.x -= n.x * k;
                    a.pos.z -= n.z * k;
                    moved = true;
                }
            }
            if (sub2 && a.cast == kCastM890) {
                a.pos.x -= n.x * 0.014f;          // 0x6bd0b8
                a.pos.z -= n.z * 0.014f;
                set330 = true;
                a.pos.y -= n.y * 0.014f;
                moved = true;
            }
        }
        if (sub2 && len(a.pos - P) < 3.0f && a.cast == kCastM890 && a.hp >= 1.0f) {
            ++eaten_;
            eaten = true;
        }
        if (moved || set330 || eaten) {
            Boss01ActorEdit ed;
            ed.index = i;
            ed.set_pos = moved;
            ed.pos = a.pos;
            ed.set_330 = set330;
            ed.f330 = 0.014f;                     // 0x3c656042
            ed.eaten = eaten;
            out_->actor_edits.push_back(ed);
            if (moved && a.cast == kCastPlayer) player_now_ = a.pos;
        }
    }
    if (!sub2) return;
    const int nb = int(in_->bubbles.size());
    for (int i = 0; i < nb; ++i) {
        const Boss01Bubble& bb = in_->bubbles[std::size_t(i)];
        if (bb.effect != 0x143 && bb.effect != 0x144) continue;
        const Vec3 d = bb.pos - P;
        const float l = len(d);
        float nx = 0, nz = 0;
        if (l != 0.0f) { nz = (1.0f / l) * d.z; nx = (1.0f / l) * d.x; }
        Vec3 q = bb.pos;
        q.x -= nx * 15.0f * 0.003f;               // 0x6bd020
        q.z -= nz * 15.0f * 0.003f;
        out_->bubble_edits.push_back({i, q, len(q - P) < 3.0f});
    }
    out_->blur_centre = P;
}

void Boss01::heal(float divisor) {
    float h = (max_hp_ / divisor) * float(eaten_) + hp_;
    hp_ = h;
    if (max_hp_ < h) hp_ = max_hp_;
}

void Boss01::state4() {
    switch (bc_) {
        case 0:
            speed(0.5f);
            set_clip(kAtk10_1, 1);
            ++bc_;
            drool_ = 0;
            se3d(207, pos_);
            eaten_ = 0;
            break;
        case 1:
            if (clip_done()) {
                out_->zoom.strength = 0.5f;
                out_->zoom.rate = -0.0025f;
                out_->zoom.snapshot = true;
                speed(1.0f);
                set_clip(kAtk10_2, 0);
                ++bc_;
                const Vec3 B = in_->bone114;
                const Vec3 at = add(B, {0, -0.65f, 1.75f});   // 0x6bd34c
                Boss01Effect& e = fx(0x148, at);
                e.rot = {-0.34906584f, 0, 0};
                e.power = 0; e.knock = 1; e.rtype = 0; e.status = 0; e.cls = -1;
                e.attach = Boss01Effect::kBone114;
                e.attach_off = {0, -0.65f, 1.75f};
                e.attach_rot = {-0.34906584f, 0, 0};
                e.attach_mode = 1;
                fx_slot_[8] = true;               // blk+0x20c
                se3d(198, at);
                out_->rumble.push_back(4);
            }
            break;
        case 2: {
            ++c0_;
            pull_actors(true);
            if (c0_ == 60) out_->rumble.push_back(bb5c_ >= 1 ? 4 : 2);
            const std::uint32_t L = clip_count_;
            if ((L > 5 && bb5c_ > 0) || (L > 4 && bb5c_ < 1)) {
                set_clip(kAtk10_3, 0);            // the inline hard set
                ++bc_;
                c0_ = 0;
                se_stop(207, 0);
                kill_fx(0x148);
                cam(1, 2, 0.03f);
            }
            break;
        }
        case 3: {
            ++c0_;
            if (c0_ < 21) pull_actors(false);
            const Vec3 B = in_->bone114;
            if (eaten_ == 0) {
                if (c0_ == 20) se3d(200, B);
            } else {
                if (c0_ == 20) se3d(199, B);
                if (c0_ - 20u < 31u && c0_ % 5 == 0) {   // spit (visual)
                    const float ang = float(50 - int(R(100))) * kDeg + (k3HalfPi - e4_);
                    const Vec3 v = rot_y({0, 0, -1.0f}, ang);
                    Boss01Effect& e = fx(0x147, add(B, v));
                    e.power = 0; e.knock = 1; e.rtype = 0; e.status = 0; e.cls = -1;
                    // [H] the effect was created: the scale draw happens only then.
                    const float sc = 0.8f - float(R(30)) / 100.0f;   // 0x6bd3a8
                    e.scale = {sc, sc, sc};
                    e.scale_after = true;
                    e.life = 20;
                }
                if (c0_ == 35) {
                    float D;
                    if (bb5c_ == 0 && bb58_) D = 50.0f;
                    else if (bb5c_ == 2) D = 15.0f;
                    else if (bb5c_ == 1) D = 20.0f;
                    else D = 30.0f;
                    heal(D);
                }
            }
            if (in_->player_took_damage) {
                if (eaten_ == 0) se3d(199, B);
                float D2;
                if (bb5c_ == 0 && bb58_) D2 = bc48_ ? 50.0f : 40.0f;
                else if (bb5c_ == 2 || bb5c_ == 1) D2 = 10.0f;
                else D2 = 15.0f;
                heal(D2);
            }
            if (clip_done()) commit(0);           // inline
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------------------
// State 5, FUN_00510730: mouth laser (§5.5). The world raycast is a host request.
void Boss01::state5() {
    const Vec3 m238 = bones_[11];
    switch (bc_) {
        case 0: {
            set_clip(kAtk07_1, 1);
            speed(1.25f);
            bc_ = 1;
            const Vec3 pl = in_->player_pos;
            aim_ = {pl.x, 1.9f, pl.z};
            if (toal()) {
                const Vec3 n = norm0({pos_.x - pl.x, 0.0f, pos_.z - pl.z});
                const Vec3 c{pl.x + 6.0f * n.x, 1.9f, pl.z + 6.0f * n.z};
                const float dx = pos_.x - c.x, dz = pos_.z - c.z;
                if (std::sqrt(dx * dx + dz * dz) < 6.0f) aim_ = {pl.x - 6.0f * n.x, 1.9f, pl.z - 6.0f * n.z};
                else aim_ = c;
            }
            aim_.y = 1.9f;
            Boss01Effect& e = fx(0x141, m238);
            e.scale = {3, 3, 3};
            e.power = 0; e.knock = 1; e.rtype = 0; e.status = 0; e.cls = -1;
            e.attach = Boss01Effect::kBone; e.attach_bone = 11; e.attach_mode = 2;
            e.life = 45;
            fx_slot_[1] = true;
            se3d(193, m238);
            break;
        }
        case 1:
            ++c0_;
            if (clip_done()) {
                set_clip(kAtk07_2, 0);
                speed(0.25f);
                bc_ = 2;
                c0_ = 0;
                tilt(20, {0.25f, 0.25f, 0.0f});
                {
                    Boss01Effect& e = fx(0x142, m238);
                    e.power = 4.0f; e.knock = 1; e.rtype = 2; e.status = 0; e.cls = -1;
                    e.attach = Boss01Effect::kBeam; e.attach_mode = 1;
                    e.flag201 = true;
                    if (toal()) e.life = 250;
                    fx_slot_[1] = true;
                }
                const float ys[2] = {2.0f, 2.5f};
                const int bs[2] = {16, 17};
                for (int k = 0; k < 2; ++k) {     // shockwaves (EF2 332: r 1.5, hh 0.5 cylinder)
                    const Vec3 q = bones_[std::size_t(bs[k])];
                    Boss01Effect& e = fx(0x14c, {q.x, ys[k], q.z});
                    e.scale = {3, 2, 3};
                    e.power = 0; e.knock = 1; e.rtype = 2; e.cls = -1;
                }
                out_->zoom.strength = 0.4f;
                out_->zoom.rate = -0.02f;
                out_->zoom.snapshot = true;
                out_->blur_centre = m238;
                se2d(194);
                se2d(192);
                out_->rumble.push_back(1);
            }
            break;
        case 2: {
            ++c0_;
            const std::uint32_t q = t1_ & 3;
            auto ray = [&](int id, bool at_aim) {
                const Vec3 d = aim_ - m238;
                Boss01Ray r;
                r.from = add(m238, norm0(d));
                r.delta = d;
                r.fx.id = id;
                r.fx.pos = aim_;
                r.fx.power = 0; r.fx.knock = 1; r.fx.rtype = 1; r.fx.status = 0; r.fx.cls = -1;
                r.at_aim = at_aim;
                out_->rays.push_back(r);
            };
            bool exit;
            if (toal()) {
                if (q == 0 && !in_->player_took_damage) ray(0x144, true);
                const float diff = std::fabs(angle_diff(e4_ + e8_, e0_));
                if (diff >= k60) se_stop(195, 1000);
                else if (c0_ - 20u < 91u && t1_ % 50 == 0) se3d(195, aim_);
                exit = c0_ > 249 || !(diff <= k60);
            } else {
                if (q == 0) ray(0x146, false);
                if (phase_ > 1) out_->shorten_146 = true;
                const float diff = std::fabs(angle_diff(e4_ + e8_, e0_));
                const std::uint32_t base = phase_ < 2 ? 40u : 30u;
                if (c0_ - base < 31u && t1_ % 50 == 0) se3d(195, aim_);
                exit = c0_ > 169 || !(diff <= k60);
            }
            if (exit) {
                ++bc_;
                e8_ = 0.0f;
                kill_fx(0x142);
                speed(1.0f);
                set_clip(kAtk07_3, 1);
                se_stop(194, 200);
                cam(1, cam_.cur, 0.03f);
            }
            break;
        }
        case 3:
            if (clip_done()) {
                commit(0);
                speed(1.0f);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// State 7, FUN_005116f0: bubble spray (§5.7). 29 bubble bullets (kind 0x1e).
void Boss01::state7() {
    const float toalb = toal() ? 0.01f : 0.0f;    // 0x6bd084
    const bool hugo = char_ == 2;
    if (bc_ == 0) {
        speed(1.0f);
        set_clip(kAtk06, 1);
        ++bc_;
        drool_ = 1;
        se3d(196, in_->player_pos);
    }
    const std::uint32_t b = std::uint32_t(++bc_);
    if (b == 35) drool_ = 0;
    if (b > 40) {
        speed(0.2f);
        ++c0_;
    }
    if (std::uint32_t(bc_) < 131) {
        const std::uint32_t c = c0_;
        const float ym = k3HalfPi - e4_;
        const Vec3 B0 = add(bones_[15], rot_y({0, 0, -1.0f}, ym));
        const float Lsw = 2.0f - float(c) / 14.0f;    // 0x6bd960
        const Vec3 dir = norm0(rot_y({Lsw, 0, -1.0f}, ym));
        auto accel_of = [&](std::uint32_t w1) {
            float a = float(-int((w1 >> 17) % 200)) / 1000000.0f;   // 0x6bdf88
            if (hugo) a = (a - 0.00025f) * 1.5f;                    // 0x6bcfc4, 0x6bd4d4
            return a;
        };
        auto bubble = [&](float f0, float f1, float f10, float f11) {
            Boss01Bullet bl;
            bl.kind = 0x1e;
            bl.effect = 0x143;
            bl.pos = add(B0, {0, -0.75f, 0});     // 0x6bd390
            bl.dir = dir;
            bl.f[0] = f0; bl.f[1] = f1; bl.f[2] = 0.0025f; bl.f[3] = 0.0f; bl.f[4] = 5.0f;
            bl.f[9] = Lsw; bl.f[10] = f10; bl.f[11] = f11;
            out_->bullets.push_back(bl);
        };
        if (c > 4 && c % 3 == 0 && c < 57) {      // E1
            const std::uint32_t w1 = draw(), w2 = draw(), w3 = draw(), w4 = draw();
            const float a = accel_of(w1);
            const float r2 = float(-int((w2 >> 17) % 15)) / 1000.0f;   // 0x6bde6c
            const float f11 = float((w3 >> 17) % 10) / 100.0f + 0.4f;
            float f0, f10;
            if (c % 6 == 0) {
                f0 = r2 + (c % 9 == 0 ? 0.075f : 0.085f);   // 0x6bd174 / 0x6bd188
                f0 = f0 + toalb;
                f10 = float((w4 >> 17) % 50) / 100.0f + 1.0f;
            } else {
                f0 = float(double(r2) + 0.1 + double(toalb));   // 0x6bd4c8 (double)
                f10 = 2.0f - float((w4 >> 17) % 50) / 100.0f;
            }
            bubble(f0, a, f10, f11);
        }
        if (c > 7 && ((c & 7) == 0 || (c & 7) == 5) && c < 54) {   // E2
            const std::uint32_t w1 = draw(), w2 = draw(), w3 = draw(), w4 = draw();
            const float a = accel_of(w1);
            const float r2 = float(-int((w2 >> 17) % 15)) / 1000.0f;
            const float f11 = float((w3 >> 17) % 10) / 100.0f + 0.4f;
            float f0, f10;
            if ((c & 7) == 0) {
                f0 = r2 + 0.08f;                  // 0x6bd180
                f10 = 2.0f - float((w4 >> 17) % 50) / 100.0f;
            } else {
                f10 = float((w4 >> 17) % 50) / 100.0f + 1.0f;
                f0 = r2 + 0.07f;                  // 0x6bd168
            }
            bubble(f0 + toalb, a, f10, f11);
        }
        if (c > 4 && (c & 3) == 1 && c < 76) {    // E3 puff (EF2 325, visual)
            const Vec3 d3 = norm0(rot_y({1.5f - float(c) / 28.0f, 0.05f, -1.0f}, ym));
            const std::uint32_t w1 = draw(), w2 = draw(), w3 = draw();
            const float sc = float((w3 >> 17) % 50) / 100.0f + 1.5f;
            // x <- w2, z <- w1, scale <- w3 [BYTES 0x5123e4..0x51257e]
            const Vec3 q{(0.1f - float((w2 >> 17) % 20) / 100.0f) + (d3.x * 2.0f + B0.x),
                         (B0.y - 1.25f + d3.y * 2.0f) + 0.0f,
                         (0.1f - float((w1 >> 17) % 20) / 100.0f) + (d3.z * 2.0f + B0.z)};
            Boss01Effect& e = fx(0x145, q);
            e.scale = {sc, sc, sc};
            e.power = 0; e.knock = 1; e.rtype = 0; e.status = 0; e.cls = -1;
        }
    } else {
        speed(1.0f);
    }
    if (clip_done()) {
        speed(1.0f);
        commit(0);                                // inline
        after06_ = 1;
        cam(1, 3, 0.01f);
    }
}

// State 8, FUN_00512630: taunt (§5.8).
void Boss01::state8() {
    if (bc_ == 0) {
        speed(1.5f);
        set_clip(kAction01, 1);
        bc_ = 1;
        se2d(203);
    } else if (bc_ == 1 && clip_done()) {
        speed(1.0f);
        commit(0);                                // inline
    }
}

// State 9, FUN_005093e0: the death timeline (§8.2). k = bc_; k = 0 is the kill frame.
void Boss01::state9() {
    const int k = bc_;
    if (k == 0) {
        out_->kill_minions = true;
        tilt(300, {0.3f, 0.3f, 0.3f});
        se3d(577, pos_);
    }
    if (k % 60 == 0 && k < 361) out_->rumble.push_back(5);
    if (k == 142) { out_->alpha_target = 0.0f; out_->alpha_step = 0.005f; }
    if (k == 440) se3d(188, pos_);
    if (k != 580) {
        ++bc_;
        return;
    }
    out_->award_exp = true;
    out_->destroy = true;
    destroyed_ = true;
    if (!time_attack_) {
        if (in_->player_hp >= 1.0f) {
            out_->flags.push_back({220, 1});
            out_->clear_buffs = true;
        }
        // FUN_00403cd0 (Steam stats) is ignored.
    } else {
        out_->flags.push_back({156, 0});
    }
}

// State 10, FUN_00509370: the intro wait.
void Boss01::state10() {
    ++bc_;
    drool_ = 0;
    if (std::uint32_t(bc_) > 60u && cam_.t == 1.0f) commit(0);
}

}  // namespace cleria
