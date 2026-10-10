// Khonsclard (boss04) — see boss04.hpp and build/boss_re/KHONSCLARD.md.
#include "cleria/boss04.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cleria {
namespace boss04 {

const ClipInfo kClipTable[kClips] = {
    {"STAND", 40, 99, true}, {"FALL", 150, 165, false}, {"DOWN", 170, 199, true}, {"UP", 210, 240, false},
    {"WAVE", 250, 290, false}, {"ARM1", 300, 320, false}, {"ARM2", 320, 340, false}, {"ARM3", 340, 360, false},
    {"ARM4", 360, 380, false}, {"R_FALL", 390, 400, false}, {"REST", 410, 439, true}, {"ATTACK", 450, 456, false},
    {"DEATH", 470, 530, false}, {"START_WAIT", 540, 540, true}, {"START", 540, 630, false},
};

std::string node_name(int k) {
    if (k == 0) return "root";
    if (k <= 16) return "prickle" + std::to_string((k + 1) / 2) + ((k & 1) ? "_a" : "_b");
    if (k <= 24) return "u" + std::to_string(k - 16);
    if (k <= 35) return "ta" + std::to_string(k - 24);
    if (k <= 46) return "tb" + std::to_string(k - 35);
    if (k <= 57) return "tc" + std::to_string(k - 46);
    if (k <= 62) return "fang" + std::to_string(k - 57);
    if (k == 63) return "eye_l";
    if (k == 64) return "eye_r";
    return "core";
}

const Vec3 kHoles[kPods] = {{-4.25f, -3, 36}, {4.25f, -3, 36}, {-8.5f, -3, 32.5f},
                            {8.5f, -3, 32.5f}, {-4.25f, -3, 29}, {4.25f, -3, 29}};

// [0] = 0x68b728 (every difficulty but Nightmare), [1] = 0x68b668 (Nightmare) [§A.5.1, §B.1.0].
const AttackRow kAttackTable[2][12] = {
    {{0, 60, 2, 100}, {0, 5, 6, 80}, {0, 25, 7, 70}, {0, 8, 8, 110}, {0, 255, 0, 40}, {2, 0, 255, 20},
     {2, 0, 255, 10}, {}, {}, {}, {}, {}},
    {{0, 30, 4, 90}, {0, 5, 6, 70}, {0, 20, 7, 60}, {0, 6, 16, 100}, {0, 255, 0, 30}, {2, 0, 255, 20},
     {2, 0, 255, 10}, {}, {}, {}, {}, {}},
};

}  // namespace boss04

using namespace boss04;

namespace {
float fbits(int i) { float f; std::memcpy(&f, &i, 4); return f; }
float cosf_(float a) { return float(std::cos(double(a))); }
float sinf_(float a) { return float(std::sin(double(a))); }
Vec3 norm_xz(float x, float z) {
    const float l = std::sqrt(x * x + z * z);
    return l == 0.0f ? Vec3{} : Vec3{x / l, 0.0f, z / l};
}
constexpr float kPi = 3.14159265358979f;
Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
}  // namespace

