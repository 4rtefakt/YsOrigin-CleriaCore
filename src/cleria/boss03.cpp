// Gelaldy (boss03) — see boss03.hpp and build/boss_re/GELALDY.md.
#include "cleria/boss03.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cleria {

namespace boss03 {

const ClipInfo kClipTable[kClips] = {
    // BOSS03.YMO sections (the scratch clips.py dump of §2.5 [DATA]).
    {"ALL", 15, 16, 0.25f, true},         {"WAIT1", 15, 48, 0.25f, true},
    {"RROLL", 75, 95, 0.25f, true},       {"LROLL", 120, 140, 0.25f, true},
    {"HDAMAGE", 160, 199, 0.25f, false},  {"RPUNCHI", 205, 278, 0.25f, false},
    {"LPUNCHI", 305, 378, 0.25f, false},  {"BEAT", 410, 495, 0.3f, false},
    {"LSWING", 500, 550, 0.3f, false},    {"RSWING", 600, 650, 0.3f, false},
    {"MAGUMA1", 715, 734, 0.4f, false},   {"MAGUMA2", 750, 769, 0.25f, true},
    {"MAGUMA3", 780, 805, 0.4f, false},   {"TEETH1", 850, 880, 0.4f, false},
    {"TEETH2", 900, 920, 0.3f, true},     {"TEETH3", 940, 960, 0.3f, false},
    {"TEETH4", 1001, 1049, 0.4f, false},  {"TEETH5", 1100, 1125, 0.4f, false},
    {"TEETH6", 1050, 1080, 0.4f, false},  {"LDAMAGE", 161, 194, 0.3f, false},
    {"RDAMAGE", 1161, 1194, 0.3f, false}, {"DDAMAGE", 1261, 1294, 0.3f, false},
    {"LBEAT", 1310, 1395, 0.3f, false},   {"RNAGI1", 1410, 1424, 0.3f, false},
    {"RNAGI2", 1435, 1444, 0.3f, true},   {"RNAGI3", 1455, 1464, 0.3f, false},
    {"LNAGI1", 1510, 1524, 0.3f, false},  {"LNAGI2", 1535, 1544, 0.3f, true},
    {"LNAGI3", 1555, 1564, 0.3f, false},  {"FIRE", 1600, 1650, 0.25f, false},
    {"DEMO", 1715, 1850, 0.25f, false},   {"DEAD", 2000, 2200, 0.25f, false},
};

const char* const kNodeNames[kNodes] = {
    "Core",     "RightNeji", "RightFyubi1",   "LeftNeji",     "LeftFyubi1", "RightForeArm2", "LeftForeArm2", "Saru",
    "Neck4",    "RightHand", "LeftHand",      "RightForeArm1", "LeftForeArm1", "Head",       "root",
};

const std::uint16_t kFxIdList[kFxIds] = {410, 412, 413, 415, 416, 417, 418, 419, 430, 431, 432,
                                         433, 434, 435, 436, 437, 443, 444, 377, 499, 506, 507};
const std::uint16_t kSePreload[27] = {231, 232, 233, 235, 234, 241, 242, 243, 239, 240, 244, 236, 238, 237,
                                      247, 248, 249, 250, 801, 802, 806, 804, 577, 803, 805, 208, 209};

const float kAtariS3099[kRings][2] = {
    {0.10f, -18.55f},  {-3.10f, -17.60f},  {-6.10f, -16.65f}, {-9.00f, -15.45f}, {-12.15f, -14.30f}, {-13.75f, -11.45f},
    {-15.60f, -8.80f}, {-16.65f, -5.95f},  {-18.40f, -3.20f}, {-17.95f, 0.40f},  {-17.85f, 3.45f},   {-16.90f, 6.25f},
    {-16.05f, 9.35f},  {-13.60f, 11.60f},  {-11.40f, 13.85f}, {-8.95f, 15.55f},  {-6.45f, 17.65f},   {-3.20f, 17.55f},
    {-0.10f, 17.90f},  {3.05f, 17.60f},    {6.50f, 17.60f},   {8.80f, 15.40f},   {11.40f, 13.60f},   {13.70f, 11.55f},
    {16.45f, 9.55f},   {16.80f, 6.10f},    {17.60f, 3.05f},   {18.15f, -0.25f},  {18.45f, -3.30f},   {16.65f, -6.00f},
    {15.45f, -9.00f},  {13.60f, -11.55f},  {12.15f, -14.20f}, {8.90f, -15.55f},  {5.95f, -16.90f},   {2.95f, -17.85f},
};

std::array<Vec3, kRings> atari_s3099() {
    std::array<Vec3, kRings> a{};
    for (int k = 0; k < kRings; ++k) a[std::size_t(k)] = {kAtariS3099[k][0], -1.5f, kAtariS3099[k][1]};
    return a;
}

std::string ring_name(int k) {
    char b[16];
    std::snprintf(b, sizeof b, "ring%.2d", k);
    return b;
}
std::string atari_name(int k) {
    char b[16];
    std::snprintf(b, sizeof b, "atari%.2d", k);
    return b;
}

// §4.3 [BYTES 0x520df9..0x52102e]: the stack table, then the edits.
std::array<int, kSeqLen> sequence(bool easy, int diff_index) {
    std::array<int, kSeqLen> s = {
        2, 3, 4, 2, 3, 5, 2, 3, 0xc, 2, 6,                    // phase 0, idx 0..10
        2, 0xd, 2, 3, 4, 2, 0xb, 2, 3, 0xc, 2, 6, 2, 3, 4,    // phase 1, idx 11..25
        2, 7, 2, 6, 2, 7,                                     // phase 2, idx 26..31
        2, 7, 0xc, 2, 0xb, 2, 6, 2, 7,                        // phase 3, idx 32..40
    };
    if (easy) {
        s[10] = 3;                                            // M -> P
        if (diff_index != 2) s[34] = 1;                       // F -> skip
    } else if (diff_index != 2) {
        s[34] = 1;
    } else {                                                  // Nightmare
        s[17] = 3; s[18] = 0xc; s[19] = 2; s[20] = 0xb;
        s[34] = 2; s[35] = 0xb; s[36] = 2; s[37] = 6; s[38] = 0xc;
    }
    return s;
}

}  // namespace boss03

using namespace boss03;

Boss03Statics& boss03_statics() {
    static Boss03Statics s;
    return s;
}

namespace {

// The engine's float constants (read with exe_peek --f32 / --f64).
constexpr float kPi = 3.14159274f;         // 0x6bd6ac
constexpr float kHalfPi = 1.57079637f;     // 0x6bd4e4
constexpr float k2Pi = 6.28318548f;        // 0x6bd898
constexpr float kDeg = 0.0174532924f;      // 0x6bd0dc
constexpr float kRad2Deg = 57.2957764f;    // 0x6bda88
constexpr float kDeg30 = 0.523598790f;     // 0x6bd314 (-: 0x6be018)
constexpr float kThreePiHalf = 4.71238899f;   // 0x6bd858
constexpr float kHide = -1000.0f;          // 0xc47a0000
constexpr float kFlash = 1.65f;            // 0x3fd33333

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
// The inline wrap after a fold difference (-pi 0x6be0ac / 2pi 0x6bd898).
float wrap_d(float d) {
    if (d >= 0.0f) { if (kPi < d) d -= k2Pi; }
    else if (d < -kPi) d += k2Pi;
    return d;
}
// FUN_00444660 around +-pi: into [-pi, pi).
float wrap_pi(float x) {
    if (x >= kPi) return float(std::fmod(double(x + kPi), double(k2Pi))) - kPi;
    if (x < -kPi) return float(std::fmod(double(x - kPi), double(k2Pi))) + kPi;
    return x;
}
// FUN_005eecc0(rate, cur, tgt): step cur by +-rate the way that ends nearer tgt, snap when
// within the rate; result folded to [-pi, pi]. A negative rate steps by |rate| and never snaps.
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
// The single 0..360 fold the sector loops use (>= 360: -360; < 0: +360).
float fold360(float a) {
    if (a >= 360.0f) return a - 360.0f;                       // 0x6bdc74
    if (0.0f > a) return a + 360.0f;
    return a;
}

// D3DXQuaternionRotationYawPitchRoll and D3DXQuaternionSlerp (shortest arc).
Boss03Quat quat_ypr(float yaw, float pitch, float roll) {
    const float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f);
    const float cp = std::cos(pitch * 0.5f), sp = std::sin(pitch * 0.5f);
    const float cr = std::cos(roll * 0.5f), sr = std::sin(roll * 0.5f);
    Boss03Quat q;
    q.x = cy * sp * cr + sy * cp * sr;
    q.y = sy * cp * cr - cy * sp * sr;
    q.z = cy * cp * sr - sy * sp * cr;
    q.w = cy * cp * cr + sy * sp * sr;
    return q;
}
Boss03Quat slerp(const Boss03Quat& a, Boss03Quat b, float t) {
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

// RotY(a) * (0, 0, d) (D3DXMatrixRotationYawPitchRoll(a, 0, 0) + Vec3TransformCoord).
Vec3 rot_y_z(float a, float d) { return {d * sinf_(a), 0.0f, d * cosf_(a)}; }

// The PUNCH aim / NAGI per-character tables (§2.4 [BYTES 0x52066e..0x52095b, jump tables
// 0x5210c0 / d0 / e0 / f0]): {0x4ec, 0x4e0, 0x4e4}. Row = difficulty column, col = character.
struct CharRow { int aim; float far_, near_; };
constexpr CharRow kCharEasy[4] = {{14, 0.00795000046f, 0.00980000012f}, {14, 0.00850000046f, 0.0104999999f},
                                  {13, 0.00769999996f, 0.00910000037f}, {15, 0.00999999978f, 0.0114000002f}};
constexpr CharRow kCharNormal[4] = {{15, 0.00795000046f, 0.00980000012f}, {14, 0.00850000046f, 0.0104999999f},
                                    {13, 0.00769999996f, 0.00910000037f}, {15, 0.00999999978f, 0.0114000002f}};
constexpr CharRow kCharHard[4] = {{15, 0.00795000046f, 0.00987999979f}, {15, 0.00850000046f, 0.0110299997f},
                                  {14, 0.00769999996f, 0.00989999995f}, {16, 0.00999999978f, 0.0120999999f}};
constexpr CharRow kCharNightmare[4] = {{15, 0.00795000046f, 0.00989999995f}, {15, 0.00850000046f, 0.0110499999f},
                                       {14, 0.00769999996f, 0.00989999995f}, {16, 0.00999999978f, 0.0121999998f}};

// The neck mode per clip (byte table 0x69d114): 1 track the player, 2 hold, 0 ease to 0.
constexpr std::uint8_t kNeckMode[kClips] = {0, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 1, 2, 0, 0, 0, 0};

}  // namespace

