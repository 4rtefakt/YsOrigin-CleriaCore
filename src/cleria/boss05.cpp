// Pictimos (boss05) — see boss05.hpp and build/boss_re/PICTIMOS.md.
#include "cleria/boss05.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cleria/enemy_ai.hpp"   // rotate_toward (FUN_005eecc0), ai::angle_diff (FUN_005eec50)

namespace cleria {
namespace boss05 {

const SectionInfo kSectionTable[kSections] = {
    {"STAND", 1, 24, true}, {"WALK", 30, 45, true}, {"L_WALK", 50, 65, true}, {"R_WALK", 66, 81, true},
    {"OOBURI", 86, 110, false}, {"YOKONAGI", 111, 130, false}, {"TATENAGI", 131, 150, false},
    {"TATAKITUKE", 151, 175, false}, {"UTIAGE", 181, 244, false}, {"LASER", 251, 330, false},
    {"ZOROMEKA", 336, 385, false}, {"JUMP", 391, 421, false}, {"JETATTACK", 426, 450, false},
    {"DEAD", 456, 520, false}, {"PIKURI", 526, 529, false}, {"DOKONJYO", 535, 624, false},
    {"RAKKATYUU", 405, 405, false}, {"TYAKUTI", 580, 624, false},
};

const char* const kSlotNames[kSlots] = {
    "Bone01", "Bone02", "Bone01(11)", "Bone01(13)", "Bone01(12)", "Bone01(32)", "Bone01(31)", "Bone04(50)",
    "Bone04(54)", "Bone03(27)", "Bone03(46)", "Bone03(24)", "Bone03(43)", "Bone01(17)", "Bone01(59)", "Bone04(21)",
    "Bone04(60)", "Bone01(36)", "Bone01(61)", "Bone04(40)", "Bone04(62)", "Bone01(8)", "Bone02(9)", "Bone03(10)",
    "Bone05", "Bone05(30)", "Bone03(20)", "Bone03(39)",
};

const PartSlot kPartSlots[kParts] = {{23, 1.6f}, {2, 1.6f}, {3, 4.0f}, {5, 4.0f}, {24, 2.5f}, {25, 2.5f}, {15, 2.0f},
                                     {19, 2.0f}, {13, 2.0f}, {17, 2.0f}, {26, 2.0f}, {27, 2.0f}, {0, 0.0f}};

}  // namespace boss05

using namespace boss05;

namespace {
constexpr float kPi = 3.14159265358979f;
float cosf_(float a) { return float(std::cos(double(a))); }
float sinf_(float a) { return float(std::sin(double(a))); }
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 nrm(const Vec3& a) {
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l == 0.0f ? Vec3{} : mul(a, 1.0f / l);
}
// D3DXMatrixRotationYawPitchRoll(y, p, 0) applied to (0, 0, -1) (row vectors: pitch about X, then yaw about Y).
Vec3 ypr_fwd(float y, float p) {
    const float cp = cosf_(p), sp = sinf_(p), cy = cosf_(y), sy = sinf_(y);
    return {-cp * sy, sp, -cp * cy};
}
// FUN_0044bd70 [H the order]: rotate v about X by e0, Y by e1, Z by e2.
Vec3 euler(const Vec3& v, float e0, float e1, float e2) {
    Vec3 r = v;
    float c = cosf_(e0), s = sinf_(e0);
    r = {r.x, r.y * c - r.z * s, r.y * s + r.z * c};
    c = cosf_(e1); s = sinf_(e1);
    r = {r.x * c + r.z * s, r.y, -r.x * s + r.z * c};
    c = cosf_(e2); s = sinf_(e2);
    return {r.x * c - r.y * s, r.x * s + r.y * c, r.z};
}
}  // namespace

Boss05::Boss05(const Boss05Setup& s) {
    char_ = s.character; bb5c_ = s.diff_index; easy_ = s.diff_easy; very_easy_ = s.diff_very_easy; rush_ = s.boss_rush;
    pos_ = s.pos; pos_.z = 17.0f;                                  // obj+0x58 = 17 [BYTES 0x52da48]
    level_ = s.level; exp_b0_ = s.exp_b0; retries_ = s.retries;
    static const float kHpMul[3] = {1.0f, 1.25f, 1.5f};           // FUN_00443b80
    max_hp_ = s.eex_hp * kHpMul[std::clamp(bb5c_, 0, 2)];
    str_ = s.eex_str; def_ = s.eex_def;
    if (very_easy_) { max_hp_ *= 0.75f; str_ -= 1.0f; def_ -= 2.0f; }
    else if (easy_) def_ -= 1.0f;
    hp_ = max_hp_;
    if (!rush_ && s.retries > 0) {                                 // the retry softening, then >= 1
        const int n = s.retries;
        const float k = very_easy_ ? float(std::pow(double(n), 1.25)) : easy_ ? float(n < 20 ? n >> 1 : n - 10) : 0.0f;
        str_ -= k; def_ -= k;
    }
    str_ = std::max(1.0f, str_); def_ = std::max(1.0f, def_);
    for (int i = 0; i < kParts; ++i) {                             // 13 parts (FUN_005a1a10 type 4)
        Boss05Part& p = parts_[std::size_t(i)];
        p.slot = kPartSlots[i].slot; p.radius = kPartSlots[i].radius;
        p.max_hp = max_hp_; p.hp = max_hp_ * 10.0f; p.def = i >= 2 ? 999.0f : def_;
        if (char_ == 1) { p.resist[0] = -0.3f; p.resist[1] = 0.1f; }
        else if (char_ == 2) { p.resist[1] = 0.2f; p.resist[2] = 0.6f; }
        else if (char_ == 3) { p.resist[1] = -0.2f; p.resist[2] = -0.1f; }
        p.centre = {500, 500, 500};
    }
    // The difficulty table [BYTES 0x52dede..0x52e075], Easy first.
    if (easy_) { near_ = 15.0f; utiage_deg_ = 0.08f; windup_ = 0; variant_ = 0; rounds_ = 1; minion_lv_ = 33; heal_amt_ = 30; cadence_ = 0; aura_scale_ = {1.0f, 0.8f, 1.0f}; }
    else if (bb5c_ == 0) { near_ = 13.5f; utiage_deg_ = 0.13f; windup_ = 0; variant_ = 0; rounds_ = 2; minion_lv_ = 35; heal_amt_ = 40; cadence_ = 0; aura_scale_ = {1, 1, 1}; }
    else if (bb5c_ == 1) { near_ = 13.0f; utiage_deg_ = 0.155f; windup_ = 1; variant_ = 1; rounds_ = 3; minion_lv_ = 38; heal_amt_ = 40; cadence_ = 1; aura_scale_ = {1.3f, 1.2f, 1.2f}; }
    else if (bb5c_ == 2) { near_ = 13.0f; utiage_deg_ = 0.19f; windup_ = 2; variant_ = 1; rounds_ = 8; minion_lv_ = 40; heal_amt_ = 60; cadence_ = 2; aura_scale_ = {1.3f, 1.4f, 1.3f}; }
    else { near_ = 0; utiage_deg_ = 0; windup_ = 0; variant_ = 0; rounds_ = 0; minion_lv_ = 0; heal_amt_ = 0; cadence_ = 0; aura_scale_ = {}; }
}

Mat4 Boss05::matrix() const {
    const float c = cosf_(yaw_), s = sinf_(yaw_);
    Mat4 m;
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
    m.m[12] = pos_.x; m.m[13] = pos_.y; m.m[14] = pos_.z;
    return m;
}

std::uint32_t Boss05::draw() { return in_ && in_->rng ? in_->rng() : 0u; }
float Boss05::U1() {
    const std::uint32_t b = (draw() & 0x7fffffu) | 0x3f800000u;
    float f; std::memcpy(&f, &b, 4);
    return f - 1.0f;
}

void Boss05::set_state(int k) {                                    // FUN_0052d980
    d4_ = 0; d0_ = k; c0_ = 1; b4_ = 0;
    out_->anim_speed = 1.0f;
    out_->sections.push_back(k);
    sec_ = k; count_ = 0;                                          // FUN_005f5c80 zeroes A+0x1c
}

Boss05Effect& Boss05::fx(int id, const Vec3& p) {
    Boss05Effect e; e.id = id; e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}

void Boss05::bullet(int kind, int eff, const Vec3& p, const Vec3& d, std::initializer_list<std::pair<int, float>> f) {
    Boss05Bullet b; b.kind = kind; b.effect = eff; b.pos = p; b.dir = d;
    for (const auto& [i, v] : f) b.f[i] = v;
    out_->bullets.push_back(b);
}
static float ibits(int i) { float f; std::memcpy(&f, &i, 4); return f; }

void Boss05::add_part_damage(int i, float dmg) {
    Boss05Part& p = parts_[std::size_t(i)];
    p.dmg += dmg;
    p.hp -= dmg;
}
bool Boss05::part_hittable(int i) const {
    const Boss05Part& p = parts_[std::size_t(i)];
    return !destroyed_ && p.hp >= 1.0f && p.radius > 0.0f && p.centre.y < 400.0f;
}

// ---------------------------------------------------------------------------
Boss05Output Boss05::tick(const Boss05Input& in, std::vector<Boss05Minion>& minions) {
    Boss05Output out;
    if (destroyed_) return out;
    in_ = &in; out_ = &out; mins_ = &minions;
    sec_ = in.section; count_ = in.count;
    P_ = in.player_pos;
    if (!in.fight) {                                               // §A.1: the gate off
        if (vy_ != 0.0f || std::isnan(vy_)) {
            pos_.y += 0.5f * vy_;
            vy_ -= 0.05f;
            if (vy_ < -2.0f) { shake(20, {0, 0.6f, 0}); pos_.y = 0; vy_ = 0; }
        }
        in_ = nullptr; out_ = nullptr; mins_ = nullptr;
        return out;
    }
    // §A.2 the prologue
    const int clip0 = sec_;
    const float dz = P_.z - pos_.z, dx = P_.x - pos_.x;
    dx2_ = dx * dx; dz2_ = dz * dz;
    dist2_ = dx2_ + dz2_;
    tgt_yaw_ = -1.5707964f - float(std::atan2(double(dz), double(dx)));
    delta_ = std::max(0.08f, float(std::fabs(ai::angle_diff(yaw_, tgt_yaw_))));
    if (!in.flag224) { pushout(); part_sync(); }
    if (clip0 != kJetattack && pos_.x * pos_.x + pos_.z * pos_.z > 361.0f) {
        const Vec3 n = nrm({pos_.x, 0, pos_.z});
        pos_.x -= 0.15f * n.x; pos_.z -= 0.15f * n.z;
    }
    {
        const Vec3 Pp = add(P_, out.player_move);
        if (Pp.x * Pp.x + Pp.z * Pp.z > 576.0f) {
            const Vec3 n = nrm({Pp.x, 0, Pp.z});
            out.player_move = add(out.player_move, mul(n, -0.2f));
        }
    }
    dist_ = std::sqrt(dx * dx + dz * dz);
    ++b4_;
    if (hp_ < 1.0f || in.flag224) { death(); in_ = nullptr; out_ = nullptr; mins_ = nullptr; return out; }
    if (bc_ < 700) { intro_hold(); in_ = nullptr; out_ = nullptr; mins_ = nullptr; return out; }
    feet();
    if (dc_ != 0) { ++dc_; if (dc_ > 1200) dc_ = 0; }             // §A.5 (the colour: [not modelled])
    // §A.6 the summon gate
    if (hp_ <= 0.5f * max_hp_ && count_ == 1u) {
        const int n = int(minions.size());
        if (n < 12 && f8_ == 0) set_state(kZoromeka);
        else if (n == 0 && f4_ < rounds_ && (fc_ > 2 || (bb5c_ >= 1 && fc_ != 0))) set_state(kZoromeka);
        ++fc_;
    }
    // §A.7
    if ((d0_ == 7 || d0_ == 12) && count_ == 1u) set_state(kJump);
    done_ = clip0 == 0 ? (count_ + 1u >= std::uint32_t(c0_)) : clip0 == 1 ? false : count_ == 1u;
    selection_clip0_ = clip0;
    selection();
    s_ = sec_;                                                     // F+0xac: the section at the switch
    switch (sec_) {
        case kWalk: case_walk(); break;
        case kOoburi: case_ooburi(); break;
        case kYokonagi: case_yokonagi(); break;
        case kTatenagi: case_tatenagi(); break;
        case kTatakituke: case_tatakituke(); break;
        case kUtiage: case_utiage(); break;
        case kZoromeka: case_zoromeka(); break;
        case kJump: case_jump(); break;
        case kDead: case_dead(); break;
        default: break;
    }
    tail();
    in_ = nullptr; out_ = nullptr; mins_ = nullptr;
    return out;
}

// FUN_0052d6d0: push the player out of the body and the part spheres (planar).
void Boss05::pushout() {
    const float r2 = 31.36f * ((d8_ != 0 && sec_ == kWalk) ? 1.5f : 1.0f);
    Vec3 Pp = P_;
    const float dx = Pp.x - pos_.x, dz = Pp.z - pos_.z;
    if (dx * dx + dz * dz < r2) {
        const Vec3 n = nrm({dx, 0, dz});
        out_->player_move = add(out_->player_move, mul(n, 0.2f));
        out_->player_kb = add(out_->player_kb, mul(n, 0.1f));
        Pp = add(Pp, mul(n, 0.2f));
    }
    for (int i = 0; i < kParts; ++i) {
        const float r = kPartSlots[i].radius;
        const Vec3 b = in_->slots[std::size_t(kPartSlots[i].slot)];
        const Vec3 e = Pp - b;
        if (e.x * e.x + e.y * e.y + e.z * e.z < r * r) {
            const Vec3 n = nrm({e.x, 0, e.z});
            out_->player_move = add(out_->player_move, mul(n, 0.15f));
            out_->player_kb = add(out_->player_kb, mul(n, 0.03f));
            Pp = add(Pp, mul(n, 0.15f));
        }
    }
}

// FUN_0052d480: the parts follow their nodes; damage rolls up into the body (parts 0 and 1 only).
void Boss05::part_sync() {
    if (flash_ != 0.0f) flash_ = std::max(0.0f, flash_ - 0.075f);
    const bool inert = sec_ == kDead || hp_ < 1.0f || vy_ != 0.0f || in_->player_hp < 1.0f;
    float sum = 0;
    for (int i = 0; i < kParts; ++i) {
        Boss05Part& p = parts_[std::size_t(i)];
        p.centre = inert ? Vec3{500, 500, 500} : in_->slots[std::size_t(p.slot)];   // [H] +0x50 as the hit gate
        if (p.dmg != 0.0f) {
            if (cc_ == 0) { ++c8_; cc_ = 1; }                      // the booster meter, debounced
            sum += p.dmg; p.dmg = 0;
            if (!inert && i <= 1) { flash_ = 1.65f; hp_ -= sum; } // the running sum (U7)
        }
    }
    if (!(hp_ == bar_last_)) { bar_last_ = hp_; out_->gauge = std::make_pair(hp_, max_hp_); }
    if (cc_) cc_ = cc_ + 1 > 15 ? 0 : cc_ + 1;
}

// §A.3 the intro hold (the PART debris and the light: [not modelled]).
void Boss05::intro_hold() {
    ++bc_;
    if ((!in_->fix_held || retries_ != 0) && bc_ < 600) bc_ = 600;   // no Fix, or a retry: a 101-frame hold
    const int bc = bc_;
    if (bc == 1) out_->sounds.push_back(285);
    if (bc > 100 && bc < 330 && bc % 20 == 0) shake(5, {0.2f, 0.2f, 0});
    if (bc == 100 || bc == 130 || bc == 160 || bc == 200 || bc == 220 || bc == 230) {
        const float t = float((bc - 50) >> 1);
        Vec3 Q = mul(add(mul(pos_, t), mul(P_, 100.0f - t)), 0.01f);
        Q.x += 8.0f * U1() - 4.0f;
        Q.z += 6.0f * U1() - 3.0f;
        Boss05Effect& e = fx(528, {Q.x, 0, Q.z});
        e.rot = {0, 1.0f, 0}; e.power = 1.0f;
    }
    if (bc == 350) shake(70, {0.2f, 0.2f, 0});
    if (bc >= 350 && bc <= 390) (void)U1();                        // the PART size draw (the PART itself: [not modelled])
    if (bc >= 330 && bc <= 399 && bc % 5 == 0) {
        float X = pos_.x, Z = pos_.z - 5.0f;
        if (bc != 395) { X = pos_.x + 10.0f * U1() - 5.0f; Z = pos_.z + 2.0f * U1() - 5.0f; }
        Boss05Effect& e = fx(528, {X, 0, Z});
        e.rot = {0, 1.0f, 0}; e.scale = {1.5f, 1.0f, 1.5f}; e.power = 1.0f;
    }
}

// §A.4 the four feet.
void Boss05::feet() {
    static const int kA[4] = {13, 15, 17, 19}, kB[4] = {14, 16, 18, 20};
    for (int k = 0; k < 4; ++k) {
        const Vec3 Pa = in_->slots[std::size_t(kA[k])], Pb = in_->slots[std::size_t(kB[k])];
        int& cnt = feet_[std::size_t(k)];
        if (Pb.y < 0.65f) {
            const bool turn = k == 2 ? delta_ < 0.3f : delta_ > 0.3f;
            const bool fire = cnt == 0 || (selection_clip0_ == kWalk && turn && cnt % 5 == 0);
            if (fire) {
                fx(445, {Pa.x, 0.01f, Pa.z}).boss_stat = false;
                fx(445, {Pb.x, 0.01f, Pb.z}).boss_stat = false;
                if (cnt == 0) {
                    if (d8_ != 0) shake(2, {0, 0.3f, 0});
                    out_->sounds.push_back(191);
                }
            }
            ++cnt;
        } else {
            cnt = 0;
        }
    }
}

// §A.8..§A.10 the booster block and the selections.
void Boss05::selection() {
    const int clip0 = selection_clip0_;
    if (d8_ != 0) {
        ++d8_;
        if ((d8_ & 7) == 0)
            for (int slot = 9; slot <= 12; ++slot) {
                Boss05Effect& e = fx(379, {0, 0, 2.0f});
                e.slot = slot; e.rot = {0, kPi, 0}; e.scale = aura_scale_; e.power = -5.0f; e.rtype = 2;
            }
        if (clip0 != kWalk && !done_) return;
        if (d8_ > 700) {
            c8_ = 0; d8_ = 0; b8_ = 0; set_state(kJump);
            for (int h = 0; h < 4; ++h) if (aura_[std::size_t(h)]) { out_->kill_handles.push_back(h + 1); aura_[std::size_t(h)] = false; }
        } else if (near_ * near_ > dist2_ && delta_ < 0.5f) {
            ++b8_;
            if (R(4) == 0) set_state(kOoburi);
            else set_state(R(2) == 0 ? kYokonagi : kTatenagi);
        } else if (clip0 != kWalk && done_) {
            set_state(kWalk);
        }
        return;
    }
    if (clip0 != kWalk && !done_) return;
    const int c8v = c8_;
    if (c8v > 20 && (dc_ > 300 || dc_ == 0)) {
        set_state(kOoburi); d4_ = 1; c8_ = 0;
    } else if (dist2_ < 400.0f && delta_ < 0.3f) {
        ++b8_;
        if (b8_ < 3) {
            if (dc_ == 0) set_state(kUtiage);
            else set_state(R(2) == 0 ? kYokonagi : kTatenagi);
        } else if (b8_ < 7) {
            const std::uint32_t r = R(3);
            if (r == 0) set_state(kYokonagi);
            else if (r == 1) set_state(kTatenagi);
            else { b8_ = 0; set_state(dist2_ < 169.0f ? kTatakituke : kJump); }
        } else {
            b8_ = 0; set_state(dist2_ < 169.0f ? kTatakituke : kJump);
        }
    } else if (delta_ < 0.2f) {
        ++b8_;
        if (dc_ == 0 && b8_ == 2) set_state(kUtiage);
        else if (b8_ >= 10) { b8_ = 0; set_state(kJump); }
        else { c8_ = c8v + 2; set_state(R(2) == 0 ? kYokonagi : kTatenagi); }
    } else if (clip0 != kWalk && done_) {
        set_state(kWalk);
    }
}

// §B.1
void Boss05::case_walk() {
    const float th = theta();
    const float c = float(std::cos(double(th))), s = float(std::sin(double(th)));
    if (d8_ != 0) {
        speed((1.0f + 0.5f * delta_) * 0.7f + 0.4f);
        yaw_ = rotate_toward(yaw_, tgt_yaw_, 0.025f * delta_);
        if (dist2_ >= 121.0f) { pos_.x += 0.16f * c; pos_.z += 0.16f * s; }
        return;
    }
    const float k = float(b4_) * 0.02f;
    float rate;
    if (k < 1.0f) { speed((1.0f + 0.5f * delta_) * 0.7f); rate = 0.02f * delta_; }
    else { speed((1.0f + k * 0.5f * delta_) * 0.7f); rate = 0.02f * delta_ * k; }
    yaw_ = rotate_toward(yaw_, tgt_yaw_, rate);
    if (dist2_ >= 144.0f && (pos_.x * pos_.x + pos_.z * pos_.z < 225.0f || P_.x * P_.x + P_.z * P_.z < 361.0f)) {
        pos_.x += 0.09f * c; pos_.z += 0.09f * s;
    }
}

// §B.2
void Boss05::case_ooburi() {
    const float th = theta(), c = cosf_(th), s = sinf_(th);
    if (d4_ == 1) {                                                // the booster entry
        if (b4_ == 1) speed(0.35f);
        if (b4_ == 109) {
            out_->sounds.push_back(282); out_->rumble.push_back(5);
            d8_ = 1; feet_ = {0, 0, 0, 0}; speed(1.0f);
            for (int slot = 9; slot <= 12; ++slot) {
                Boss05Effect& e = fx(375, {0, 0, 2.0f});
                e.slot = slot; e.rot = {0, kPi, 0}; e.scale = aura_scale_; e.power = 1.0f; e.rtype = 2;
                e.handle = slot - 8; aura_[std::size_t(slot - 9)] = true;
            }
        }
        if (b4_ == 120) { speed(0.5f); out_->sounds.push_back(278); out_->zoom = std::make_pair(0.5f, -0.005f); }
        return;
    }
    if (d8_ == 0) return;
    if (b4_ == 1) speed(windup_ == 0 ? 0.37f : 0.555f);
    if (b4_ == 75 && windup_ != 0) b4_ = 105;
    if (b4_ == 105) { out_->sounds.push_back(283); speed(1.0f); }
    if (b4_ == 115) {
        bullet(1, 380, {pos_.x + 5 * c, 0.5f, pos_.z + 5 * s}, fwd_, {{0, 0.3f}, {1, 0.035f}, {3, 1.0f}, {4, -2.0f}});
        out_->sounds.push_back(271); speed(1.0f);
    }
    if (b4_ == 125) {
        bullet(1, 381, {pos_.x + 10 * c, 3.5f, pos_.z + 10 * s}, fwd_, {{0, 0.3f}, {1, 0.035f}, {3, 1.0f}, {4, -2.0f}});
        out_->sounds.push_back(272);
        feet_[0] = feet_[2] = 0;
    }
}

void Boss05::windup() {                                            // §B.3 / §B.4 shared
    if (d8_ == 0 && b4_ < 20) yaw_ = rotate_toward(yaw_, tgt_yaw_, 0.06f * delta_);
    if (b4_ == 20) speed(windup_ == 0 ? 0.2f : windup_ == 1 ? 0.4f : 0.6f);
    if (windup_ == 1 && b4_ == 32) b4_ = 44;
    if (windup_ == 2 && b4_ == 28) b4_ = 44;
    if (b4_ == 44) { out_->sounds.push_back(283); speed(1.0f); }
}

void Boss05::case_yokonagi() {
    windup();
    if (b4_ != 56) return;
    const float th = theta(), c = cosf_(th), s = sinf_(th);
    const Vec3 S{pos_.x + 5 * c, 0.5f, pos_.z + 5 * s};
    if (variant_ == 0) bullet(1, 380, S, fwd_, {{0, 0.3f}, {1, 0.035f}, {3, 1.0f}, {4, -2.0f}});
    if (variant_ == 1) {                                           // the two side shots (0.9 / 1.5: as read)
        const Vec3 Sa{S.x + 5 * cosf_(th + 0.9f), 0.5f, S.z + 5 * sinf_(th + 1.5f)};
        const Vec3 Sb{S.x + 5 * cosf_(th - 0.9f), 0.5f, S.z + 5 * sinf_(th - 1.5f)};
        bullet(1, 380, Sa, fwd_, {{0, 0.01f}, {1, 0.03f}, {3, 1.0f}, {4, -2.0f}});
        bullet(1, 380, Sb, fwd_, {{0, 0.01f}, {1, 0.03f}, {3, 1.0f}, {4, -2.0f}});
    }
    out_->sounds.push_back(271);
}

void Boss05::case_tatenagi() {
    windup();
    if (b4_ != 60) return;
    const float th = theta();
    for (float sg : {1.0f, -1.0f}) {
        const Vec3 S{pos_.x + 10 * cosf_(th + 0.1f * sg), 3.5f, pos_.z + 10 * sinf_(th + 0.1f * sg)};
        if (variant_ == 0) bullet(1, 381, S, fwd_, {{0, 0.3f}, {1, 0.035f}, {3, 1.0f}, {4, -2.0f}});
        if (variant_ == 1)
            bullet(4, 381, S, fwd_, {{0, 0.01f}, {1, 0.02f}, {3, 1.0f}, {4, -2.0f}, {7, ibits(1)}, {8, 0.0174533f},
                                     {10, 1.0f}, {11, 20.0f}});
    }
    out_->sounds.push_back(272);
}

void Boss05::case_tatakituke() {                                   // §B.5
    const float th = theta(), c = cosf_(th), s = sinf_(th);
    if (b4_ == 10) speed(0.2f);
    if (b4_ == 33) b4_ = 55;
    if (b4_ == 55) { out_->sounds.push_back(273); out_->sounds.push_back(283); speed(1.0f); }
    if (b4_ == 88) {
        speed(0.2f); shake(20, {0, 0.6f, 0}); out_->rumble.push_back(1); out_->zoom = std::make_pair(0.5f, -0.005f);
        Boss05Effect& e = fx(378, {pos_.x + 8 * c, pos_.y + 0.2f, pos_.z + 8 * s});
        e.rot = {0, yaw_ + kPi, 0}; e.scale = {2, 2, 2}; e.power = 1.0f; e.rtype = 2;
    }
    if (b4_ == 130) speed(1.2f);
    if (b4_ == 150) speed(0.3f);
    if (b4_ == 170) speed(1.0f);
}

void Boss05::case_utiage() {                                       // §C.1
    if (b4_ == 1) speed(1.0f);
    if (b4_ == 50 || b4_ == 63 || b4_ == 79) {
        out_->sounds.push_back(281);
        feet_ = {0, 0, 0, 0};
        shake(2, {0, 0.3f, 0});
    }
    if (b4_ == 50) {
        dc_ = 1;
        tgt_ = P_;
        const float th = theta();
        const float ox = -cosf_(th) * 3.0f, oz = sinf_(th) * -3.0f;
        static const int kSl[4] = {4, 6, 3, 5};
        static const float kDy[4] = {0, 0, -0.6f, -0.6f}, kDp[4] = {0.3f, -0.3f, 0.2f, -0.2f};
        for (int k = 0; k < 4; ++k) {                              // the funnels (no hit)
            const Vec3 n = in_->slots[std::size_t(kSl[k])];
            Boss05Effect& e = fx(383, {n.x + ox, n.y + kDy[k], n.z + oz});
            e.rot = {0.5f, yaw_ + kDp[k], 0}; e.scale = {3, 3, 3};
        }
    }
    if (b4_ == 250) out_->sounds.push_back(277);
    if (std::uint32_t(b4_ - 201) <= 78u && b4_ % 17 == 0) {         // 204, 221, 238, 255, 272: the meteors
        speed(0.5f);
        const float al = float(std::uint32_t(4 * b4_));
        const float yw = RF(6.2831855f);
        const float r = RR(6.0f, 17.0f);
        Vec3 p{(tgt_.x + pos_.x) * 0.5f - r * cosf_(al), (tgt_.y + pos_.y) * 0.5f, (tgt_.z + pos_.z) * 0.5f - r * sinf_(al)};
        const float pt = -RR(1.2566371f, 1.5707964f);
        const Vec3 d = nrm(ypr_fwd(yw, pt));
        p.y += RF(1.0f) + 18.0f;
        bullet(2, 376, p, d, {{0, 0.04f}, {3, -10.0f}, {4, -2.0f}, {7, ibits(1)}, {8, utiage_deg_ * 0.017453292f},
                              {10, 1.0f}, {11, 700.0f}});
    }
}

void Boss05::case_zoromeka() {                                     // §C.2
    const float th = theta(), c = cosf_(th), s = sinf_(th);
    if (b4_ == 1) { speed(0.5f); if (f8_ != 0) ++f4_; }
    if (b4_ >= 80 && b4_ < 200 && b4_ % 20 == 0) {
        Boss05Effect& e = fx(414, {pos_.x + 3 * c, 0.3f, pos_.z + 3 * s});
        e.rot = {0.31415927f, yaw_, 0}; e.scale = {2.0f, 0.3f, 2.3f}; e.power = 1.0f; e.rtype = 2;
    }
    if (b4_ == 70) out_->sounds.push_back(279);
    if (b4_ <= 70) return;
    const std::uint32_t b4 = std::uint32_t(b4_), f4 = std::uint32_t(f4_);
    if (cadence_ == 0) { if (b4 >= 230u - 15u * f4 || b4 % (3u * (f4 + 2u)) != 0) return; }
    else if (cadence_ == 1) { if (b4 >= 230u - 10u * f4 || b4 % (2u * f4 + 6u) != 0) return; }
    else if (cadence_ == 2) { if (b4 >= 230u - 15u * f4 || b4 % 6u != 0) return; }
    else return;
    speed(1.0f);
    const Vec3 sp{pos_.x + 4.5f * c, 0.25f, pos_.z + 4.5f * s};
    const int lvl = minion_lv_ - 2 * f4_;
    const float a = RR(-1.0471976f, 1.0471976f);
    const int deg = int((a + (yaw_ - kPi)) * 57.295776f);          // FUN_0066f350: truncation [H]
    float mdef = 0;
    const int id = in_->spawn ? in_->spawn(sp, deg, lvl, &mdef) : 0;
    if (id == 0) return;
    const float u = RF(1.0f);
    Vec3 v = mul(fwd_, 1.5f * u);
    float e[3];
    for (float& x : e) x = float((draw() >> 17) % 100u) * 0.7853982f / 100.0f - 0.3926991f;
    v = euler(v, e[0], e[1], e[2]);
    v.y *= 20.0f;
    out_->spawned.push_back({id, v});
    minion_def_ = mdef;                                            // blk+0x1e8 = m.+0xa8
}

void Boss05::case_jump() {                                         // §C.3
    const float th = theta(), c = cosf_(th), s = sinf_(th);
    if (b4_ == 1) {
        if (d4_ == 0) { const Vec3 t = nrm({-P_.x, 0, -P_.z}); tgt_ = mul(t, 18.0f); }
        vy_ = 2.0f;
    }
    if (b4_ == 55) out_->sounds.push_back(274);
    if (b4_ >= 38 && b4_ <= 299) {
        yaw_ = rotate_toward(yaw_, tgt_yaw_, delta_ * 0.1f);
        pos_.x += (tgt_.x - pos_.x) * 0.04f;
        pos_.z += (tgt_.z - pos_.z) * 0.04f;
        pos_.y += vy_ * 0.5f;
        vy_ -= 0.05f;
        if (vy_ < -2.0f) {                                         // the landing
            out_->rumble.push_back(1); out_->sounds.push_back(191);
            shake(20, {0, 0.6f, 0});
            pos_.y = 0; b4_ = 300; vy_ = 0;
        }
    }
    if (d4_ == 0) {
        if (b4_ == 300) {
            skew_ = RR(-0.2243995f, 0.2243995f);
            Boss05Effect& e = fx(384, {pos_.x + 8 * c, pos_.y + 0.2f, pos_.z + 8 * s});
            e.rot = {0, yaw_ + kPi, 0}; e.scale = {1.3f, 0.5f, 1.3f};
            out_->sounds.push_back(275); out_->sounds.push_back(276);
        }
        if (b4_ >= 300) {                                          // the shock-wave push on the player
            const float d = std::sqrt(dx2_ + dz2_);
            const float f = (40.0f - d) * 0.125f;
            if (f > 0.0f) {
                const Vec3 n = nrm({P_.x - tgt_.x, 0, P_.z - tgt_.z});
                out_->player_move = add(out_->player_move, mul(n, 0.2f * f));
                out_->player_kb = add(out_->player_kb, mul(n, 0.1f * f));
            }
        }
        if (b4_ >= 300 && b4_ <= 329 && b4_ % 5 == 0) {           // the fissures
            const float k = float(b4_ - 286);
            for (float ph : {th + 0.2f + skew_, th - 0.2f + skew_}) {
                Boss05Effect& e = fx(382, {pos_.x + k * cosf_(ph), pos_.y, pos_.z + k * sinf_(ph)});
                e.scale = {1.5f, 1.5f, 1.5f}; e.power = 1.0f;
            }
        }
        return;
    }
    if (b4_ == 300) {                                              // the pounce
        speed(0.5f);
        skew_ = RR(-0.9424779f, 0.9424779f);
        (void)RR(-0.9424779f, 0.9424779f);                         // blk+0x200: a dead draw
        const Vec3 E{pos_.x + 8 * c, pos_.y + 0.2f, pos_.z + 8 * s};
        Boss05Effect& e = fx(378, E);
        e.rot = {0, yaw_ + kPi, 0}; e.scale = {1.5f, 1.5f, 1.5f}; e.power = 1.0f;
        out_->sounds.push_back(276);
        for (float ph : {th - 1.5707964f, -yaw_, th}) {
            const Vec3 dir{cosf_(ph), 0, sinf_(ph)};
            bullet(8, 270, add(E, mul(dir, 6.0f)), dir, {{0, 0.3f}, {4, -2.0f}, {5, ibits(382)}, {6, ibits(8)},
                                                        {7, ibits(1)}, {8, 0.0349066f}});
        }
    }
}

void Boss05::case_dead() {                                         // §C.4 the guts
    if (vy_ != 0.0f || std::isnan(vy_)) {
        pos_.y += vy_ * 0.5f; vy_ -= 0.05f;
        if (vy_ < -2.0f) { shake(20, {0, 0.6f, 0}); pos_.y = 0; vy_ = 0; }
    }
    if (b4_ == 1) { ++f8_; speed(0.5f); }
    if (b4_ == 120) {
        out_->sounds.push_back(280);
        for (int h = 0; h < 2; ++h)
            if (!heal_[std::size_t(h)]) {
                Boss05Effect& e = fx(373, {pos_.x, pos_.y - 2.5f, pos_.z});
                e.scale = {1.2f, 1.2f, 1.2f}; e.boss_stat = false; e.handle = 5 + h;
                heal_[std::size_t(h)] = true;
            }
    }
    if (b4_ >= 101 && b4_ <= 499) {
        speed(0.3f);
        int alive = 0;
        for (Boss05Minion& m : *mins_) {
            if (!(m.hp >= 1.0f)) continue;
            ++alive;
            if (std::uint32_t(b4_) > 100u + 3u * std::uint32_t(m.pool)) {
                m.alpha -= 0.07f;
                if (m.alpha < 0.0f) { m.or130 |= 0x2008u; m.hp = 0; hp_ += float(heal_amt_); }   // no clamp
            }
        }
        if (alive == 0) b4_ = 500;
    }
    if (b4_ == 550) {
        for (int h = 0; h < 2; ++h) if (heal_[std::size_t(h)]) { out_->kill_handles.push_back(5 + h); heal_[std::size_t(h)] = false; }
        b8_ = 0; set_state(kJump); fc_ = 0; d4_ = 0;
    }
}

// §D.5 the tail: the forward vector, the pounce trigger, gather / control of the summons.
void Boss05::tail() {
    fwd_ = {-sinf_(yaw_), 0, -cosf_(yaw_)};
    const int s = s_;
    std::vector<Boss05Minion>& M = *mins_;
    if ((s == 0 || s == 1 || s == 5 || s == 6) && d8_ == 0) {
        for (const Boss05Minion& c : M)
            if (1.0f > c.hp && c.dead_t == 2u) {
                set_state(kJump); d4_ = 1;
                Vec3 t = mul(add(pos_, c.pos), 0.5f); t.y = 0;
                if (t.x * t.x + t.z * t.z > 400.0f) t = mul(nrm(t), 20.0f);
                tgt_ = t;
                break;
            }
    }
    if (s == 13 || s == 9 || s == 12 || (s == 11 && b4_ < 300)) { // GATHER
        const float K = s == 13 ? 60.0f : 20.0f;
        const int base = s == 13 ? 60 : 30;
        const std::uint32_t t = std::uint32_t(b4_ - base);
        int k = 0;
        for (Boss05Minion& c : M) {
            if (s != 13) { if (c.hp >= 1.0f) c.hp = c.max_hp; c.def = 999.0f; }
            c.target = -2;
            const int ni = 47 - k % 22;
            if (ni < 0 || std::size_t(ni) >= in_->nodes.size()) return;
            const Vec3 J = in_->nodes[std::size_t(ni)];
            const float fi = float(std::uint32_t(c.pool));
            const Vec3 dest{J.x + 0.5f * cosf_(fi), J.y + 0.5f * cosf_(fi), J.z + 0.5f * sinf_(fi)};
            const int w = k / 3;
            if (b4_ > base + w) {
                if (float(w) + K > float(t)) {
                    const float u = float(t - std::uint32_t(w));
                    c.pos = mul(add(mul(dest, u), mul(c.pos, K - u)), 1.0f / K);
                } else {
                    c.pos = dest;
                }
            } else if (b4_ == base + w - 30) {
                c.ch1 = mul(dest - c.pos, 0.1f);
                c.vt = 0.02f * (dest.y - c.pos.y) + 0.26f;
                c.turn = 0.1f;
            } else if (b4_ > base + w - 30) {
                c.ch1 = mul(dest - c.pos, 0.1f);
            }
            ++k;
        }
        return;
    }
    if (d8_ != 0 || s == 10 || s == 4 || s == 7 || s == 11) {       // CONTROL
        for (Boss05Minion& c : M) { c.target = -2; c.sub = 0x1e; c.mode = 0; c.def = minion_def_; }
    } else if (0.1f * max_hp_ >= hp_) {
        for (Boss05Minion& c : M) if (c.hp >= 1.0f) { c.target = 0; c.sub = 0x1e; c.mode = 1; c.def = minion_def_; }
    } else if (dist2_ > 324.0f) {
        int kk = 0;
        for (Boss05Minion& c : M)
            if (c.hp >= 1.0f) {
                c.target = kk == 0 ? 0 : (std::size_t(kk - 1) < in_->pool_ids.size() ? in_->pool_ids[std::size_t(kk - 1)] : 0);
                ++kk; c.sub = 0x1e; c.mode = 1; c.def = minion_def_;
            }
    } else {
        for (Boss05Minion& c : M) if (c.hp >= 1.0f) { c.target = -2; c.sub = 0x1e; c.mode = 0; c.def = minion_def_; }
    }
}

// §D.6 the death path.
void Boss05::death() {
    if (sec_ != kDead) set_state(kDead);
    const bool r291 = in_->flag291;
    if (b4_ == 0) {
        out_->player_iframes = 10u;
        c8_ = d8_ = b8_ = 0; dc_ = 700;
        out_->kill_376 = true;
        for (int h = 0; h < 4; ++h) if (aura_[std::size_t(h)]) { out_->kill_handles.push_back(h + 1); aura_[std::size_t(h)] = false; }
        for (Boss05Minion& c : *mins_)
            if (c.hp >= 1.0f) {
                if (ac_ != 0) c.hp = 0;
                else { c.def = 999.0f; hp_ = 1.0f; }                 // the guts
            }
        return;
    }
    if (b4_ == 1 && !r291) { out_->player_iframes = 100u; speed(0.5f); }
    if (b4_ == 2) {
        out_->sounds.push_back(286);
        out_->gauge_hide = true;
        if (ac_ == 0) out_->exp = true;
        Boss05Effect& a = fx(88, {pos_.x, pos_.y + 3.5f, pos_.z}); a.boss_stat = false;
        Boss05Effect& b = fx(99, {pos_.x, pos_.y + 3.5f, pos_.z}); b.boss_stat = false; b.scale = {1.3f, 1.3f, 1.3f};
        out_->sounds.push_back(576);
    }
    if (b4_ == 20) shake(190, {0.2f, 0.2f, 0});
    if (b4_ >= 3 && b4_ <= 199 && b4_ % 60 == 0) out_->rumble.push_back(5);
    if (b4_ == 290) out_->sounds.push_back(188);
    if (b4_ == 340 && !r291) {
        static const float kS[6] = {1, 2, 3, 4, 3, 4}, kD[6] = {0, 0, 0, 1, 2, 2};
        for (int i = 0; i < 6; ++i) {
            Boss05Effect& e = fx(346, {pos_.x, pos_.y + kD[i], pos_.z});
            e.scale = {kS[i], kS[i], kS[i]}; e.boss_stat = false;
        }
    }
    if (b4_ == 350 && !r291) {
        out_->zoom = std::make_pair(0.8f, -0.05f);
        out_->sounds.push_back(287); out_->rumble.push_back(3);
        shake(5, {0.2f, 0.2f, 0});
        out_->alpha = 0.0f;                                        // FUN_00504a40: alpha target 0 over 4 frames
    }
    if (b4_ > 350 && !r291 && b4_ <= 354 && rush_) {               // alpha <= 0.2 at ~354 [H the 0.25 step]
        if (b4_ == 354) out_->flags.push_back({156, 0});
    }
    if (b4_ == 500) {
        out_->flags.push_back({224, 1});
        fwd_ = {}; yaw_ = 0;
        if (r291) { hp_ = max_hp_; ac_ = 1; }
        else { out_->destroy = true; destroyed_ = true; }
    }
}

}  // namespace cleria