Boss04::Boss04(const Boss04Setup& s) {
    bb5c_ = s.diff_index; easy_ = s.diff_easy; very_easy_ = s.diff_very_easy; rush_ = s.boss_rush;
    model_nodes_ = s.model_nodes;
    pos_ = s.pos;
    level_ = s.level;
    exp_b0_ = s.exp_b0;
    // FUN_00443b80 (EEX record 3) [BYTES 0x443c47..0x443c63]: HP x {1.0, 1.25, 1.5}[bb5c]; Very Easy x0.75,
    // STR -1, DEF -2; Easy DEF -1.
    static const float kHpMul[3] = {1.0f, 1.25f, 1.5f};
    max_hp_ = s.eex_hp * kHpMul[std::clamp(bb5c_, 0, 2)];
    str_ = s.eex_str; def_ = s.eex_def;
    if (very_easy_) { max_hp_ *= 0.75f; str_ -= 1.0f; def_ -= 2.0f; }
    else if (easy_) def_ -= 1.0f;
    hp_ = max_hp_;
    if (!easy_) str_ += 1.0f;                                     // 0x527dff
    // The eight parts (§A.2.1 item 7): the obj block copied, then per part.
    static const float kPodHp[3] = {600, 900, 1100};             // i16 0x68b910
    const float podhp = kPodHp[std::clamp(bb5c_, 0, 2)];
    for (int i = 0; i < kParts; ++i) {
        Boss04Part& p = parts_[std::size_t(i)];
        p.hp = p.max_hp = max_hp_; p.str = str_; p.def = def_;
        p.resist[0] = 0.5f;                                        // +0xb4 = 0.5; +0xb8.. [H] 0
        if (i == 7) { p.hp = p.max_hp = podhp - (easy_ ? 200.0f : 0.0f); }
        else if (i >= 1) { p.hp = p.max_hp = podhp - (easy_ ? 100.0f : 0.0f); p.def = 999.0f; }
        else { p.hp = max_hp_ * 10.0f; }                           // the core: an inert x10 pool
    }
    parts_[7].hurt_radius = 4.0f; parts_[7].half_h = 3.0f; parts_[7].shape = 1;
    parts_[0].hurt_radius = 0.9f;
    def_save_ = def_;                                              // 0x248 = 0x24c (unsoftened)
    parts_[0].def = 999.0f;                                        // the core armoured
    table_ = {};
    for (int a = 0; a < 12; ++a) table_[std::size_t(a)] = kAttackTable[bb5c_ == 2 ? 1 : 0][a];
    // Retry softening of obj STR / DEF (not in boss rush): only the effects' / bullets' STR feels it.
    if (!rush_ && s.retries > 0) {
        const int n = s.retries;
        float k = 0;
        if (very_easy_) k = float(std::pow(double(n), 1.25));
        else if (easy_) k = float(n < 20 ? n >> 1 : n - 10);
        str_ = std::max(1.0f, str_ - k);
        def_ = std::max(1.0f, def_ - k);
    }
    ctor_.clips.push_back(kStand);                                 // SetClip(STAND) [BYTES 0x528441]
    atk_ = 0; prev_ = 0;                                           // 0x160 = 0 from the memset: attack 0 first
}

std::uint32_t Boss04::draw() { return in_ && in_->rng ? in_->rng() : 0u; }

float Boss04::U() {
    const std::uint32_t b = (draw() & 0x7fffffu) | 0x3f800000u;
    float f; std::memcpy(&f, &b, 4);
    return f - 1.0f;
}

void Boss04::set_clip(int c) {
    out_->clips.push_back(c);
    clip_ = c; done_ = false;
    frame_ = float(kClipTable[c].start);
}

Boss04Effect& Boss04::fx(int id, const Vec3& p) {
    Boss04Effect e; e.id = id; e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}

void Boss04::bullet(int kind, int eff, const Vec3& p, const Vec3& d, std::initializer_list<std::pair<int, float>> f) {
    Boss04Bullet b; b.kind = kind; b.effect = eff; b.pos = p; b.dir = d;
    for (const auto& [i, v] : f) b.f[i] = v;
    out_->bullets.push_back(b);
}

void Boss04::add_part_damage(int i, float dmg) {
    Boss04Part& p = parts_[std::size_t(i)];
    p.dmg += dmg;
    if (i != 0) { p.hp -= dmg; if (p.hp < 1.0f) p.hp = 0.0f; }    // the hit path (once; not the core's pool)
}

bool Boss04::part_hittable(int i) const { return !destroyed_ && parts_[std::size_t(i)].hp >= 1.0f; }