// ---------------------------------------------------------------------------
// The constructor FUN_0051ffe0 (§2.1).
Boss03::Boss03(const Boss03Setup& s) {
    st_ = s.statics ? s.statics : &boss03_statics();
    char_ = s.character;
    bb5c_ = s.diff_index;
    easy_ = s.diff_easy;
    very_easy_ = s.diff_very_easy;
    rush_ = s.boss_rush;
    flag201_ = s.flag201;
    atari_ = s.atari;
    atari_present_ = s.atari_present;
    ring_y_.fill(s.ring_y0);
    pos_ = s.pos;
    // EEX record 2 -> obj+0x94 (mov ecx, 2 @0x5200ee).
    max_hp_ = s.eex_hp;
    hp_ = s.eex_hp;
    str_ = s.eex_str;
    def_ = s.eex_def;
    rec3_ = s.rec3;
    rec4_ = float(int(s.rec4));
    level_ = s.level;
    exp_b0_ = s.exp_b0;
    // Retry softening of STR / DEF, not in boss rush; no clamp [BYTES 0x5200f8..0x5201ef].
    if (very_easy_) {
        if (!rush_) {
            const float n = float(std::pow(double(float(s.retries)), 1.25));   // FUN_006701e0, f64 0x6bd568
            str_ -= n;
            def_ -= n;
        }
    } else if (easy_ && !rush_) {
        const int n = s.retries < 20 ? (s.retries >> 1) : s.retries - 10;
        str_ -= float(n);
        def_ -= float(n);
    }
    // The five parts [BYTES 0x5201f7..0x520550].
    const bool hard_or_nm = bb5c_ == 1 || bb5c_ == 2;
    const bool hugo = char_ == 2;
    for (int i = 0; i < kParts; ++i) {
        Boss03Part& p = parts_[std::size_t(i)];
        p.node = kPartNode[i];
        p.max_hp = max_hp_;                                // the stat block copy, before the body HP write
        p.hp = hp_;
        p.str = str_;
        p.def = def_;
        if (i == 0) {
            hp_ = 1500.0f;                                 // 0x44bb8000: body HP and maxHP
            max_hp_ = 1500.0f;
            p.resist[0] = hugo ? 0.0f : (hard_or_nm ? 0.670000017f : 0.370000005f);   // 0x3f2b851f / 0x3ebd70a4
            p.resist[1] = hard_or_nm ? 0.689999998f : 0.409999996f;                    // 0x3f30a3d7 / 0x3ed1eb85
            p.resist[2] = hard_or_nm ? 0.670000017f : 0.370000005f;
            p.hp *= 10.0f;                                 // 0x6bd928: 20000 (never consulted)
        } else if (i == 1 || i == 3) {                     // the arms: their own HP (not x10)
            const float arm = bb5c_ == 1 ? 900.0f : (bb5c_ == 2 ? 1800.0f : 800.0f);   // 0x44610000 / 0x44e10000 / 0x44480000
            p.hp = arm;
            p.max_hp = arm;
            if (hard_or_nm) {
                p.resist[0] = hugo ? 0.0f : 0.5f;
                p.resist[1] = 0.5f;
                p.resist[2] = 0.5f;
            } else {
                p.resist[0] = hugo ? 0.0f : 0.349999994f;  // 0x3eb33333
                p.resist[1] = 0.389999986f;                // 0x3ec7ae14
                p.resist[2] = 0.349999994f;
            }
        } else {
            p.hp *= 10.0f;                                 // the fists: 15000, inert pools
        }
        p.power = 1.0f;                                    // +0xfc
        p.f100 = 1.0f;                                     // +0x100
    }
    // blk+0x550..0x57b = kFxIdList (the death's kill list); the SE preload (kSePreload) is the host's.
    // The difficulty table (§2.4) [BYTES 0x5205f2..0x520951].
    auto chars = [&](const CharRow* t) {
        if (char_ < 0 || char_ > 3) return;               // ja 0x52095b: left 0
        punch_aim_ = t[char_].aim;
        nagi_far_ = t[char_].far_;
        nagi_near_ = t[char_].near_;
    };
    auto table = [&](float off, int n02, int n13, int rnd, int aim, int cd, int shrt, int near, int perr) {
        dmg_off_ = off; fire_n02_ = n02; fire_n13_ = n13; pillar_rand_ = rnd; pillar_aim_ = aim; pillar_cd_ = cd;
        short_hold_ = shrt; near_steps_ = near; perrie_n_ = perr;
    };
    if (easy_) { table(20.0f, 15, 25, 20, 80, 150, 10, 2, 1); chars(kCharEasy); }
    else if (bb5c_ == 0) { table(16.0f, 20, 35, 15, 25, 90, 15, 2, 1); chars(kCharNormal); }
    else if (bb5c_ == 1) { table(16.0f, 25, 35, 15, 20, 80, 15, 3, 2); chars(kCharHard); }
    else if (bb5c_ == 2 || flag201_) { table(16.0f, 30, 40, 15, 20, 70, 18, 5, 5); chars(kCharNightmare); }
    // (else: the block stays memset 0 — the engine would then divide by 0 in FIRE.)
    // The per-attack defaults [BYTES 0x52095b..0x520aeb] are the member initialisers.
    latches_reset();
    // The core glow 377 bs_03_core01 at node Core (facing still 0 here) [BYTES 0x520af2..0x520d08].
    {
        Boss03Effect e;
        e.id = 377;
        e.rot = {0.0f, -kHalfPi - 0.0f, 0.0f};
        e.node = kNCore;
        e.slot = Boss03Effect::kCore;
        ctor_.effects.push_back(e);
        core_fx_ = true;
    }
    // facing -pi/2, yaw = fmod(3pi, 2pi) - pi, the matrix, the first bearing, clip WAIT1.
    facing_ = -kHalfPi;                                    // 0xbfc90fdb
    yaw_ = float(std::fmod(double(9.42477798f), double(k2Pi))) - kPi;   // 0x6bd90c
    bearing_ = atan2f_(s.player_pos.z - pos_.z, s.player_pos.x - pos_.x);
    ctor_.clips.push_back({kWait1, 1});
    seq_ = sequence(easy_, bb5c_);
}

Mat4 Boss03::matrix() const {
    const float c = cosf_(yaw_), s = sinf_(yaw_);
    Mat4 m;                                                // D3DXMatrixRotationY (0x67b4e4) + the position
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
    m.m[12] = pos_.x; m.m[13] = pos_.y; m.m[14] = pos_.z;
    return m;
}

// ---------------------------------------------------------------------------
// Helpers.
std::uint32_t Boss03::draw() { return in_ && in_->rng ? in_->rng() : 0u; }

float Boss03::rel() const { return wrap_d(norm_2pi(bearing_) - norm_2pi(facing_)); }

// FUN_0051f420(rate): facing = turnToward(rate, facing, bearing); yaw and matrix from it.
void Boss03::turn(float rate) {
    facing_ = turn_toward(rate, facing_, bearing_);
    set_yaw_from_facing();
}
void Boss03::set_yaw_from_facing() {
    const float x = (kPi - facing_) + kHalfPi;             // 3pi/2 - facing
    yaw_ = wrap_pi(x);
}

void Boss03::set_clip(int clip) {
    out_->clips.push_back({clip, 1});
    clip_ = clip;
    done_ = false;                                         // FUN_005f5c80 zeroes +0x1c
    frame_ = float(kClipTable[clip].start);                // [H] the section starts at its first frame
}

Boss03Effect& Boss03::fx(int id, const Vec3& p) {
    Boss03Effect e;
    e.id = id;
    e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}

// FUN_0051f280(ecx obj, edx id; name, latch, attach, power, rtype): FUN_00450410 at the
// node's world position, rot (0, -pi/2 - facing, 0), scale 1, the boss's stat block, once
// per latch Lfx[latch] (blk+0x54c+latch). Index 4 reads blk+0x550 (the low byte of 410,
// 0x9a): never 1, so it never fires [BYTES 0x51f297].
void Boss03::node_fx(int id, int n, int latch, bool attach, float power, int rtype) {
    if (latch < 0 || latch > 3 || !lfx_[std::size_t(latch)]) return;
    Boss03Effect& e = fx(id, node(n));
    e.rot = {0.0f, -kHalfPi - facing_, 0.0f};              // 0x6be078 - facing
    e.stat = Boss03Effect::kBoss;
    e.power = power;
    e.knock = 1.0f;
    e.rtype = rtype;
    if (attach) e.node = n;                                // eff+0x1a0 = the node record, +0x1d5 = 1
    lfx_[std::size_t(latch)] = false;
}

void Boss03::se_latch(int id, const Vec3& p, int latch) {
    if (!lse_[std::size_t(latch)]) return;
    lse_[std::size_t(latch)] = false;
    se3d(id, p);
}

void Boss03::shake(int frames, const Vec3& amp) {
    out_->shake.frames = frames;
    out_->shake.amp = amp;
    shake_amp_ = amp;
}