// ---------------------------------------------------------------------------
Boss04Output Boss04::tick(const Boss04Input& in) {
    Boss04Output out;
    if (destroyed_) return out;
    in_ = &in; out_ = &out;
    clip_ = in.clip; done_ = in.clip_done; frame_ = in.clip_frame;
    emergence();                                                   // FUN_0052a2b0, every frame
    parts();                                                       // FUN_00528570, every frame
    if (in.fight) {
        pit();                                                     // FUN_00528ac0
        if (hp_ >= 1.0f) {
            dispatch_clips();
            pods_tick();                                           // ends with FUN_005291c0
            selector();                                            // FUN_0052c680
            ++frames_;
        } else {
            death();
            ++dk_;                                                 // blk+0x348++ (every dead frame)
        }
        if (flash_ != 0.0f) { flash_ -= 0.075f; if (flash_ < 0.0f) flash_ = 0.0f; }   // 0x6bd174
        if (!destroyed_ && !(bar_last_ == hp_)) {                  // FUN_0043a460, lenScale 6500, emblem 4
            bar_last_ = hp_;
            out.gauge = Boss04Gauge{hp_, max_hp_, 6500.0f, 4};
        }
    }
    in_ = nullptr; out_ = nullptr;
    return out;
}

// §B.3: the START emergence (sand puffs at the model's nodes; four damaging shockwaves).
void Boss04::emergence() {
    if (clip_ != kStart) return;
    const int n = std::min<int>(model_nodes_, int(in_->model_nodes.size()));
    for (int m = 0; m < n; ++m) {
        const Vec3 Q = in_->model_nodes[std::size_t(m)];
        if (R(128) != 0 || m >= 100) continue;
        const std::uint32_t d2 = draw() >> 17, d3 = draw() >> 17, d4 = draw() >> 17;
        const Vec3 sc{1.5f + 0.05f * float(d4 % 50), 1.5f + 0.05f * float(d3 % 50), 1.5f + 0.05f * float(d2 % 50)};
        if (R(8) == 0) {                                           // the dust FUN_00584550, its size drawn first
            const float size = float(R(100)) * 0.005f + 0.55f;
            if (in_->dust) in_->dust(Q, size);                     // [H] at the node
            continue;
        }
        Boss04Effect& e = fx(355, Q);                              // bs_03_kemuri (no hit)
        e.scale = sc; e.rtype = -1;
    }
    if (frame_ == 582.0f || frame_ == 600.0f || frame_ == 614.0f || frame_ == 627.0f) {
        Boss04Effect& e = fx(370, {pos_.x, pos_.y + 0.5f, pos_.z - 1.0f});   // bs_04_syougekiha01 (hits)
        e.scale = {0.6f, 0.8f, 0.6f};
        if (frame_ == 627.0f) out_->zoom = std::make_pair(0.5f, -0.006f);
    }
}

// §A.2.3 / §A.3.1 / §A.3.3: place the parts, consume their damage, the part-7 stagger.
void Boss04::parts() {
    for (int i = 0; i < kParts; ++i) {
        Boss04Part& p = parts_[std::size_t(i)];
        if (i == 0) p.centre = in_->nodes[kNodeCore];
        else if (i <= 6) p.centre = in_->pods[std::size_t(i - 1)] + Vec3{0, 1.5f, 0};
        else p.centre = parts_[0].centre - Vec3{0, 6.5f, 0};
        p.hurt_radius = i == 0 ? 0.9f : i == 7 ? 4.0f : 0.7f;
        p.broken = false;
        const float d = p.dmg;
        if (d != 0.0f || std::isnan(d)) { flash_ = 1.65f; p.dmg = 0.0f; }
        if (!(d > 0.0f)) continue;
        if (i == 0) {
            hp_ -= d;
            if (hp_ < 1.0f) hp_ = 0.0f;
        } else if (i == 7) {
            if (bb5c_ == 0) out_->body_gauge = Boss04MiniGauge{255, p.hp, p.max_hp, pos_ - Vec3{0, 3.0f, 0}};
        } else {
            out_->pod_clips.push_back({i - 1, "DAMAGE"});
            if (bb5c_ == 0) out_->pod_gauge = Boss04MiniGauge{255, p.hp, p.max_hp, in_->pods[std::size_t(i - 1)]};
        }
    }
    Boss04Part& p7 = parts_[7];
    if (p7.hp < 1.0f) { p7.hp = p7.max_hp; p7.def = 999.0f; abort_attack(); }
    if (p7.def >= 999.0f && ++stagger_ > 900) { stagger_ = 0; p7.hp = p7.max_hp; p7.def = def_save_; }
}

void Boss04::abort_attack() { shots_ = 0; sub_ = 0; rest_ = 60; atk_ = 11; prev_ = 11; }

// §A.6: the ant-lion pit.
void Boss04::pit() {
    const float H = (max_hp_ - hp_) / max_hp_;
    if (suction_mode_ == 0) {
        if (s_ > 0.0f) { s_ -= 0.0001f; if (s_ < 0.0f) s_ = 0.0f; }
        else if (s_ < 0.0f) { s_ += 0.0001f; if (s_ > 0.0f) s_ = 0.0f; }
    } else if (suction_mode_ == 1) {
        if (H * 0.015f + 0.005f > s_) s_ += 0.0001f;
    } else if (suction_mode_ == 2) {
        if (s_ > H * 0.015f - 0.005f) s_ -= 0.0001f;
    }
    out_->pit_scroll = s_;
    const Vec3 P = in_->player_pos;
    if (in_->player_hp >= 1.0f && in_->player_grounded && in_->player_floor <= 0.0f) {
        const float dx = pos_.x - P.x, dz = pos_.z - P.z;
        if (dx * dx + dz * dz <= 900.0f) {
            const Vec3 n = norm_xz(dx, dz);
            out_->player_push = Vec3{n.x * 5.0f * s_, 0.0f, n.z * 5.0f * s_};
        }
    }
}

// §A.4.2: the clip dispatcher (alive frames).
void Boss04::dispatch_clips() {
    const int c = clip_;
    if (c == kRFall) { if (done_) set_clip(kRest); }
    else if (c == kRest) { if (stagger_ == 0) set_clip(kStand); }
    else if (c == kFall) { if (done_) set_clip(kDown); }
    else if (c == kDown) { if (down_ == 0) set_clip(kUp); }
    else if (down_) set_clip(kFall);
    else if (stagger_) set_clip(kRFall);
    else if (done_) set_clip(kStand);
}

// FUN_00528fb0: a free hole for pod n (one R(6)); teleport there buried at y -3.
void Boss04::pick_hole(int n) {
    pods_[std::size_t(n)].slot = 0xff;
    int h = int(R(6));
    for (int pass = 0; pass < 6; ++pass) {
        bool used = false;
        for (const Boss04Pod& q : pods_) used |= q.slot == h;
        if (!used) break;
        h = (h + 1) % 6;
    }
    pods_[std::size_t(n)].slot = h;
    out_->pod_moves.push_back({n, kHoles[h], std::nullopt});
}