void Boss03::kill_core_slot() {
    if (!core_fx_) return;
    out_->kills.push_back({Boss03Kill::kCoreSlot, 0, out_->effects.size()});
    core_fx_ = false;
}

void Boss03::ring_set_y(int k, float y) {
    ring_y_[std::size_t(k)] = y;
    out_->rings.push_back({k, y});
}

// FUN_00527910: the atariNN marker k (wrapped once); its y is forced to -1.8 (0xbfe66666).
Vec3 Boss03::atari(int k) {
    if (k < 0) k += kRings;
    if (k >= kRings) k -= kRings;
    if (!atari_present_[std::size_t(k)]) return {0, 0, 0};
    atari_[std::size_t(k)].y = -1.8f;
    if (!atari_lowered_[std::size_t(k)]) {
        atari_lowered_[std::size_t(k)] = true;
        out_->atari_lowered.push_back(k);
    }
    return atari_[std::size_t(k)];
}

// The 10-degree sector loops (BEAT 0x5245d0, NAGI 0x526060, FIRE 0x526b20): sector 0 is
// [355, 360) + [0, 5), sector i [10i - 5, 10i + 5). -1 = none.
int Boss03::sector_of(float a) const {
    const float w = sector_w_, h = sector_w_ * 0.5f;
    if (a >= w * 36.0f - h || h > a) return 0;             // 0x6bda18 = 36
    for (int i = 1; i < kRings; ++i) {
        const float c = float(i) * w;
        if (a >= c - h && c + h > a) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Damage in.
void Boss03::add_part_damage(int i, float dmg, float dmg_hp) {
    Boss03Part& p = parts_[std::size_t(i)];
    p.dmg += dmg;
    p.hp -= dmg_hp;
    if (p.hp < 1.0f) p.hp = 0.0f;                          // [EXPORT 004faf20.c:397-398]
}

bool Boss03::part_hittable(int i) const {
    const Boss03Part& p = parts_[std::size_t(i)];
    return !destroyed_ && p.hp >= 1.0f && p.hurt_radius > 0.0f;   // [EXPORT 004faf20.c:89]
}

// ---------------------------------------------------------------------------
// One engine frame.
Boss03Output Boss03::tick(const Boss03Input& in) {
    Boss03Output out;
    if (destroyed_) return out;
    in_ = &in;
    out_ = &out;
    pose_ = nullptr;
    clip_ = in.clip;
    done_ = in.clip_done;
    frame_ = in.clip_frame;
    player_ = in.player_pos;
    player_hp_ = in.player_hp;
    f156_ = in.fight;
    perries_ = in.perrie_count;
    cam_pitch_ = in.cam_pitch;
    cam_up_ = in.cam_up;
    cam_dist_ = in.cam_dist;
    shake_amp_ = in.shake_amp;
    brain();
    in_ = nullptr;
    out_ = nullptr;
    pose_ = nullptr;
    return out;
}

// FUN_00521130 (§4.1) [BYTES 0x521130..0x521593].
void Boss03::brain() {
    if (!f156_) return;                                    // idle: no driver, no tail, no rings
    if (hp_ < 1.0f) {
        if (hp_ == 0.0f) death();                          // (0 < HP < 1: nothing; unreachable [H])
    } else {
        driver();
        if (intro_ < 120) {                                // the 2 s intro hold (G6)
            ++intro_;
            if (clip_ != kWait1) set_clip(kWait1);
        } else if (phase_ == kPhaseSpecial) {              // table 0x521594
            switch (special_) {
                case 8: case 10: special_regrow(special_); break;
                case 9: arm_break(); break;
                case 11: nagi(); break;                    // unreachable (G3)
                default: break;
            }
        } else {                                           // table 0x5215a4
            switch (code()) {
                case kCodeNop: break;                      // FUN_00523860
                case kCodeSkip: next_attack(); break;
                case kCodeRoll: roll(); break;
                case kCodePunch: punch(); break;
                case kCodeSwing: swing(); break;
                case kCodeBeat: beat(); break;
                case kCodeMaguma: maguma(); break;
                case kCodeTeeth: teeth(); break;
                case kCodeNagi: nagi(); break;
                case kCodeFire: fire(); break;
                case kCodeBeatPattern:
                    wave_var_ = (bb5c_ == 1 || bb5c_ == 2) ? 2 : (flag201_ ? 4 : 1);
                    beat();
                    break;
                default: break;                            // 8..10: no case
            }
        }
        if (wave_on_) wave_tick();
    }
    if (unsigned(c4ac_) < 550u || hp_ != 0.0f) {           // alive, or the death until its end
        pose_ = &out_->pose.emplace();
        ring_motion();
        if (clip_ == kDDamage) {
        } else if (clip_ == kRDamage) {                    // the right arm's own clip: left pose only
            arm_pose(1);
        } else if (clip_ == kLDamage) {
            arm_pose(0);
        } else {
            arm_pose(0);
            arm_pose(1);
        }
        tail();
    }
}

// ---------------------------------------------------------------------------
// FUN_005227e0: the parts, the damage, the breaks, the stagger, the arm gauge (§2.3, §3.2)
// [BYTES 0x5227e0..0x523413].
void Boss03::driver() {
    obj11c_ ^= 8u;
    bool hit[2] = {false, false};
    const int c = code();
    for (int i = 0; i < kParts; ++i) {
        Boss03Part& p = parts_[std::size_t(i)];
        const Vec3 n = node(kPartNode[i]);
        p.pos = n;
        p.hurt_centre = n;
        p.centre = n;
        p.hurt_radius = 0.0f;
        p.radius = 0.0f;
        p.power = 0.0f;
        p.f100 = 1.0f;
        p.rtype = 2;
        p.flags = 0;
        p.armed = false;
        p.shape = 0;
        bool arm_attack = false;
        if (i == 0) {
            p.hurt_radius = 1.0f;
            if (core_guard_) p.hurt_centre.y = kHide;
        } else if (i == 1 || i == 3) {
            p.hurt_radius = 2.9f;                          // 0x4039999a
            if (c == kCodePunch) p.hurt_centre.y += 0.05f; // 0x6bd13c (cosmetic)
            if (guard_[0]) parts_[1].hurt_centre.y = kHide;
            if (guard_[1]) parts_[3].hurt_centre.y = kHide;
            if (strike_ && (c == kCodeBeat || c == kCodeBeatPattern)) {   // the BEAT contact cylinders
                p.armed = true;
                p.centre.y -= 1.0f;
                p.radius = 3.0f;
                p.shape = 1;
                p.half_h = 3.0f;
                arm_attack = true;
            }
        } else {
            p.hurt_radius = 0.0f;
            p.hurt_centre.y = 100.0f;                      // 0x42c80000
            if (strike_ && c == kCodePunch) {              // dead branch: nothing sets 0x526 in PUNCH (§5.2)
                p.radius = 2.2f;                           // 0x400ccccd
                p.shape = 1;
                p.half_h = 3.5f;                           // 0x40600000
                p.centre.y -= 1.0f;
                arm_attack = true;                         // (no +0x19c)
            }
        }
        if (arm_attack) {
            p.rtype = 2;
            p.flags |= 2u;
            p.power = 10.0f - dmg_off_;                    // 0x6bd928 - 0x590
        }
        // Consume +0x134.
        const float d = p.dmg;
        if (d == 0.0f) continue;                           // (NaN goes on, as the lahf test)
        flash_ = kFlash;
        p.dmg = 0.0f;
        if (!(d > 0.0f)) continue;
        if (i == 0) {                                      // the core -> the body HP
            hp_ -= d;
            acc_ = int(float(double(acc_)) + d);           // FUN_0066f350 (ftol)
            if (!(0.0f < player_hp_)) hp_ = 1.0f;          // the boss cannot die while the player is dead
            else if (!(0.0f < hp_)) out_->player_iframes = true;   // player +0x734 = 9999
            if (float(acc_) >= max_hp_ * 0.25f) {          // 0x6bd244: 375
                acc_ = 0;
                if (!alive_[1] || !alive_[0]) {
                    if (c == kCodeTeeth) enter_special(10);   // TEETH interrupted: regrow at once
                    else stagger_ = true;                  // special 8 at the next attack boundary
                }
            }
        } else if (i == 1 || i == 3) {                     // an arm: the SECOND subtraction (G9)
            const int a = i == 1 ? 0 : 1;
            hit[a] = true;
            p.hp -= d;
            if (guard_[std::size_t(a)]) {                  // a guarded arm that took damage drops to 1 HP
                p.hp -= 0.0f;                              // (literal: `HP != 0 -> HP = 1.0`) [BYTES 0x522cc3..0x522d04]
                if (p.hp != 0.0f) p.hp = 1.0f;
            }
            flash_ = 0.0f;                                 // arm hits do not flash the body
            const int fa = a == 0 ? kNRightForeArm1 : kNLeftForeArm1;
            Boss03Effect& e = fx(431, node(fa));           // bs_03_blood, inline, stat obj
            e.rot = {0.0f, -kHalfPi - facing_, 0.0f};
            se3d(239, node(fa));
            if (p.max_hp != p.hp) {                        // the pose targets (§3.6)
                const float m100 = p.max_hp / 100.0f, k4 = screw_rate_ / 100.0f, k5 = slide_rate_ / 100.0f;   // 0x6bdafc
                const float h = 0.0f < p.hp ? p.hp : 0.001f;   // 0x6bcff0
                const float lost = (p.max_hp - h) / m100;
                slide_tgt_[std::size_t(a)] = lost * k5;
                screw_tgt_[std::size_t(a)] = lost * k4 - screw_base_[std::size_t(a)];
            }
            if (!(0.0f < p.hp)) {                          // ARM BREAK
                enter_special(9);
                loops_ = 0;
            }
        }
    }
    // The arm mini-gauge 0x76c1cc.. [BYTES 0x5232f6..0x523401]: the right one wins a tie.
    if (gauge0_) arm_gauge_.timer = gauge0_->timer;        // the word the HUD tick (FUN_0043a540) also counts down
    if (hit[0] || hit[1]) {
        const int a = hit[0] ? 0 : 1;
        const Boss03Part& p = parts_[a == 0 ? 1 : 3];
        arm_gauge_.right = a == 0;
        arm_gauge_.timer = 255;
        arm_gauge_.hp = p.hp;
        arm_gauge_.max = p.max_hp;
        if (p.hp <= 0.0f) arm_gauge_.timer = 0;            // (both tests are ordered: NaN keeps 255)
    }
    if (arm_gauge_.timer != 0) {
        --arm_gauge_.timer;                                // the driver's own -1 [EXPORT 005227e0.c:431-432]
        arm_gauge_.pos = parts_[arm_gauge_.right ? 1 : 3].pos;
    }
    if (gauge0_ && (hit[0] || hit[1] || gauge0_->timer != 0))
        *gauge0_ = WorldGauge{arm_gauge_.timer, arm_gauge_.hp, arm_gauge_.max, arm_gauge_.pos};
    out_->arm_gauge = arm_gauge_;
}

// ---------------------------------------------------------------------------
// FUN_0051ea30: the next attack (§4.2) [BYTES 0x51ea30..0x51ee0c].
void Boss03::next_attack() {
    if (stagger_) {                                        // pending stagger -> regrow
        stagger_ = false;
        enter_special(8);
        return;
    }
    const float half = max_hp_ * 0.5f;                     // 0x6bd304
    if (!alive_[1] && !alive_[0]) {                        // both arms broken: the core is open
        if (half >= hp_) {
            if (phase_ != 2) {                             // from phase 2 it STAYS 2, 0x527 untouched (literal)
                if (phase_ != 3) loops_ = 0;
                core_guard_ = false;
                phase_ = 3;
            }
        } else {
            if (phase_ != 2) loops_ = 0;
            core_guard_ = false;
            phase_ = 2;
        }
    } else {
        if (half >= hp_) {
            if (phase_ != 1) loops_ = 0;
            core_guard_ = true;
            phase_ = 1;
        } else {
            if (phase_ != 0) loops_ = 0;
            core_guard_ = true;
            phase_ = 0;
        }
    }
    ++idx_;                                                // clamp to the phase range (table 0x51ee10)
    switch (phase_) {
        case 0: if (idx_ >= 11) { ++loops_; idx_ = 0; } break;
        case 1: if (idx_ >= 26) { ++loops_; idx_ = 11; } else if (idx_ < 11) idx_ = 11; break;
        case 2: if (idx_ >= 32) { ++loops_; idx_ = 26; } else if (idx_ < 26) idx_ = 26; break;
        case 3: if (idx_ >= 41) { ++loops_; idx_ = 32; } else if (idx_ < 32) idx_ = 32; break;
        default: break;
    }
    const int lim = (phase_ == 2 || phase_ == 3) ? 1 : 2;
    if (loops_ >= lim) {
        if (!alive_[1] || !alive_[0]) enter_special(8);    // an arm is broken: regrow
        loops_ = 0;
    }
    if (seq_[std::size_t(idx_)] == kCodeBeat) wave_var_ = 0;
    else wave_on_ = false;                                 // the ring wave stops unless the next is BEAT
    if (phase_ != 2 && phase_ != 3) acc_ = 0;              // (phase re-read: 4 after a special)
    c4ac_ = c4b0_ = c4b4_ = 0;
    strike_ = false;
    nagi_m_ = 0.0f;
    guard_ = {false, false};
    c4c0_ = sub_ = 0;
    // The arm pose fold, and both Neji quaternions halfway to identity [BYTES 0x51ecca..0x51edd3].
    screw_base_[0] = screw_[0] + screw_base_[0];
    screw_[0] = 0.0f;
    screw_tgt_[0] = 0.0f;
    screw_tgt_[1] = 0.0f;
    screw_base_[1] = screw_[1] + screw_base_[1];
    screw_[1] = 0.0f;
    neji_q_[0] = slerp(neji_q_[0], Boss03Quat{}, 0.5f);
    neji_q_[1] = slerp(neji_q_[1], Boss03Quat{}, 0.5f);
    latches_reset();
}

// FUN_0051e880(n): enter special n (§3.5) [BYTES 0x51e880..0x51ea25].
void Boss03::enter_special(int n) {
    special_ = n;
    c4ac_ = c4b0_ = 0;
    nagi_m_ = 0.0f;
    c4b4_ = 0;
    strike_ = false;
    phase_ = kPhaseSpecial;
    c4c0_ = sub_ = 0;
    acc_ = 0;
    latches_reset();
    out_->sounds.push_back({Boss03Sound::kStop, 242, {}, 100});   // the NAGI loops (sound on only)
    out_->sounds.push_back({Boss03Sound::kStop, 243, {}, 100});
    kill_id(437);                                          // every live 437 bs_03_fire
}

// Specials 8 / 10: the stagger and the regrowth (§3.4) [BYTES 0x5211d5..0x5213c2].
void Boss03::special_regrow(int n) {
    guard_ = {true, true};
    core_guard_ = true;
    if (sub_ == 0) {
        const int want = n == 8 ? kTeeth5 : kTeeth4;
        if (clip_ != want) set_clip(want);
        if (done_ || clip_ != want) {
            if (!alive_[0]) {
                alive_[0] = true;
                slide_[0] = slide_tgt_[0] = screw_[0] = screw_base_[0] = screw_tgt_[0] = 0.0f;
                parts_[1].hp = parts_[1].max_hp;
            }
            if (!alive_[1]) {
                alive_[1] = true;
                slide_[1] = slide_tgt_[1] = screw_[1] = screw_base_[1] = screw_tgt_[1] = 0.0f;
                parts_[3].hp = parts_[3].max_hp;
            }
            se_latch(248, pos_, 0);
            sub_ = 2;
        }
    } else if (sub_ == 2) {
        if (clip_ != kTeeth6) set_clip(kTeeth6);
        if (done_ || clip_ != kTeeth6) next_attack();
    }
}

// FUN_00525300: special 9, the ARM BREAK (§5.10) [BYTES 0x525300..0x52577f].
void Boss03::arm_break() {
    guard_ = {true, true};
    const float hR = parts_[1].hp, hL = parts_[3].hp;
    if (0.0f < hR) {                                       // the LEFT arm broke
        c4c0_ = kLDamage;
        alive_[1] = false;
    } else if (0.0f < hL) {
        c4c0_ = kRDamage;
        alive_[0] = false;
    } else if (alive_[0] && alive_[1]) {
        c4c0_ = kDDamage;
        alive_[0] = alive_[1] = false;
    } else if (alive_[0]) {
        c4c0_ = kRDamage;
        alive_[0] = false;
    } else if (alive_[1]) {
        c4c0_ = kLDamage;
        alive_[1] = false;
    }                                                      // (else 0x4c0 unchanged)
    if (clip_ != c4c0_ && c4c0_ != 0) {
        se_latch(240, node(c4c0_ == kRDamage ? kNRightHand : kNLeftHand), 1);
        set_clip(c4c0_);
    }
    if (!alive_[0] && !alive_[1] &&
        (frame_in(1165, 1170) || frame_in(165, 170) || frame_in(1265, 1270)))
        core_exposed_ = true;                              // the core02 glow (§11.2)
    if (frame_in(173, 178) || frame_in(1273, 1278)) node_fx(432, kNLeftForeArm1, 0, false, 0.0f, 3);
    if (frame_in(1169, 1174) || frame_in(1267, 1272)) node_fx(432, kNRightForeArm1, 1, false, 0.0f, 3);
    if (frame_in(180, 185) || frame_in(1280, 1285)) {      // the hand falls into the lava
        node_fx(419, kNLeftHand, 2, false, 0.0f, 3);
        se_latch(806, node(kNLeftHand), 0);
        if (lrb_[0]) { lrb_[0] = false; out_->rumble.push_back(0); }
    }
    if (frame_in(1179, 1184) || frame_in(1276, 1281)) {
        node_fx(419, kNRightHand, 3, false, 0.0f, 3);
        se_latch(806, node(kNRightHand), 0);
        if (lrb_[0]) { lrb_[0] = false; out_->rumble.push_back(0); }
    }
    if (done_ || clip_ != c4c0_) next_attack();
}

// ---------------------------------------------------------------------------
// The attacks (§5).

// Code 2 ROLL, FUN_00523880 [BYTES 0x523880..0x523b13]: turn to face the player.
void Boss03::roll() {
    const float d = rel();
    const float k = turn_rate_ / 360.0f;                   // 0x6bdc74
    float g = st_->roll_acc;                               // 0x7d9b94
    float rate;
    if (0.0f > d) {
        g -= 0.0001f;                                      // 0x6bcfb4
        rate = float(-0.005 - double((d * -57.2957764f) * k));   // f64 0x6be050, 0x6be128
    } else {
        g += 0.0001f;
        rate = float(double((d * kRad2Deg) * k) + 0.005);  // f64 0x6bd400
    }
    st_->roll_acc = g;
    turn(rate + g);
    if (c4ac_ == 0) {
        const float d1 = rel();
        if (d1 >= -kDeg30 && kDeg30 >= d1) {
            next_attack();
            st_->roll_acc = 0.0f;
        }
    } else {
        --c4ac_;
    }
    const float d2 = rel();
    if (!(d2 >= -kDeg30 && kDeg30 >= d2)) {
        const int want = 0.0f > rel() ? kRRoll : kLRoll;
        if (clip_ != want) {
            set_clip(want);
            c4ac_ = 20;
        }
    }
}

// Code 3 PUNCH, FUN_00523b30 [BYTES 0x523b30..0x523fc2].
void Boss03::punch() {
    if (!alive_[1] && !alive_[0]) { next_attack(); return; }
    if (clip_ != kRPunchi && clip_ != kLPunchi) {
        const float d = rel();
        int side;
        if (0.0f > d) {
            side = alive_[1] ? 2 : 1;
            c4c0_ = alive_[1] ? kLPunchi : kRPunchi;
        } else if (alive_[0]) {
            c4c0_ = kRPunchi;
            side = 1;
        } else {
            c4c0_ = kLPunchi;
            side = 2;
        }
        st_->punch_side = side;                            // 0x7ddc28
        set_clip(c4c0_);
        guard_ = {true, true};
    }
    // Aim during the wind-up: F in [205, 205 + 0x4ec] or [305, 305 + 0x4ec].
    const int r_hi = punch_aim_ + 205, l_hi = punch_aim_ + 305;
    if ((r_hi >= 205 && frame_ >= 205.0f && float(r_hi) >= frame_) ||
        (l_hi >= 305 && frame_ >= 305.0f && float(l_hi) >= frame_)) {
        if (!rel_within(-kDeg, kDeg)) turn(turn_rate_);
    }
    if (!(frame_in(215, 227) || frame_in(315, 327))) strike_ = false;
    if (frame_in(206, 216) || frame_in(306, 316)) {
        const int fa = clip_ == kRPunchi ? kNRightForeArm2 : kNLeftForeArm2;
        node_fx(430, fa, 0, true, 15.0f - dmg_off_, 2);   // 0x6bd964 - 0x590
        se_latch(244, node(fa), 0);
    }
    if (frame_in(223, 226) || frame_in(323, 326)) {
        const int side = st_->punch_side;                  // byte 0x527 + side
        if (side == 0) core_guard_ = false;
        else if (side == 1) guard_[0] = false;
        else if (side == 2) guard_[1] = false;
        zoom(0.1f, -0.05f);                                // 0x3dcccccd / 0xbd4ccccd
        se_latch(231, node(clip_ == kRPunchi ? kNRightHand : kNLeftHand), 1);
    }
    if (done_ || clip_ != c4c0_) next_attack();
}

// Code 4 SWING, FUN_00523fd0 [BYTES 0x523fd0..0x52423b]: the whole-arena floor sweep.
void Boss03::swing() {
    guard_ = {true, true};
    if (!alive_[1] && !alive_[0]) { next_attack(); return; }
    if (clip_ != kLSwing && clip_ != kRSwing) {
        if (alive_[1]) c4c0_ = alive_[0] ? 8 + int((draw() >> 17) & 1u) : kLSwing;   // inline: ONE draw
        else c4c0_ = kRSwing;
        set_clip(c4c0_);
        se_latch(802, pos_, 1);
    }
    if (frame_in(515, 530) || frame_in(615, 630)) {
        strike_ = true;
        se_latch(232, pos_, 0);
    } else {
        strike_ = false;
    }
    if (frame_in(517, 527) || frame_in(617, 627))
        node_fx(clip_ != kRSwing ? 417 : 416, kNRoot, 0, false, -(dmg_off_ * 0.5f), 1);
    if (done_ || clip_ != c4c0_) next_attack();
}

// Codes 5 / 0xd BEAT, FUN_00524240 [BYTES 0x524240..0x524921]: the slam and the ring wave.
void Boss03::beat() {
    if (!alive_[1] && !alive_[0]) { next_attack(); return; }
    if (clip_ != kBeat && clip_ != kLBeat) {
        bool right;
        if (alive_[1]) right = alive_[0] ? (draw() & 0x20000u) != 0 : false;   // inline: bit 17 of ONE draw
        else right = alive_[0];
        if (right) { guard_[1] = true; c4c0_ = kBeat; }
        else { guard_[0] = true; c4c0_ = kLBeat; }
        set_clip(c4c0_);
        se_latch(245, node(c4c0_ == kBeat ? kNRightHand : kNLeftHand), 0);
        guard_ = {true, true};
        ring_latch_.fill(true);
        wave_frame_ = wave_step_ = 0;
    }
    if (frame_in(430, 435) || frame_in(1330, 1335)) {
        strike_ = true;                                    // the driver arms both arm parts next frame
        if (frame_in(431, 435) || frame_in(1331, 1335)) {
            beat_pending_ = true;
            guard_ = {false, false};
            shake(5, {0.2f, 1.0f, 0.2f});
            zoom(0.5f, -0.025f);
            se_latch(233, node(clip_ == kBeat ? kNRightHand : kNLeftHand), 1);
            if (lrb_[0]) { lrb_[0] = false; out_->rumble.push_back(3); }
        }
    } else {
        strike_ = false;
    }
    if (beat_pending_) {                                   // the ring in front of the boss
        const float a = fold360(-((facing_ + kHalfPi) * kRad2Deg));
        const int i = sector_of(a);
        if (i >= 0) {
            if (ring_latch_[std::size_t(i)]) {
                ring_latch_[std::size_t(i)] = false;
                fx(413, atari(i));                         // bs_03_beat (inline, rot 0, power 0, rtype 0)
            }
            ring_state_[std::size_t(i)] = 2;
            wave_base_ = i;
            ring_394_[std::size_t(i)] = true;
            beat_pending_ = false;
            wave_on_ = true;                               // the wave starts
        }
    }
    if (frame_in(430, 435)) node_fx(418, kNRoot, 0, false, 0.0f, 3);     // bs_03_zanzou
    if (frame_in(1330, 1335)) node_fx(435, kNRoot, 0, false, 0.0f, 3);   // bs_03_zanzou2
    if (done_ || clip_ != c4c0_) next_attack();
}

// Code 6 MAGUMA, FUN_00524930 [BYTES 0x524930..0x524f0d]: magma spit and the Perrie summon.
void Boss03::maguma() {
    if (sub_ == 0) {
        if (clip_ != kMaguma1) {
            set_clip(kMaguma1);
            se_latch(235, node(kNSaru), 0);
        }
        if (done_ || clip_ != kMaguma1) {
            sub_ = 1;
            lfx_.fill(true);
        }
    } else if (sub_ == 1) {
        if (clip_ != kMaguma2) set_clip(kMaguma2);
        if (frame_in(752, 755) && lfx_[0]) {
            lfx_[0] = false;
            const Vec3 S = node(kNSaru);
            Boss03Effect& e = fx(415, S);                  // bs_03_gero2 (inline)
            e.rot = {0.0f, -kHalfPi - facing_, 0.0f};
            e.power = -dmg_off_;
            e.rtype = 0;
            for (int i = 0; i < perrie_n_; ++i) {
                if (perries_ > 5) continue;                // FUN_005279f0: refuse at 6 alive
                float ox = 0.0f, oz = 0.0f;
                if (perrie_n_ == 2) {
                    const double off = double(i) - 0.5;    // f64 0x6bd518
                    ox = float(double(sinf_(facing_)) * off + 0.0);
                    oz = float(0.0 - double(cosf_(facing_)) * off);
                } else if (perrie_n_ == 3) {
                    const float off = float(i - 1);
                    ox = sinf_(facing_) * off + 0.0f;
                    oz = 0.0f - cosf_(facing_) * off;
                }                                          // (1 and 5: one point)
                float a = -((facing_ - kHalfPi) * kRad2Deg);
                while (a >= 360.0f) a -= 360.0f;
                while (0.0f > a) a += 360.0f;
                Boss03Spawn sp;
                sp.yaw_deg = int(a);                       // cvttss2si
                sp.pos = {S.x + ox, S.y - 3.0f, S.z + oz}; // 0x6bd680
                sp.level = level_;
                out_->spawns.push_back(sp);
                ++perries_;
            }
        }
        if (done_ || clip_ != kMaguma2) sub_ = 2;          // after one loop
    } else if (sub_ == 2) {
        if (clip_ != kMaguma3) set_clip(kMaguma3);
        if (done_ || clip_ != kMaguma3) next_attack();
    }
}

// Code 7 TEETH, FUN_00524f10 [BYTES 0x524f10..0x52514a]: the bite and the 200-frame hold.
void Boss03::teeth() {
    if (sub_ == 0) {
        if (clip_ != kTeeth1) set_clip(kTeeth1);
        if (frame_in(850, 855) && !rel_within(-kDeg, kDeg)) turn(turn_rate_);   // FUN_0051f190(-1, 1)
        if (frame_in(871, 876)) {
            node_fx(412, kNSaru, 0, false, float(-(double(dmg_off_) / 3.5)), 2);   // f64 0x6bd5c0
            se_latch(241, node(kNSaru), 0);
            if (lrb_[0]) { lrb_[0] = false; out_->rumble.push_back(1); }
        }
        if (done_ || clip_ != kTeeth1) sub_ = 1;
    } else if (sub_ == 1) {
        if (clip_ != kTeeth2) set_clip(kTeeth2);
        if (unsigned(++c4ac_) >= 200u) {
            c4ac_ = 0;
            sub_ = 2;
        }
    } else if (sub_ == 2) {
        if (clip_ != kTeeth3) set_clip(kTeeth3);
        if (done_ || clip_ != kTeeth3) next_attack();
    }
}

// Code 0xb NAGI, FUN_00525940 [BYTES 0x525940..0x526506]: the spinning lava sweep.
void Boss03::nagi() {
    const float delta = rel();
    if (c4b0_ == 0) {
        ring_latch_.fill(true);
        st_->nagi_right = delta > 0.0f;                    // seta byte 0x7ddc2c
        wave_frame_ = wave_step_ = 0;
        ++c4b0_;
    }
    const bool R = st_->nagi_right;
    if (sub_ == 0) {
        c4c0_ = R ? kRNagi1 : kLNagi1;
        if (clip_ != c4c0_) {
            set_clip(c4c0_);
            se_latch(802, pos_, 3);
        }
        if (done_ || clip_ != c4c0_) sub_ = 1;
        return;
    }
    if (sub_ == 2) {
        c4c0_ = R ? kRNagi3 : kLNagi3;
        if (clip_ != c4c0_) set_clip(c4c0_);
        if (done_ || clip_ != c4c0_) next_attack();
        return;
    }
    if (sub_ != 1) {                                       // any other sub [BYTES 0x525a4b]
        next_attack();
        return;
    }
    c4c0_ = R ? kRNagi2 : kLNagi2;
    int n;
    if (clip_ != c4c0_) {
        set_clip(c4c0_);
        strike_ = true;
        float dd = float(std::fabs(double(delta))) * kRad2Deg;
        if (100.0f > dd) dd = dd - (20.0f - dd * 0.1f);    // 0x6bd994 / 0x6bd19c
        nagi_m_ = dd * 0.006f + 1.0f;                      // 0x6bd04c
        shake(1, {0.0f, 0.2f, 0.0f});
        n = 1;
    } else {
        n = st_->nagi_n + 1;
    }
    st_->nagi_n = n;
    const int arm = R ? 0 : 1;
    const int stump = R ? kNRightForeArm1 : kNLeftForeArm1;
    const int hand = R ? kNRightHand : kNLeftHand;
    const bool win = R ? frame_in(1435, 1436) : frame_in(1535, 1536);
    if (!alive_[std::size_t(arm)]) {                       // the stump burns
        if (win) {
            node_fx(437, stump, 0, true, 0.0f, 2);
            se_latch(801, node(stump), 1);
        }
        se_latch(243, node(stump), 0);
    } else {
        if (win) se_latch(801, node(hand), 1);
        se_latch(242, node(hand), 0);
    }
    if (n % 100 == 0) lse_[0] = true;                      // the loop SE re-armed
    float t = std::max(0.0f, dist_ - 15.5f);               // 0x6bd968
    t = std::min(4.2f, t);                                 // 0x6bd7e8
    const float span = nagi_near_ - nagi_far_;
    nagi_w_ = nagi_m_ * ((span - span * 0.25f * t) + nagi_far_);
    out_->shake.frames = 10;                               // 0x76a5ac = 10 (frames only)
    float f = R ? facing_ + nagi_w_ : facing_ - nagi_w_;
    if (f >= kThreePiHalf) f -= k2Pi;
    else if (-kHalfPi > f) f += k2Pi;
    facing_ = f;
    set_yaw_from_facing();
    const float s = fold360(-((facing_ + kHalfPi) * kRad2Deg));
    for (int i = 0; i < kRings; ++i) {
        if (sector_of(s) != i || unsigned(c4ac_) > 18u) continue;
        if (ring_latch_[std::size_t(i)]) {
            ring_latch_[std::size_t(i)] = false;
            ++c4ac_;
            Boss03Effect& e = fx(436, atari(i));           // bs_03_yuka2 (inline)
            e.rot = {0.0f, -facing_, 0.0f};
            e.power = 5.0f - dmg_off_;                     // 0x6bd868
            e.rtype = 2;
            out_->rumble.push_back(0);
        }
        ring_394_[std::size_t(i)] = false;
        ring_state_[std::size_t(i)] = 2;
        ring_hold_[std::size_t(i)] = nagi_hold_;           // re-armed every frame it is faced
    }
    if (unsigned(c4ac_) >= 19u) {
        out_->shake.frames = 0;
        c4ac_ = 0;
        sub_ = 2;
        strike_ = false;
        out_->sounds.push_back({Boss03Sound::kFade, 242, {}, 50});
        out_->sounds.push_back({Boss03Sound::kFade, 243, {}, 50});
        se3d(801, node(stump));
        kill_id(437);
    }
}

// FIRE's pillar (0x52685f / 0x526c03): cooldown, the delayed SE 238, effect 444, SE 237.
void Boss03::pillar(int c, float theta) {
    st_->pillar_cd[std::size_t(c)] = pillar_cd_;
    se238_[std::size_t(c)] = 70;
    const Vec3 Q = atari(c);
    Boss03Effect& e = fx(444, Q);                          // bs_03_laser2 (inline)
    e.rot = {0.0f, -theta, 0.0f};
    e.power = -dmg_off_;
    e.rtype = 0;
    lse_[1] = false;                                       // byte 0x57d = 1, then its own test clears it
    se3d(237, Q);
}

// Code 0xc FIRE, FUN_00526510 [BYTES 0x526510..0x527193]: breath and lava pillars.
void Boss03::fire() {
    guard_ = {true, true};
    if (sub_ == 0) {                                       // the inhale
        if (clip_ != kFire) set_clip(kFire);
        if (frame_in(1609, 1630)) {
            node_fx(443, kNRoot, 0, false, 0.0f, 2);       // bs_03_laser1: no hit emitter
            se_latch(236, node(kNSaru), 0);
        }
        // Tested BEFORE the ease (bug: may skip the breath) [BYTES 0x5270f9..0x527177].
        const bool ok = -0.226892799f >= cam_pitch_ && cam_up_ >= 3.0f;   // 0x6bdff0 / 0x6bd680
        cam_pitch_ = (-0.244346097f - cam_pitch_) / 25.0f + cam_pitch_;   // 0x6bdff4 / 0x6bd9c0
        cam_up_ = (4.0f - cam_up_) / 25.0f + cam_up_;      // 0x6bd7c0
        out_->camera.pitch = cam_pitch_;
        out_->camera.up = cam_up_;
        if (ok) sub_ = 1;
    } else if (sub_ == 1) {
        if (frame_in(1624, 1632) && lrb_[0]) { lrb_[0] = false; out_->rumble.push_back(3); }
        if (frame_in(1625, 1632)) {
            zoom(0.5f, -0.05f);
            shake(10, {0.2f, 0.2f, 0.0f});
        }
        if (frame_in(1633, 1640)) sub_ = 2;
    } else if (sub_ == 2) {
        if (done_ || clip_ != kFire) {
            if (clip_ != kWait1) set_clip(kWait1);
        }
        for (int& c : st_->pillar_cd) { c -= 1; if (c <= 0) c = 0; }
        ++c4ac_;
        if (pillar_rand_ != 0 && unsigned(c4ac_) % unsigned(pillar_rand_) == 0u) {   // RANDOM pillar
            int L[kRings];
            int n = 0;
            for (int k = 0; k < kRings; ++k)
                if (ring_state_[std::size_t(k)] != 3) L[n++] = k;
            if (n != 0) {
                for (int tries = 0; tries < 20; ++tries) {
                    const int r = int((draw() >> 17) % std::uint32_t(n));
                    const int prev = r - 1 < 0 ? n - 1 + r : r - 1;
                    const int next = r + 1 < n ? r + 1 : r + 1 - n;
                    const int c = L[r];
                    if (st_->pillar_cd[std::size_t(c)] != 0) continue;
                    const int a = st_->pillar_cd[std::size_t(L[prev])];
                    const int b = st_->pillar_cd[std::size_t(L[next])];
                    const int half = pillar_cd_ / 2;
                    if (a != 0 && b != 0) continue;
                    if ((a == 0 || a <= half) && (b == 0 || b <= half)) {
                        const Vec3 Q = atari(c);
                        // (the pillar re-reads the marker: atari() is idempotent)
                        pillar(c, atan2f_(Q.z - pos_.z, Q.x - pos_.x));
                        break;
                    }
                }
            }
        }
        if (pillar_aim_ != 0 && unsigned(c4ac_) % unsigned(pillar_aim_) == 0u) {     // AIMED pillar
            const float tp = atan2f_(player_.z - pos_.z, player_.x - pos_.x);
            const float a = fold360(-((tp + kHalfPi) * kRad2Deg));
            for (int i = 0; i < kRings; ++i) {
                if (sector_of(a) != i) continue;
                if (ring_state_[std::size_t(i)] == 3 || st_->pillar_cd[std::size_t(i)] != 0) continue;
                int j = i - 1;                             // the previous ring not held
                for (;;) {
                    const int jj = j < 0 ? j + kRings : j;
                    if (ring_state_[std::size_t(jj)] != 3) { j = jj; break; }
                    j = jj - 1;
                }
                // The next-ring scan tests the wrong variable (state[j], never 3): k = i + 1
                // [BYTES 0x526bd1..0x526be9].
                const int k = i + 1 < kRings ? i + 1 : i + 1 - kRings;
                if (st_->pillar_cd[std::size_t(j)] == 0 || st_->pillar_cd[std::size_t(k)] == 0) pillar(i, tp);
            }
        }
        int N = 0;                                         // (phase 4: a stale stack slot; FIRE never runs there)
        if (phase_ == 0 || phase_ == 2) N = fire_n02_;
        else if (phase_ == 1 || phase_ == 3) N = fire_n13_;
        if (unsigned(c4ac_) >= unsigned(20 * N)) {
            c4ac_ = 0;
            st_->pillar_cd.fill(0);
            next_attack();
        }
    }
}

// ---------------------------------------------------------------------------
// The ring floor (§6).

// 0x521469..0x5214f0: the wave driver (every alive frame while byte 0x4a0 == 1).
void Boss03::wave_tick() {
    ++wave_frame_;
    if (blk478_ != 0 && wave_frame_ % blk478_ == 0) ++wave_step_;
    const int s = wave_step_;
    if (unsigned(s) > 18u) return;
    const int base = wave_base_;
    if (s == 0) {
        wave(base, 0);
    } else {
        const int a = base + s;
        wave(a < kRings ? a : a - kRings, s);
        const int b = base - s;
        wave(b < 0 ? b + kRings : b, wave_step_);
    }
}

// FUN_00527280(ring k, step s) [BYTES 0x527280..0x5275cc].
void Boss03::wave(int k, int s) {
    if (s < 0) s = -s;
    const int LONG = short_hold_ * 500;                    // 0x1f4
    int& hold = ring_hold_[std::size_t(k)];
    switch (wave_var_) {                                   // table 0x5275d0
        case 0: hold = s < near_steps_ ? (near_steps_ - wave_step_) * 500 : short_hold_; break;   // BEAT
        case 1: hold = k % 4 == 0 ? LONG : (s < near2_ ? 170 : short_hold_); break;               // BEAT' Normal/Easy
        case 2: hold = k % 3 == 0 ? LONG : (s < near2_ ? 170 : short_hold_); break;               // BEAT' Hard/NM
        case 3: hold = k % 3 != 0 ? LONG : (s < near2_ ? 170 : short_hold_); break;               // never set
        case 4: hold = (k & 1) ? (s < near2_ ? 170 : short_hold_) : LONG; break;                 // BEAT' flag 201
        default: break;
    }
    if (ring_latch_[std::size_t(k)]) {
        ring_latch_[std::size_t(k)] = false;
        ring_timer_[std::size_t(k)] = 0;
        ring_state_[std::size_t(k)] = 2;
        fx(434, atari(k));                                 // bs_03_yuka (inline, rot 0, stat obj)
    }
}

// FUN_005275f0: ring motion and the delayed SE 238 [BYTES 0x5275f0..0x527847].
void Boss03::ring_motion() {
    bool any = false;
    for (int k = 0; k < kRings; ++k) any |= ring_state_[std::size_t(k)] != 0;
    if (any) {
        for (int k = 0; k < kRings; ++k) {
            int& st = ring_state_[std::size_t(k)];
            if (st == 2) {                                 // sinking
                float y = ring_y_[std::size_t(k)] - sink_speed_;
                if (!(sink_bottom_ < y)) { y = sink_bottom_; st = 3; }
                ring_set_y(k, y);
            }
            if (st == 3) {                                 // held
                if (++ring_timer_[std::size_t(k)] >= ring_hold_[std::size_t(k)]) {
                    ring_timer_[std::size_t(k)] = 0;
                    st = 4;
                    se3d(249, {0.0f, ring_y_[std::size_t(k)], 0.0f});   // the ring object's +0x50 (the arena centre)
                }
            } else if (st == 4) {                          // rising (never reaches -1.5 in float32, G13)
                float y = ring_y_[std::size_t(k)];
                y = (-1.5f - y) / 10.0f + y;               // 0x6be074 / 0x6bd928
                if (y >= -1.5f) { y = -1.5f; st = 0; }
                ring_set_y(k, y);
            }
        }
    }
    for (int k = 0; k < kRings; ++k) {
        int& c = se238_[std::size_t(k)];
        if (c >= 2) --c;
        else if (c != 0) {
            se3d(238, atari(k));
            c = 0;
        }
    }
}

// FUN_00523480 (a = 0, right) / FUN_00523670 (a = 1, left): the procedural arm pose
// (§3.6) [BYTES 0x523480..0x523856]. Visual only.
void Boss03::arm_pose(int a) {
    const std::size_t i = std::size_t(a);
    screw_[i] = screw_tgt_[i] > screw_[i] ? screw_[i] + 0.2f : screw_tgt_[i];    // 0x6bd210
    slide_[i] = slide_tgt_[i] > slide_[i] ? slide_[i] + 0.01f : slide_tgt_[i];   // 0x6bd084
    neji_q_[i] = slerp(neji_q_[i], quat_ypr(0.0f, 0.0f, -screw_[i]), 0.1f);
    pose_->arm_written[i] = true;
    pose_->neji[i] = neji_q_[i];
    if (alive_[i]) {
        pose_->forearm_gone[i] = false;
        pose_->forearm_dz[i] = slide_[i];
        const int c = code();
        pose_->forearm_alt_dy[i] = ((c == kCodeSwing || c == kCodeNagi) && strike_) ? -5000.0f : 0.0f;   // 0x6be154
    } else {
        pose_->forearm_gone[i] = true;                     // T(0, -5000, 5000): the arm is thrown away
    }
}

// ---------------------------------------------------------------------------
// FUN_0051f520: the per-frame tail (§11) [BYTES 0x51f520..0x51ffd8].
void Boss03::tail() {
    if (!(0.0f < hp_) || alive_[0] || alive_[1]) core_exposed_ = false;
    if (0.0f < hp_) {
        const float dx = player_.x - pos_.x, dz = player_.z - pos_.z;
        bearing_ = atan2f_(dz, dx);
        dist_ = std::sqrt(dx * dx + 0.0f + dz * dz);
        // The head push-out (§9) [BYTES 0x51f61b..0x51f78b].
        {
            const Vec3 h = node(kNHead);
            const float hx = player_.x - h.x, hy = player_.y - h.y, hz = player_.z - h.z;
            const float r = code() == kCodeMaguma ? 0.5f : 3.0f;   // 0x6bd304 / 0x6bd680
            const float d2 = (hy * hy + hx * hx) + hz * hz;
            if (r * r > d2) {
                const float len = std::sqrt(hx * hx + 0.0f + hz * hz);
                Vec3 n{0, 0, 0};
                if (len != 0.0f) { const float k = 1.0f / len; n = {k * hx, k * 0.0f, k * hz}; }
                const Vec3 dp{n.x * 0.2f, n.y * 0.2f, n.z * 0.2f};   // 0x6bd210
                out_->player_push.x += dp.x; out_->player_push.y += dp.y; out_->player_push.z += dp.z;
                player_.x += dp.x; player_.y += dp.y; player_.z += dp.z;
                out_->player_push_vel.x += n.x * 0.1f;     // 0x6bd19c
                out_->player_push_vel.y += n.y * 0.1f;
                out_->player_push_vel.z += n.z * 0.1f;
            }
        }
        // The Saru smoke every 6th frame, and the Head translation overwrite (bug, U9).
        if (smoke_t_ >= 5) {
            smoke_t_ = 0;
            const Vec3 sp = node(kNSaru);
            pose_->head_pos = sp;
            Boss03Effect& e = fx(433, sp);                 // bs_03_smoke (inline, attached)
            e.rot = {0.0f, -kHalfPi - facing_, 0.0f};
            e.node = kNSaru;
        } else {
            ++smoke_t_;
        }
        // The core effect swap (§11.2).
        if (core_exposed_) {
            if (!core02_) {
                kill_core_slot();
                core02_ = true;
                Boss03Effect& e = fx(499, node(kNCore));   // bs_03_core02
                e.rot = {0.0f, -kHalfPi - facing_, 0.0f};
                e.node = kNCore;
                e.slot = Boss03Effect::kCore;
                core_fx_ = true;
                se3d(250, node(kNCore));
            }
        } else if (core02_) {
            kill_core_slot();
            core02_ = false;
            Boss03Effect& e = fx(377, node(kNCore));       // back to bs_03_core01 (no SE)
            e.rot = {0.0f, -kHalfPi - facing_, 0.0f};
            e.node = kNCore;
            e.slot = Boss03Effect::kCore;
            core_fx_ = true;
        }
    } else {
        hp_ = 0.0f;
        parts_[0].hurt_centre.y = kHide;
        kill_core_slot();
    }
    bool ease = false;                                     // not during FIRE sub 0/1, not dead
    if (0.0f < hp_) {
        if (code() != kCodeFire) ease = true;
        else if (sub_ != 0) ease = sub_ != 1;
    }
    neck();
    if (ease) {
        cam_pitch_ = (0.122173049f - cam_pitch_) / 25.0f + cam_pitch_;   // 0x6bd1b4: 7 degrees
        cam_up_ = (2.5f - cam_up_) / 25.0f + cam_up_;      // 0x6bd624
        out_->camera.pitch = cam_pitch_;
        out_->camera.up = cam_up_;
    }
    if (flash_ != 0.0f) {                                  // 0x6bd174
        flash_ -= 0.075f;
        if (0.0f > flash_) flash_ = 0.0f;
    }
    if (bar_last_ != hp_) {                                // FUN_0043a460(0x6d5fd4, id, max, 5500, HP)
        bar_last_ = hp_;
        out_->gauge = Boss03Gauge{hp_, max_hp_, 5500.0f, 3};
    }
}

// FUN_00527b50: the neck look-at (§11.2) [BYTES 0x527b50..0x527caa]. Visual.
void Boss03::neck() {
    const std::uint8_t mode = (clip_ >= 0 && clip_ < kClips) ? kNeckMode[clip_] : 0;
    if (mode != 2) {
        float e = wrap_d(norm_2pi(facing_) - norm_2pi(bearing_));
        if (-0.785398185f > e) e = -0.785398185f;          // 0x6be034
        else if (!(0.785398185f > e)) e = 0.785398185f;    // 0x6bd3a0
        if (mode != 1) e = 0.0f;
        float v = neck_;
        if (v != e) {
            if (e > v) {
                v += 0.02f;                                // 0x6bd0ec
                neck_ = v;
                if (!(v < e)) neck_ = e;
            } else {
                v -= 0.02f;
                neck_ = v;
                if (!(e < v)) neck_ = e;
            }
        }
    }
    pose_->neck = quat_ypr(neck_ * -0.6f / 3.0f, 0.0f, neck_ * 0.6f);   // 0x6be01c / 0x6bd680 / 0x6bd334
}

// ---------------------------------------------------------------------------
// FUN_005215e0: the death and the flag-222 handshake (§8) [BYTES 0x5215e0..0x5227d7].
void Boss03::death() {
    if (clip_ != kDead) {                                  // the first frame
        c4ac_ = 0;
        c4b0_ = 0;
        blk478_ = 0;
        set_clip(kDead);
        out_->rumble.push_back(5);
        se2d(805, 0x400);
        out_->time_scale = 0.875f;                         // 0x3f600000: the whole game in slow motion
    }
    if (c4ac_ == 0) {                                      // init [BYTES 0x5216b5..0x52183d]
        out_->kill_perries = true;
        st_->death_cam_y = 0.0f;
        st_->death_cam_h = 0.0f;
        st_->debris_t = 0;
        st_->armor_latch = 0;
        ring_hold_.fill(0);                                // every held ring rises
        shake(795, {0.15f, 0.15f, 0.0f});                  // 0x31b, 0x3e19999a
        out_->anim_speed = 0.75f;
        c4ac_ = 2;
        out_->player_iframes = true;
        flag(134, 1);                                      // the player input lock [H U12]
    }
    if (c4b0_ < 3) death_stutter();
    else death_final();
    // Every death frame, last [BYTES 0x522717..0x5227d1].
    ++blk478_;
    if (blk478_ % 60 == 0 && unsigned(c4ac_) < 550u) out_->rumble.push_back(5);
    if (c4ac_ > 30) {
        if (rush_) {
            flag(156, 0);
            f156_ = false;
            return;
        }
        if (player_hp_ >= 1.0f) {
            flag(222, 1);
            out_->clear_buffs = true;                      // + byte 0x76a840 = 1 (no reader)
        }
        out_->steam_achievement = true;
    }
}

// The three DEAD-clip stutters (c4b0 = 0, 1, 2) [BYTES 0x521847..0x521fae].
void Boss03::death_stutter() {
    if (c4ac_ == 1) {                                      // a new stutter: re-set DEAD hard at 2001
        Boss03ClipSet cs{kDead, 1, true, 2001.0f};         // FUN_005f5e10, then *(+0x20) = 0x44fa2000
        out_->clips.push_back(cs);
        clip_ = kDead;
        done_ = false;
        frame_ = 2001.0f;
        st_->debris_t = 0;
        st_->armor_latch = 0;
        out_->anim_speed = 0.75f;
        lfx_.fill(true);
        ++c4ac_;
    }
    if (c4b0_ == 1) {
        out_->camera.eye_flags = 0;
        out_->camera.target = Vec3{pos_.x, 7.0f, pos_.z};
        out_->camera.target_mode = 0;
        Vec3 eye = rot_y_z(float(double(yaw_ - kPi) + 1.102313242460552), 17.0f);   // f64 0x6bd550
        eye.y = 30.0f;
        out_->camera.eye = eye;
        out_->camera.eye_mode = 0;
    } else if (c4b0_ == 2) {
        out_->camera.target = Vec3{pos_.x, 5.0f, pos_.z};
        out_->camera.target_mode = 0;
        Vec3 eye = rot_y_z(float(double(yaw_ - kPi) - 2.5132741928100586), 25.0f);  // f64 0x6bd5a0
        eye.y = 20.0f;
        out_->camera.eye = eye;
        out_->camera.eye_mode = 0;
    }
    if (frame_in(2003, 2004)) {
        node_fx(507, kNCore, st_->armor_latch, true, 0.0f, 3);   // bs_03_armorbreak, latch = the static
        if (st_->armor_latch < 4) ++st_->armor_latch;
        Boss03Effect& e = fx(433, node(kNCore));           // FUN_00450880: bs_03_smoke, no stat
        e.stat = Boss03Effect::kNone;
        if (lse_[0]) {
            lse_[0] = false;
            se2d(209, 0x233);
            se2d(250, 0x400);
        }
        se_latch(804, pos_, 1);
        zoom(0.5f, -0.025f);
    }
    if (++st_->debris_t % 20 == 0) debris(false);
    if (frame_in(2013, 2020)) {                            // the stutter ends
        ++c4b0_;
        c4ac_ = 1;
        lse_.fill(true);
        out_->flash = std::make_pair(1.0f, -0.0280000009f);   // 0xbce56042
        zoom(0.0f, 0.0f);
        out_->zoom.snapshot = true;                        // 0x7b2488 = 1
    }
}

// The final phase (c4b0 == 3), c4ac = 1, 2, ... [BYTES 0x521fb3..0x522711].
void Boss03::death_final() {
    if (c4ac_ == 1) {
        flag(134, 0);
        out_->time_scale = 1.0f;
        st_->death_cam_y = 12.0f;
        st_->death_cam_h = 2.0f;
        out_->anim_speed = 1.0f;
        shake(795, {0.25f, 0.25f, 0.0f});
        cam_dist_ = 22.0f;                                 // 0x41b00000
        out_->camera.dist = cam_dist_;
        se_latch(577, pos_, 2);
        se_latch(803, pos_, 3);
    }
    if (frame_in(2075, 2080) && lse_[0]) {
        lse_[0] = false;
        se2d(208, 0x400);
    }
    // The camera pull-back.
    {
        const float y0 = st_->death_cam_y;
        const float y = y0 - (y0 + 15.0f) / 500.0f;        // 0x6bd964 / 0x6bdcf4
        st_->death_cam_y = y;
        out_->camera.target = Vec3{pos_.x, y, pos_.z};
        out_->camera.target_mode = 0;
        cam_dist_ = (5.0f - st_->death_cam_h) / 500.0f + cam_dist_;   // 0x6bd868 (the old eye height)
        out_->camera.dist = cam_dist_;
        Vec3 eye = rot_y_z(yaw_ - kPi, cam_dist_);
        const float h = (10.0f - st_->death_cam_h) / 500.0f + st_->death_cam_h;   // 0x6bd928
        eye.y = h;
        st_->death_cam_h = h;
        out_->camera.eye = eye;
        out_->camera.eye_mode = 0;
    }
    // The shake decays, the debris period creeps toward 30.
    shake_amp_.x = (0.0f - shake_amp_.x) / 450.0f + shake_amp_.x;   // 0x6bdcc8
    shake_amp_.y = (0.0f - shake_amp_.y) / 450.0f + shake_amp_.y;
    out_->shake.amp = shake_amp_;
    st_->debris_period = (30.0f - st_->debris_period) / 700.0f + st_->debris_period;   // 0x6bd9e4 / 0x6bdda0
    if (unsigned(c4ac_) < 545u) {
        const int per = int(st_->debris_period);           // cvttss2si
        if (per != 0 && unsigned(c4ac_) % unsigned(per) == 0u) debris(true);
    }
    if (c4ac_ == 550) {                                    // the end
        for (int k = 0; k < kFxIds; ++k) kill_id(kFxIdList[k]);
        out_->gauge_hide = true;                           // DAT_006d5fd8 = 0
        if (!rush_) {
            Boss03Exp x;
            x.award = exp_b0_ != 0.0f;
            x.level = level_;
            x.b0 = exp_b0_;
            x.mult = very_easy_ ? 2.0f : (easy_ ? 1.1f : 1.0f);   // 0x6bd580 / 0x6bd464
            x.drop = rec4_ != 0.0f;
            if (x.award || x.drop) out_->exp = x;
        }
        alpha_ = 0.0f;                                     // obj+0x418
        out_->shake.frames = 0;
        out_->destroy = true;                              // obj+0x11 |= 2, obj+0x60 &= ~0x80
        destroyed_ = true;
    }
    ++c4ac_;
}

// The inline debris burst (FX 506 bs_03_deadbomb, FUN_00450880, no stat): 7 draws in the
// order x, z, y, scale z, scale y, scale x, rot y [BYTES 0x521ceb..0x521f1d / 0x5222df..0x5225a9].
void Boss03::debris(bool final_phase) {
    const std::uint32_t r1 = draw(), r2 = draw(), r3 = draw(), r4 = draw(), r5 = draw(), r6 = draw(), r7 = draw();
    auto R_ = [](std::uint32_t w, int n) { return int((w >> 17) % std::uint32_t(n)); };
    Vec3 p;
    Vec3 sc;
    if (!final_phase) {
        p.x = float(R_(r1, 900) - 450) / 100.0f + pos_.x;  // 0x384 / 0x1c2 / 0x6bdafc
        p.z = float(R_(r2, 900) - 450) / 100.0f + pos_.z;
        p.y = float(R_(r3, 2000) + 100) / 100.0f;          // absolute 1..21
        sc.z = float(R_(r4, 5)) * 0.1f + 0.5f;
        sc.y = float(R_(r5, 5)) * 0.1f + 0.5f;
        sc.x = float(R_(r6, 5)) * 0.1f + 0.5f;
    } else {
        const unsigned h = unsigned(c4ac_) >> 1;
        const int nxz = 900 - int(h);
        const int ny = 2000 - int(double(unsigned(c4ac_)) * 2.5);   // f64 0x6bd598, cvttsd2si
        p.x = float(R_(r1, nxz) - 450) / 100.0f + pos_.x;
        p.z = float(R_(r2, nxz) - 450) / 100.0f + pos_.z;
        p.y = float(R_(r3, ny) + 100) / 100.0f;
        sc.z = float(R_(r4, 10)) * 0.1f + 0.5f;
        sc.y = float(R_(r5, 10)) * 0.1f + 0.5f;
        sc.x = float(R_(r6, 10)) * 0.1f + 0.5f;
    }
    Boss03Effect& e = fx(506, p);
    e.scale = sc;
    e.rot = {0.0f, float(R_(r7, 628)) * 0.00999999978f, 0.0f};   // 0x274 / 0x6bd084
    e.stat = Boss03Effect::kNone;
}

// ---------------------------------------------------------------------------
namespace boss03 {

Vec3 camera_shake(int& frames, const Vec3& amp, const std::function<std::uint32_t()>& rng) {
    if (frames == 0 || !rng) return {0, 0, 0};
    auto u = [](std::uint32_t w) {
        const std::uint32_t b = (w & 0x7fffffu) | 0x3f800000u;
        float f;
        static_assert(sizeof f == sizeof b, "float");
        std::memcpy(&f, &b, sizeof f);
        return f - 1.0f;
    };
    const float a = u(rng()), b = u(rng()), c = u(rng());
    --frames;
    return {((a + a) - 1.0f) * amp.x, ((b + b) - 1.0f) * amp.y, ((c + c) - 1.0f) * amp.z};
}

}  // namespace boss03

}  // namespace cleria