// §B.2.2: the six pods, then FUN_005291c0, then the aura countdown.
void Boss04::pods_tick() {
    static const int kTa[3] = {500, 400, 340}, kTb[3] = {1000, 900, 840};   // u16 0x68b8f0 / 0x68b8f8
    bool healed = false;
    for (int n = 0; n < kPods; ++n) {
        Boss04Pod& pd = pods_[std::size_t(n)];
        Boss04Part& part = parts_[std::size_t(n + 1)];
        const Vec3 pp = in_->pods[std::size_t(n)];
        if (in_->pod_damage_done[std::size_t(n)]) out_->pod_clips.push_back({n, "STAND"});
        bool smoke = false;
        switch (pd.state) {
            case 0: {
                if (++pd.t < 120 && pd.t % 6 == 0) smoke = true;
                if (part.hp < 1.0f) {                              // BREAK
                    part.broken = true; part.def = 999.0f;
                    out_->pod_moves.push_back({n, std::nullopt, -3.0f});
                    pd.state = 1; pd.t = 0; pd.h = 0;
                    se(262); out_->rumble.push_back(0);
                }
                const int d = std::clamp(bb5c_, 0, 2);
                int T = kTa[d] * n + kTb[d];
                if (easy_) T *= 2;
                ++pd.h;
                if (pd.h == T - 60) {                              // the charge
                    fx(372, {pp.x, pp.y + 2.25f, pp.z - 0.1f}).boss_stat = false;
                    se(259);
                }
                if (pd.h > T && pd.h % 5 == 0) {                   // the beam (kind 0, harmless)
                    const Vec3 p{pp.x, pp.y + 2.25f, pp.z - 1.0f};
                    Vec3 dv = Vec3{pos_.x, pos_.y + 6.0f, pos_.z} - p;
                    const float l = std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
                    if (l > 0) dv = Vec3{dv.x / l, dv.y / l, dv.z / l};
                    bullet(0, 369, p, dv, {{4, 1.0f}});
                }
                if (pd.h > T + 60 && pd.h % 20 == 0) {             // HEAL
                    hp_ = std::min(hp_ + 5.0f, max_hp_);
                    if (!aura_) { aura_ = true; Boss04Effect& e = fx(373, pos_); e.boss_stat = false; e.tag = 1; }
                    if (!healed) { healed = true; se(260); }
                    aura_cd_ = 21;
                }
                break;
            }
            case 1:
                if (++pd.t < 120 && pd.t % 6 == 0) smoke = true;
                if (pd.t > pod_limit_ && down_ == 0) {
                    pd.t = 0; pd.state = 2; pick_hole(n);
                    out_->pod_moves.push_back({n, std::nullopt, -0.8f});
                    se(261);
                }
                break;
            case 2:
                if (++pd.t < 120 && pd.t % 6 == 0) smoke = true;
                if (pd.t > 60) { part.broken = false; part.hp = part.max_hp; part.def = def_save_; pd.t = 0; pd.state = 0; }
                break;
            case 3:
                if (++pd.t < 120 && pd.t % 6 == 0) smoke = true;
                if (pd.t > 300) {
                    pd.t = 0; pd.state = 2; pick_hole(n);
                    out_->pod_moves.push_back({n, std::nullopt, -0.8f});
                    se(261);
                }
                break;
            case 4:
                if (pd.enabled) {
                    part.broken = false; part.hp = part.max_hp; part.def = def_save_;
                    pd.t = 0; pd.state = 0; pick_hole(n);
                    out_->pod_moves.push_back({n, std::nullopt, -0.8f});
                }
                break;
            default: break;
        }
        if (smoke) {
            const float u = U() + 1.0f;                            // asfloat(...) in [1, 2): ONE draw
            Boss04Effect& e = fx(355, {pp.x, 0.3f, pp.z});
            e.scale = {u, u, u}; e.boss_stat = false;
        }
    }
    bands();
    if (aura_) {
        if (aura_cd_ != 0) --aura_cd_;
        else { out_->kill_aura = true; aura_ = false; }
    }
}

void Boss04::reset_after_down() {
    down_ = 0; suction_mode_ = 1; stagger_ = 0;
    parts_[7].hp = parts_[7].max_hp; parts_[7].def = def_save_;
    for (Boss04Pod& pd : pods_) if (pd.enabled && pd.state == 1) pd.t = 9999;
    abort_attack();
}

// §A.3.4: HP bands, the down, the get-up.
void Boss04::bands() {
    int E = 0, S = 0;
    for (const Boss04Pod& pd : pods_) { E += pd.enabled; S += pd.enabled && pd.state == 1; }
    bool over = false;
    if (E != 0 && E == S) {
        parts_[0].def = def_save_;                                 // the core vulnerable
        parts_[7].def = 999.0f;
        if (down_ == 0) { set_clip(kFall); se(265); }
        ++down_; suction_mode_ = 0;
        over = down_ > 600;
    } else {
        parts_[0].def = 999.0f;
    }
    const float f = hp_ / max_hp_;
    static const float kBand[4] = {0.8f, 0.6f, 0.4f, 0.2f};
    static const int kLimit[4] = {1500, 2000, 2500, 3000};
    if (band_ == 0) {
        pods_[0].enabled = pods_[1].enabled = true; band_ = 1; pod_limit_ = 1000;
    } else if (band_ >= 1 && band_ <= 4 && f < kBand[band_ - 1]) {
        pods_[std::size_t(band_ + 1)].enabled = true;
        pod_limit_ = kLimit[band_ - 1];
        ++band_;
        reset_after_down();
        return;
    }
    if (over) reset_after_down();
}

// §B.1.1: muzzle i at node u(i+1), the world-fixed fan.
void Boss04::fan(int i, float step, Vec3* p, Vec3* d) {
    const Vec3 B = in_->nodes[std::size_t(kNodeU1 + i)];
    const int deg = int(float(i) * step - 70.0f);                  // cvttss2si
    float a;
    if (deg == -1 || deg == 360) { (void)draw(); a = 0.0f; }       // FUN_00443a80: always 0 (the NM quirk)
    else {
        int w = 90 - deg;
        while (w >= 180) w -= 360;
        while (w < -180) w += 360;
        a = float(w) * (kPi / 180.0f);
    }
    a = a - 1.5707964f - 1.5707964f;
    *d = norm_xz(cosf_(a), sinf_(a));
    *p = {B.x + 2.0f * d->x, B.y + 1.25f, B.z + 2.0f * d->z};
}

// §A.5.1: the selector / attack body.
void Boss04::selector() {
    const Vec3 P = in_->player_pos;
    const Vec3 dd = pos_ - P;
    const float d2 = dd.x * dd.x + dd.y * dd.y + dd.z * dd.z;
    if (down_ != 0 || stagger_ != 0) return;
    int a = atk_;
    if (a < 12 && shots_ > row(a).last) {
        shots_ = 0; sub_ = 0; side_ = 0; prev_ = a; rest_ = row(a).rest; a = atk_ = 11;
    }
    if (a > 11) return;
    switch (a) {
        case 0: a0(); return;
        case 1: a1(); return;
        case 2: a2(); return;
        case 3: a3(); return;
        case 4: a4(); return;
        case 5: a5(); return;
        case 6: a6(); return;
        case 11: break;
        default: return;
    }
    if (rest_ != 0) { --rest_; return; }
    if (d2 < 100.0f && prev_ != 5) { atk_ = 5; return; }
    if (R(100) < 40u && prev_ != 6) { atk_ = 6; return; }
    if (rot_ > 4) { rot_ = 0; return; }
    atk_ = rot_; ++rot_;
}

// §B.1.2..§B.1.6 — every attack body opens with the cadence gate and ends a volley with ++shots.
void Boss04::a0() {
    if (frames_ % std::uint32_t(row(0).period) != 0) return;
    se(254);
    if (clip_ != kAttack) set_clip(kAttack);
    for (int i = 0; i < 8; ++i) {
        Vec3 p, d; fan(i, 20.0f, &p, &d);
        bullet(4, 366, p, d, {{0, 0.0425f}, {1, 0.00125f}, {4, 1.0f}, {5, fbits(0x16c)}, {6, fbits(8)},
                              {7, fbits(3)}, {8, 0.10471976f}, {9, 1.0f}, {10, 20.0f}, {11, 66.0f}});
    }
    ++shots_;
}

void Boss04::a1() {
    if (frames_ % 5u != 0) return;
    if (shots_ == 0) se(258);
    if (clip_ != kAttack) set_clip(kAttack);
    const float acc = bb5c_ == 2 ? 0.0065f : 0.0045f;
    for (int i = 0; i < 8; ++i) {
        Vec3 p, d; fan(i, 20.0f, &p, &d);
        bullet(4, 357, p, d, {{0, 0.09f}, {1, acc}, {4, 1.0f}, {9, 1.0f}});
    }
    ++shots_;
}

void Boss04::a2() {
    if (frames_ % std::uint32_t(row(2).period) != 0) return;
    if (side_ == 0 && sub_ == 0 && in_->player_pos.x < 0.0f) side_ = 1;
    se(255);
    if (clip_ != kAttack) set_clip(kAttack);
    const int j = side_ == 0 ? sub_ : 7 - sub_;
    Vec3 p, d; fan(j, 20.0f, &p, &d);
    bullet(4, 365, p, d, {{0, 0.09f}, {1, 0.007f}, {4, 1.0f}, {7, fbits(18)}, {8, 6.2831855f}, {9, 1.0f},
                          {10, 17.0f}, {11, 19.0f}});
    ++sub_; ++shots_;
}

void Boss04::a3() {
    if (frames_ % std::uint32_t(row(3).period) != 0) return;
    if (shots_ == 0) se(256);
    if (clip_ != kAttack) set_clip(kAttack);
    float step = 20.0f, speed = 0.10f, acc = 0.001f;
    if (bb5c_ == 2) { step = 16.0f + 6.0f * U(); speed = 0.12f; acc = 0.002f; }
    for (int i = 0; i < 8; ++i) {
        Vec3 p, d; fan(i, step, &p, &d);
        bullet(6, 358, p, d, {{0, speed}, {1, acc}, {4, 1.0f}, {7, fbits(20)}, {8, 0.017453f}, {9, 5.0f}, {10, 0.7f}});
    }
    ++shots_;
}

void Boss04::a4() {
    if (frames_ % 255u != 0) return;
    if (shots_ == 0) { se(257); out_->rumble.push_back(4); }
    if (clip_ != kAttack) set_clip(kAttack);
    const float sp = bb5c_ == 2 ? 0.25f : 0.15f, acc = bb5c_ == 2 ? 0.0015f : 0.001f;
    for (int i = 0; i < 8; ++i) {
        Vec3 p, d; fan(i, 20.0f, &p, &d);
        bullet(5, 356, p, d, {{0, sp}, {1, acc}, {4, 1.0f}, {5, fbits(0x167)}, {6, fbits(5)}, {9, 1.0f}});
    }
    ++shots_;
}

// §A.5.2: attack 5, WAVE (the near-player burst).
void Boss04::a5() {
    if (clip_ != kWave) { set_clip(kWave); return; }
    const int t = sub_;
    if (t == 40) {
        se(263);
        Boss04Effect& e = fx(360, in_->nodes[kNodeCore] + Vec3{0, 0.6f, -1.2f});
        e.knock = 1.0f;
    }
    if (t == 52) se(253);
    if (t == 60) {
        out_->shake = std::make_pair(45, Vec3{0.25f, 0.25f, 0});
        fx(370, pos_ + Vec3{0, 0.5f, -1.0f});
        out_->rumble.push_back(3);
        for (int k = 0; k < 8; ++k) {
            const Vec3 N = in_->nodes[std::size_t(2 + 2 * k)];     // prickle(k+1)_b
            const Vec3 d = norm_xz(N.x - pos_.x, N.z - pos_.z);
            bullet(0, 368, N + d + Vec3{0, 0.25f, 0}, d, {{4, 1.0f}});
        }
    }
    if (t == 70) {
        const Vec3 P = in_->player_pos;
        for (int k = 0; k < 12; ++k) {
            const float y = P.y + 15.0f + 5.0f * U();
            const float x = P.x - 4.0f + 8.0f * U();
            const float z = P.z - 4.0f + 8.0f * U();
            bullet(1, 362, {x, y, z}, {0, -1, 0}, {{0, 0.1f}, {1, 0.005f}, {4, 1.0f}, {9, 1.0f}});
        }
    }
    if (t > 140) { set_clip(kStand); shots_ = row(atk_).last + 1; }
    sub_ = t + 1;
}

// §A.5.3: attack 6, the alternating prickle shot.
void Boss04::a6() {
    static const int kN[3] = {4, 6, 8}, kP[3] = {50, 37, 30};      // i16 0x68b908 / 0x68b900
    const int d = std::clamp(bb5c_, 0, 2);
    if (sub_ == 0) {
        if (shots_ == 0) side_ = in_->player_pos.x >= 0.0f ? 2 : 1;
        else side_ = side_ == 1 ? 2 : side_ - 1;
        set_clip(side_ == 1 ? kArm2 : kArm3);
    }
    if (sub_ == 30) {
        out_->shake = std::make_pair(12, Vec3{0.25f, 0.25f, 0});
        se(side_ == 1 ? 251 : 252);
        out_->rumble.push_back(1);
        const Vec3 A = in_->nodes[side_ == 1 ? 1 : 15], B = in_->nodes[side_ == 1 ? 2 : 16];
        const Vec3 dv = norm_xz(B.x - A.x, B.z - A.z);
        bullet(4, 371, A + dv + Vec3{0, 0.25f, 0}, dv, {{0, 0.05f}, {1, 0.001f}, {4, 1.0f}, {9, 1.0f}});
        ++shots_;
    }
    ++sub_;
    if (shots_ >= kN[d]) { set_clip(kStand); shots_ = row(atk_).last + 1; }
    if (sub_ > kP[d]) sub_ = 0;
}

// §A.8: the death.
void Boss04::death() {
    flag(194, 0);
    const float php = in_->player_hp;
    if (!killed_) {
        if (rush_ && php >= 1.0f) out_->player_iframes = true;
        if (clip_ != kDeath) set_clip(kDeath);
        if (!rush_ && exp_b0_ != 0.0f)
            out_->exp = Boss04Exp{level_, exp_b0_, very_easy_ ? 2.0f : easy_ ? 1.1f : 1.0f};
        if (php >= 1.0f) {
            killed_ = true;
            if (!rush_) flag(223, 1);
        }
    }
    if (!killed_) return;
    const int k = dk_;
    if (k == 0) { suction_mode_ = 0; se(186); se(577); }
    alpha_ -= 0.00294118f;                                         // 1/340
    if (alpha_ <= 0.2f) {
        if (rush_) flag(156, 0);
        else {
            if (php >= 1.0f && !buffs_cleared_) { buffs_cleared_ = true; out_->clear_buffs = true; }
            out_->achievement = true;
        }
    }
    if (alpha_ <= 0.0f) {
        alpha_ = 0.0f; out_->gauge_hide = true; se(188); out_->destroy = true; destroyed_ = true;
        return;
    }
    for (int n = 0; n < model_nodes_; ++n) {                       // the debris, 4 draws per node
        const float z = pos_.z - 4.0f + 8.0f * U();
        const float y = pos_.y - 2.0f + 10.0f * U();
        const float x = pos_.x - 4.0f + 8.0f * U();
        const std::uint32_t q = R(64);
        if (q != 0 || n >= 100 || debris_[std::size_t(n)]) continue;
        const float c = float(R(50)), b = float(R(50)), a = float(R(50));
        if (R(8) == 0) {                                           // the dust FUN_00584550, its size drawn first
            const float size = float(R(100)) * 0.005f + 0.55f;
            if (in_->dust) in_->dust({x, y, z}, size);             // [H] at the drawn point
        }
        else {
            Boss04Effect& e = fx(87, {x, y, z});
            e.scale = {1.1f * (a * 0.01f + 0.3f), 1.1f * (b * 0.01f + 0.3f), 1.1f * (c * 0.01f + 0.3f)};
            e.rtype = -1;
        }
        debris_[std::size_t(n)] = true;
    }
    if (k == 1) { Boss04Effect& e = fx(99, pos_ + Vec3{0, 4, 0}); e.scale = {1.25f, 1.25f, 1.25f}; e.rtype = -1; }
    if (k == 120) { se(576); fx(88, pos_ + Vec3{0, 4, 0}).rtype = -1; }
    if (k % 60 == 0) out_->rumble.push_back(5);
}

}  // namespace cleria
