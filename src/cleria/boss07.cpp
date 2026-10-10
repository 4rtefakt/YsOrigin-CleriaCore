// Dalles (boss07) — see boss07.hpp and build/boss_re/DALLES*.md.
#include "cleria/boss07.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cleria {
namespace boss07 {

const SectionInfo kSectionTable[kSections] = {
    {"N_WAIT", 310, 349, true},          {"N_DAMAGE", 351, 359, false},      {"N2W_CHANGE", 360, 399, false},
    {"N2F_CHANGE", 410, 449, false},     {"N2G_CHANGE", 460, 499, false},    {"W_WAIT", 10, 49, true},
    {"W_DAMAGE", 51, 59, false},         {"F_WAIT", 110, 149, true},         {"F_DAMAGE", 151, 159, false},
    {"G_WAIT", 210, 249, true},          {"G_DAMAGE", 251, 259, false},      {"N_SHOOT_L", 511, 549, false},
    {"N_SHOOT_R", 551, 589, false},      {"N_IMPACT_R", 591, 629, false},    {"KIAI_START", 671, 679, false},
    {"KIAI_LOOP", 680, 719, true},       {"KIAI_HISSATU_TUNAGI", 721, 725, false}, {"METEOR", 730, 769, true},
    {"METEOR_FROM_WAIT", 771, 779, false}, {"SIS_LAZER_START", 781, 789, false}, {"SIS_LAZER", 790, 829, true},
};

void advance_clip(int section, float& frame, std::uint32_t& count, float rate) {
    const SectionInfo& s = kSectionTable[section];
    float f = frame + rate;
    if (s.loop) {                                                  // next == -2: inc @0x5f5efa
        while (f > float(s.last) + 1.0f) { f -= float(s.last - s.first + 1); ++count; }
    } else if (f >= float(s.last)) {                               // stop: mov @0x5f5f32
        f = float(s.last); count = 1;
    }
    frame = f;
}

const std::int16_t kCooldown[3] = {110, 100, 90};
const std::int16_t kShotBase[3] = {180, 170, 160};
const std::int16_t kOptionHp[3] = {1000, 1200, 1400};
const std::uint32_t kWindow[3] = {600, 700, 760};
const std::uint32_t kDarkWindow[3] = {700, 800, 860};
const std::int16_t kBarrierHits[4][5] = {
    {0, 25, 25, 25, 0}, {0, 25, 25, 25, 0}, {0, 10, 25, 25, 0}, {0, 15, 25, 25, 0},   // Adol, Yunica, Hugo, Toal
};
const std::int16_t kCircleDelay[3] = {120, 110, 100};
const float kSuctionA[4] = {0.0f, 0.0f, -0.0005f, 0.001f};         // 0xba03126f / 0x3a83126f
const float kSuctionB[4] = {0.0004f, 0.0004f, 0.0002f, 0.001f};
const std::uint32_t kAttacks0[6] = {0, 2, 1, 3, 4, 5};
const std::uint32_t kAttacks1[7] = {0, 2, 1, 3, 4, 5, 6};
const std::uint32_t kAttacks2[3][8] = {{0, 2, 7, 1, 3, 8, 4, 5}, {7, 7, 7, 7, 8, 8, 4, 5}, {4, 4, 5, 5, 7, 7, 8, 8}};

int barrier_threshold(int character, int barrier, int diff_index) {
    const int base = kBarrierHits[std::clamp(character, 0, 3)][std::clamp(barrier, 0, 4)];
    return base + 2 * (base / 5) * diff_index;                     // signed /5 (magic 0x66666667)
}

}  // namespace boss07

using namespace boss07;
using ef2sim::M4;

namespace {
constexpr float kPi = 3.1415927f;        // 0x6bd6ac
constexpr float kHalfPi = 1.5707964f;    // 0x6bd4e4
constexpr float k2Pi = 6.2831855f;       // 0x6bd898
constexpr double k2PiD = 6.2831854820251465;   // f64 0x6bd610
constexpr float kDeg = 0.017453292f;     // 0x6bd0dc

float fb(std::uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
float ibits(std::uint32_t i) { float f; std::memcpy(&f, &i, 4); return f; }
float at2(float a, float b) { return float(std::atan2(double(a), double(b))); }   // FUN_0066f80a / FUN_00427410
float cosf_(float a) { return float(std::cos(double(a))); }        // FUN_0041c530
float sinf_(float a) { return float(std::sin(double(a))); }        // FUN_0041c510
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 nrm(const Vec3& a) {                                          // FUN_0042cc50: zero length -> (0, 0, 0)
    const float l = std::sqrt((a.y * a.y + a.x * a.x) + a.z * a.z);
    return l == 0.0f ? Vec3{} : mul(a, 1.0f / l);
}
Vec3 row3(const M4& m) { return {m.m[3][0], m.m[3][1], m.m[3][2]}; }
float ang_norm(float x) {                                          // FUN_005ef010 [AB §0]
    if (x > k2Pi) return float(std::fmod(double(x), k2PiD));
    if (x < 0.0f) return float(std::fmod(double(x), k2PiD)) + k2Pi;
    return x;
}
float wrap_pi(float d) {                                           // the inline pattern [AB §0]
    if (0.0f > d) { if (d < -kPi) d += k2Pi; }
    else if (d > kPi) d -= k2Pi;
    return d;
}
float fmodf_(float a, float b) { return float(std::fmod(double(a), double(b))); }   // FUN_00444660
float ang_step(float rate, float cur, float tgt) {                 // FUN_005eecc0 [AB §0]
    float r;
    if (rate >= kPi || cur == tgt) r = tgt;
    else {
        const float nt = ang_norm(tgt);
        const float a_plus = std::fabs(wrap_pi(ang_norm(cur + rate) - nt));
        const float a_minus = std::fabs(wrap_pi(ang_norm(cur - rate) - nt));
        if (a_minus > a_plus) r = rate >= a_plus ? tgt : cur + rate;
        else r = rate >= a_minus ? tgt : cur - rate;
    }
    if (r >= kPi) return fmodf_(r + kPi, k2Pi) - kPi;
    if (r < -kPi) return fmodf_(r - kPi, k2Pi) + kPi;
    return r;
}
bool between(float f, float lo, float hi) { return lo <= f && f <= hi; }
}  // namespace

struct Boss07::Scope {
    Boss07& b;
    const Boss07Input* in; Boss07Output* out;
    Scope(Boss07& bb, const Boss07Input* i, Boss07Output& o) : b(bb), in(bb.in_), out(bb.out_) {
        b.in_ = i; b.out_ = &o;
        if (i) {
            b.sec_ = i->section; b.cnt_ = i->count; b.f_ = i->frame;
            b.opt_cnt_ = i->opt_count; b.ppos_ = i->player_pos; b.psec_ = i->player_section;
        }
    }
    ~Scope() { b.in_ = in; b.out_ = out; }
};

// FUN_00545940 [P1 §3.3].
Boss07::Boss07(const Boss07Setup& s) {
    char_ = s.character; diff_ = std::clamp(s.diff_index, 0, 2); easy_ = s.diff_easy; very_easy_ = s.diff_very_easy;
    rush_ = s.boss_rush; st_ = s.statics;
    body_.pos = s.pos; body_.pos.y = 4.0f;                         // obj+0x54 = 0x40800000 [0x5459a4]
    // Item 5: FUN_00443b80(6, obj+0x94) — the loader's difficulty scaling [DOC PICTIMOS P12; as boss05.cpp].
    static const float kHpMul[3] = {1.0f, 1.25f, 1.5f};
    float hp = s.eex_hp * kHpMul[diff_], str = s.eex_str, def = s.eex_def;
    if (very_easy_) { hp *= 0.75f; str -= 1.0f; def -= 2.0f; }
    else if (easy_) def -= 1.0f;
    if (char_ == 3) str += 5.0f;                                   // Toal [0x545a88..0x545aa1]
    body_.max_hp = body_.hp = hp; body_.str = str;
    // Item 7: three GoXx parts (FUN_005a1a10 type 6, r 0.5): the stat record copied, HP x 10, resists 0.4 x 4.
    for (Boss07Part& p : parts_) { p.radius = 0.5f; p.def = def; p.pool = hp * 10.0f; p.dmg = 0; }
    // Item 8: the body's DEF, after the copy.
    body_.def = 999.0f;
    blk_.drift_scale = 1.0f; blk_.turn_scale = 1.0f; blk_.cooldown = 60; blk_.cam_slot = 1;
    // (the host's at creation: CamSlot(1, 0.025) [0x545cf5] and the emblem word 0x76c214 = 7 [0x54647c])
    // Item 9: the three child meshes at (x, y - 999, z).
    for (Boss07ChildMesh& c : child_) c.pos = {body_.pos.x, body_.pos.y - 999.0f, body_.pos.z};
    // Item 10: option HP.
    blk_.opt_hp[0] = blk_.opt_hp[1] = float(kOptionHp[diff_]);
    // Item 13: the retry softening (skipped in boss rush), then >= 1. The parts' DEF copies are not touched.
    if (!rush_) {
        const int n = s.retries;
        if (very_easy_) { const float k = float(std::pow(double(n), 1.25)); body_.str -= k; body_.def -= k; }
        else if (easy_) { const float k = float(n < 20 ? n >> 1 : n - 10); body_.str -= k; body_.def -= k; }
        body_.str = std::max(1.0f, body_.str); body_.def = std::max(1.0f, body_.def);
    }
    // Item 14: blk+0xd4 = +0xd0 = +0xa4 = 0; SetClip(0 N_WAIT) (the host starts the mesh in N_WAIT).
}

M4 Boss07::matrix() const {
    M4 m = ef2sim::rot_ypr(blk_.rot.y, blk_.rot.x, blk_.rot.z);
    m.m[3][0] = body_.pos.x; m.m[3][1] = body_.pos.y; m.m[3][2] = body_.pos.z;
    return m;
}

std::uint32_t Boss07::W() { return in_ && in_->rng ? in_->rng() : 0u; }
float Boss07::U() { return fb((W() & 0x7fffffu) | 0x3f800000u) - 1.0f; }
float Boss07::Rf(float lo, float hi) { return lo + (hi - lo) * U(); }      // FUN_00429e40
Vec3 Boss07::node(int k) const { return row3(in_->nodes[std::size_t(k)]); }

void Boss07::set_clip(int k) {                                     // FUN_005f5c80(M, blk[0x30 + 4k], 1; xmm3 0)
    out_->clips.push_back({k, k});
    sec_ = k; cnt_ = 0;                                            // zeroes A+0x1c [0x5f5c9f]
    f_ = float(kSectionTable[k].first);                            // [H V1 / G1]
}
void Boss07::queue_motion(int mesh, const char* name) {           // strncpy(+0x424); word +0x443 = 0x100; +0x448 = 0
    out_->motions.push_back({mesh, name});
    if (mesh < 2) opt_cnt_[std::size_t(mesh)] = 0;                 // *(mesh+0x26c)+0x1c = 0
}
void Boss07::kill(int& h) { if (h != 0) { out_->kill_handles.push_back(h); h = 0; } }
Boss07Effect& Boss07::fx(int id, const Vec3& p, Boss07Call call) { // "plain"
    Boss07Effect e; e.call = call; e.id = id; e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}
void Boss07::bullet(bool raw, int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f) {
    Boss07Bullet b; b.raw = raw; b.kind = kind; b.effect = effect; b.pos = pos; b.dir = dir;
    for (int i = 0; i < 13; ++i) b.f[i] = f[i];
    b.flag201 = true;                                              // every bullet of the band: fx+0x201 = 1
    out_->bullets.push_back(b);
}
void Boss07::cam_slot(int n, float speed) { blk_.cam_slot = n; out_->cam_slot = std::make_pair(n, speed); }
void Boss07::player_section(int n) { out_->player.sections.push_back(n); psec_ = n; }   // FUN_00446620

Boss07Hit Boss07::hit_part(int i, int dmg) {                       // FUN_004faf20 [AB §B.2]
    Boss07Part& p = parts_[std::size_t(i)];
    if (1.0f > p.pool) return Boss07Hit::kRefused;                 // 0x4faf67
    p.dmg += float(dmg);                                           // 0x4fb482: also when dmg == 0
    if (dmg > 0) {
        p.pool -= float(dmg);                                      // [not modelled] dmg' (GELALDY G9)
        if (1.0f > p.pool) p.pool = 0.0f;
        return Boss07Hit::kLanded;
    }
    return Boss07Hit::kClank;                                      // case 6, owner != 0, dmg <= 0: SE 368
}

// ---------------------------------------------------------------------------
// FUN_00562990 case 0xb [AB §A.1].
Boss07Output Boss07::tick(const Boss07Input& in) {
    Boss07Output out;
    if (!in.flag156) return out;                                   // nothing of the boss runs
    Scope sc(*this, &in, out);
    turn();
    drift();
    children();
    parts();
    camera();
    if (body_.hp >= 1.0f) {                                        // comiss; jb -> death (a NaN dies)
        option_shot();
        scheduler();
        sequencer();
        executor();
    } else {
        death();
        ++blk_.death_fc;
    }
    ++blk_.fc;
    float f = body_.flash;
    if (f != 0.0f) { f -= 0.075f; body_.flash = f; if (0.0f > f) body_.flash = 0.0f; }
    if (body_.bar_hp != body_.hp) { body_.bar_hp = body_.hp; out.hp_bar = Boss07HpBar{body_.hp, body_.max_hp}; }
    return out;
}

// FUN_00546dc0 [AB §A.2].
void Boss07::turn() {
    Boss07Block& b = blk_;
    b.rot_target = b.rot;
    const float dx = ppos_.x - body_.pos.x, dy = ppos_.y - body_.pos.y, dz = ppos_.z - body_.pos.z;
    const float len = std::sqrt((dy * dy + dx * dx) + dz * dz);
    float nx = 0.0f, nz = 0.0f;
    if (len != 0.0f) { const float inv = 1.0f / len; nz = inv * dz; nx = inv * dx; }
    const float yaw_p = ang_norm(at2(nx, nz) + kPi);
    const float yaw_f = ang_norm(at2(b.fwd.x, b.fwd.z) + kPi);
    const float d = wrap_pi(yaw_p - yaw_f);
    b.rot_target.y = d > 0.0f ? b.rot_target.y + 1.0471976f : b.rot_target.y - 1.0471976f;   // 0x6bd454
    float s;
    if (b.turn_timer != 0) { s = b.turn_scale; --b.turn_timer; }
    else { s = 1.0f; b.turn_scale = 1.0f; }
    const float rate = s * 0.009f;                                 // 0x6bd074
    b.rot.x = ang_step(rate, b.rot.x, b.rot_target.x);
    b.rot.y = ang_step(rate, b.rot.y, b.rot_target.y);
    Vec3 v = ef2sim::transform_coord({0, 0, -1}, ef2sim::rot_ypr(b.rot.y, b.rot.x, b.rot.z));
    v = nrm(v);
    b.fwd0 = v; b.fwd = v; b.fwd0.x = 0.0f;                        // the first copy loses its x
    // SetRot(D, &blk+0x1a0): the body's rotation rows = matrix()
}

// FUN_00547120 [AB §A.3].
void Boss07::drift() {
    Boss07Block& b = blk_;
    const float dx = ppos_.x - body_.pos.x, dz = ppos_.z - body_.pos.z;
    const float q = (dx * dx + 0.0f) + dz * dz;
    float vx = 0, vy = 0, vz = 0;
    if (64.0f > q) {                                               // closer than 8: back off
        const float ex = dx * -1.0f, ez = dz * -1.0f;
        const float l = std::sqrt((ex * ex + 0.0f) + ez * ez);
        if (l != 0.0f) { const float inv = 1.0f / l; vx = ex * inv; vy = inv * 0.0f; vz = ez * inv; }
        vx *= 0.04f; vy *= 0.04f; vz *= 0.04f;                     // 0x6bd12c
    } else if (q > 144.0f) {                                       // farther than 12: approach
        const float l = std::sqrt(q);
        if (l != 0.0f) { const float inv = 1.0f / l; vx = dx * inv; vy = inv * 0.0f; vz = dz * inv; }
        vx *= 0.06f; vy *= 0.06f; vz *= 0.06f;                     // 0x6bd150
    }
    float s;
    if (b.drift_timer != 0) { s = b.drift_scale; --b.drift_timer; }
    else { s = 1.0f; b.drift_scale = 1.0f; }
    body_.pos.x += s * vx; body_.pos.y += s * vy; body_.pos.z += s * vz;   // raw: no collision, no clamp
}

// FUN_00549020 [CD1 §2; D23 §6.4].
void Boss07::children() {
    Boss07Block& b = blk_;
    // 2.1 bearings and blk+0x15c
    const Vec3 c = node(kCore), h = node(kHontai);
    Vec3 F{c.x - h.x, 0.0f, c.z - h.z};
    {
        const float L = std::sqrt((F.x * F.x + 0.0f) + F.z * F.z);
        F = L == 0.0f ? Vec3{} : mul(F, 1.0f / L);
    }
    const Vec3 n = nrm({ppos_.x - body_.pos.x, 0.0f, ppos_.z - body_.pos.z});
    b.behind = false;
    {
        const float yaw_p = ang_norm(at2(n.z, n.x)), yaw_f = ang_norm(at2(F.z, F.x));
        float d = yaw_p - yaw_f;
        if (d < 0.0f) { if (d < -kPi) d += k2Pi; }
        else if (d > kPi) d -= k2Pi;
        if (std::fabs(double(d)) > double(kHalfPi)) b.behind = true;
    }
    // 2.2 aim vectors and their smoothing
    bool per_option = b.behind;
    if (!b.behind) { b.aim[0] = b.aim[1] = F; if (b.phase == 2) per_option = true; }
    if (per_option) {
        b.aim[0] = ppos_ - child_[kOptR].pos;                      // 3-D, not normalised
        b.aim[1] = ppos_ - child_[kOptL].pos;
    }
    const float k = 0.9f;                                          // 0x6bd3e0
    for (int i = 0; i < 2; ++i) {
        const Vec3& a = b.aim[i]; Vec3& s = b.aim_s[i]; Vec3& fs = b.fwd_s[i];
        if (!(a.x == s.x) || !(a.y == s.y) || !(a.z == s.z)) {     // any component differs (NaN = differs)
            s.x = (s.x - a.x) * k + a.x; s.y = (s.y - a.y) * k + a.y; s.z = (s.z - a.z) * k + a.z;
            fs.x = (fs.x - F.x) * k + F.x; fs.y = (fs.y - 0.0f) * k + 0.0f; fs.z = (fs.z - F.z) * k + F.z;
        }
    }
    // 2.3 the super's fade [D23 §6.4]
    if (b.mode == kSuper) {
        const int sn = ++st_.super_n;
        if (sn == 240) se2(531);
        if (sn > 240) {
            float v = st_.fade - 0.01f;                            // 0x6bd084
            if (0.0f > v) v = 0.0f;
            st_.fade = v;
            child_[kOptR].alpha = v; child_[kOptL].alpha = v;
        }
    } else {
        st_.fade = 1.0f; st_.super_n = 0;
        child_[kOptR].alpha = 1.0f; child_[kOptL].alpha = 1.0f;
    }
    // 2.4 the options
    for (int i = 0; i < 2; ++i) {
        Boss07ChildMesh& o = child_[std::size_t(i)];
        if (opt_cnt_[std::size_t(i)] == 1) queue_motion(i, "N_WAIT");
        if (b.phase < 1) { o.alpha = 0.0f; continue; }             // phase 0: invisible, not moved
        if (b.mode == kSuper) {                                    // parked beside the player
            o.rot = {0.0f, i == 0 ? fb(0xc096cbe4u) : fb(0xbfc90fdbu), 0.0f};   // -4.712389 / -1.5707964
            o.pos = ppos_;
            if (i == 0) o.pos.x += 10.0f; else o.pos.x -= 10.0f;
            continue;
        }
        float a = st_.opt_alpha[i];
        if (b.opt_hp[i] > 0.0f) { a += 0.01f; if (a > 1.0f) a = 1.0f; }
        else { a -= 0.05f; if (a < 0.0f) a = 0.0f; }
        st_.opt_alpha[i] = a; o.alpha = a;
        o.rot = {0.0f, -kHalfPi - at2(b.aim_s[i].z, b.aim_s[i].x), 0.0f};
        const float bb = at2(b.fwd_s[i].z, b.fwd_s[i].x);
        float ang, r, q;
        if (i == 0) {
            const float t = (-kHalfPi - bb) + kHalfPi;
            ang = b.phase == 1 ? 0.8975979f - t : 2.4639943f - t;
        } else {
            const float t = -kHalfPi - (-kHalfPi - bb);
            ang = b.phase == 1 ? t - 0.8975979f : t - 2.4639943f;
        }
        if (b.phase == 1) { r = 11.0f; q = 0.1f; }
        else { r = 16.5f; q = fb(0x3c22339cu); }                   // 0.0099
        o.pos.x += ((body_.pos.x - float(std::cos(double(ang))) * r) - o.pos.x) * q;
        o.pos.y += ((body_.pos.y - 1.5f) - o.pos.y) * q;
        o.pos.z += ((body_.pos.z - float(std::sin(double(ang))) * r) - o.pos.z) * q;
    }
    // 2.5 the wing
    Boss07ChildMesh& w = child_[kWing];
    if (body_.hp >= 1.0f) {
        if (b.phase < 2) w.alpha = 0.0f;
        else {
            st_.wing_alpha += 0.01f;
            if (st_.wing_alpha > 1.0f) st_.wing_alpha = 1.0f;
            w.alpha = st_.wing_alpha;
            const int nn = st_.wing_n;
            const std::uint8_t idx = std::uint8_t(0xc0 - int(float(nn) * fb(0xbf360b61u)));   // * -0.71111113
            // [H] f32[0x68f350 + 4 * idx]: the engine's 256-entry cosine table (1.0, 0.9997, ..), computed here.
            const float v = float(std::cos(double(idx) * 6.283185307179586 / 256.0));
            out_->glare = std::uint8_t(0x2a - std::uint8_t(int(v * -42.0f)));   // 42 + 42 * sin(n deg)
            st_.wing_n = nn + 6;
        }
    }
    w.rot = b.rot;                                                 // SetRot(wing, &blk+0x1a0): dead or alive
    w.pos = body_.pos;
}

// AB §B.4: the inline GoEffect 140 PC2_S_B_koware on the core node.
void Boss07::spark_fx() {
    const std::uint32_t w1 = W(), w2 = W(), w3 = W();              // drawn before the pool test
    auto ang = [](std::uint32_t w) {
        const float f = fb((w & 0x7fffffu) | 0x3f800000u);
        return ((f - 1.0f) * 360.0f + 0.0f) * kDeg;
    };
    Boss07Effect& e = fx(140, node(kCore), Boss07Call::kInline);
    e.rot = {ang(w3), ang(w2), ang(w1)};
    e.or_1dc = 0x100;
    e.parent = Boss07Parent::kNode; e.parent_index = kCore;
    e.at_scale = {2, 2, 2}; e.at_mode = 2;
}

// FUN_005472e0 [AB §B].
void Boss07::parts() {
    Boss07Block& b = blk_;
    if (1.0f > in_->player_hp_mirror) return;                      // nothing below runs while the player is dead
    {   // part 0: the body [§B.1, §B.3]
        Boss07Part& p = parts_[0];
        const Vec3 c = node(kCore);
        p.pos = c; p.radius = 1.5f;                                // +0xd8 = +0xfc = 0: the part never hits
        static const float kResist[5][4] = {
            {0.4f, 0.4f, 0.4f, 0.4f}, {0.99f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.99f, 1.0f},
            {1.0f, 0.99f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},  // bare, W, E, F, D (table 0x548ff8)
        };
        if (b.barrier >= 0 && b.barrier <= 4) for (int k = 0; k < 4; ++k) p.resist[k] = kResist[b.barrier][k];
        mini(0).anchor = {c.x, c.y - 5.25f, c.z};                 // 0x76c1d8.., every frame
        const float dmg = p.dmg;                                   // (+0x19c = +0x138 = 0)
        if (dmg != 0.0f) {
            body_.flash = 1.65f; p.dmg = 0.0f;
            if (dmg > 0.0f) {
                if (b.hit_pause == 0 && b.barrier >= 1 && b.barrier <= 4) ++b.hits;   // once per frame with damage
                if (b.barrier >= 1 && b.barrier <= 3) {            // gauge 0 = barrier strength
                    const std::uint32_t T = std::uint32_t(barrier_threshold(char_, b.barrier, diff_));
                    mini(0).timer = 255; mini(0).value = float(std::uint32_t(T - b.hits)); mini(0).max = float(T);
                    if (b.hits >= T) {                             // BARRIER BREAK 0x547697
                        se(320);
                        b.hits = 0; b.barrier = 0;
                        b.window = kWindow[diff_] + (easy_ ? 60u : 0u);
                        spark_fx();
                        set_clip(kNDamage);
                        b.mode = 0;                                // also cancels a raise in progress
                    }
                }
                if (b.phase == 0 || ((b.phase == 1 || b.phase == 2) && b.window != 0))
                    if (sec_ == 0) set_clip(kNDamage);             // flinch only when open (literal 0 = N_WAIT)
                b.meter += dmg;
                body_.hp -= dmg;
                if (1.0f > body_.hp) phase_end();
            }
        }
    }
    for (int i = 1; i <= 2; ++i) {                                 // parts 1 / 2: the options [§B.7]
        Boss07Part& p = parts_[std::size_t(i)];
        Boss07ChildMesh& o = child_[std::size_t(i - 1)];
        float& hp = b.opt_hp[i - 1];
        mini(i).anchor = i == 1 ? Vec3{o.pos.x, float(double(o.pos.y) - 0.5), o.pos.z}
                                              : Vec3{o.pos.x, o.pos.y - 0.5f, o.pos.z};
        Vec3 pos = o.pos;
        if (hp == 0.0f) pos.y -= 999.0f;                           // destroyed
        if (b.phase < 2) pos.y -= 999.0f;                          // not hittable before phase 2
        p.pos = pos; p.radius = 3.0f;
        const float dmg = p.dmg;
        if (dmg == 0.0f) continue;
        body_.flash = 1.65f; p.dmg = 0.0f;                         // the BODY flashes too
        if (dmg <= 0.0f) continue;
        hp -= dmg; o.flash_2a0 = 1.65f;
        if (1.0f > hp) {                                           // OPTION DESTROYED
            hp = 0.0f; o.hits_off = true;
            fx(320, o.pos, Boss07Call::kFx2);                      // bos\kari_sini @0x5489b2 / 0x548ad0
            se(369);
        }
        if (diff_ == 0) {                                          // the Normal row only
            Boss07MiniGauge& g = mini(i);
            g.timer = 255; g.value = hp; g.max = 1000.0f;
        }
        if (b.barrier == kBarD) {                                  // gauge 0 = both options' HP
            mini(0).timer = 255; mini(0).value = b.opt_hp[1] + b.opt_hp[0];
            mini(0).max = float(2 * kOptionHp[diff_]);
        }
        if (!(b.opt_hp[0] == 0.0f && b.opt_hp[1] == 0.0f)) continue;
        dark_break();
    }
}

// AB §B.8: both options destroyed — the dark barrier falls.
void Boss07::dark_break() {
    Boss07Block& b = blk_;
    const std::uint32_t T = std::uint32_t(barrier_threshold(char_, b.barrier, diff_));   // b == 4 -> 0
    b.hits = T;
    b.window = kDarkWindow[diff_] + (easy_ ? 60u : 0u);
    if (b.hits >= T) {                                             // unsigned jb never taken
        se(320);
        b.hits = 0; b.barrier = 0;
        b.cooldown = kCooldown[diff_];
        spark_fx();
    }
    set_clip(kNDamage);                                            // blk+0xa4 is left alone
}

// AB §B.5 / §B.6: HP below 1 — the death or a phase end.
void Boss07::phase_end() {
    Boss07Block& b = blk_;
    if (b.phase != 0 && b.phase != 1) { body_.hp = 0.0f; return; } // phase 2: the shell runs the death this frame
    body_.hp = 1.0f;
    if (!b.refill) body_.max_hp += b.phase == 0 ? 1250.0f : 500.0f;   // raw, not difficulty-scaled
    b.refill = true; b.meter = 0.0f;
    if (b.phase == 0) {                                            // §B.6.1
        b.cam_slot = 2;
        const std::uint32_t w1 = W(), w2 = W();
        const float x = float(20 - int((w2 >> 17) % 40)), z = float(20 - int((w1 >> 17) % 40));
        body_.move_speed = std::fabs(0.275f); body_.move_radius = 1.0f;
        body_.move_target = {x, body_.pos.y, z};
        out_->move_to = Boss07MoveTo{body_.move_target, body_.move_radius, body_.move_speed};
        b.hit_pause = 60; b.drift_scale = 0.0f; b.drift_timer = 360; b.turn_scale = 1.2f; b.turn_timer = 360;
        b.mode = kRaiseW; b.next_barrier = 1;
        out_->fog = std::make_pair(1, 0.0025f);
    }
    if (b.phase == 1) {                                            // §B.6.2
        b.cam_slot = 4;
        W(); W();                                                  // two draws, results unused [AB C2]
        b.turn_scale = 1.5f; b.turn_timer = 60; b.drift_scale = 0.0f; b.drift_timer = 9999;
        b.mode = 0; b.next_barrier = 0;
        if (b.barrier >= 1 && b.barrier <= 3) { b.hits = 0; b.barrier = 0; spark_fx(); }
        b.barrier = kBarD; mini(0).timer = 0;
        se(319);
        out_->fog = std::make_pair(2, 0.0025f);
        out_->skin_options = true;
    }
    // §B.6.3 common
    out_->cam_slot = std::make_pair(b.cam_slot, 0.025f);
    b.cycle = 0; b.cooldown = kCooldown[diff_];
    for (int i = 0; i < 3; ++i) {
        Boss07Effect& e = fx(440, body_.pos, Boss07Call::kFx2);    // bs_06_fusion_brek @0x5482be
        e.local_scale = float(double(0.5f * float(i)) + 1.0);      // 1.0, 1.5, 2.0
        e.parent = Boss07Parent::kBody; e.at_mode = 2;
    }
    {
        Boss07Effect& e = fx(172, body_.pos, Boss07Call::kFx2);    // PC3_mazin_hatudou @0x548457 (hit emitters)
        e.local_scale = 5.0f;                                      // [UNKNOWN G3] whether it scales the hit radius
        e.parent = Boss07Parent::kBody; e.at_mode = 2;
    }
    se2(186);
    out_->shake = std::make_pair(120, Vec3{0.3f, 0.3f, 0.3f});
    {
        Boss07Fader fd; fd.zero_30 = true; fd.colour = 0x00aa0000u; fd.step = fb(0x3c360b61u);   // 1/90
        fd.current = 1.0f; fd.target = 0.0f;
        out_->fader.push_back(fd);
    }
    out_->rumble.push_back(5);
    out_->skin_body = b.phase + 2;                                 // §B.6.4: blk+0xa0 is still the old phase
}

// FUN_00549eb0 [CD1 §3.1].
void Boss07::camera() {
    Boss07Block& b = blk_;
    const Vec3& D = body_.pos; const Vec3& P = ppos_;
    const Vec3 a = nrm({D.x - P.x, 0.0f, D.z - P.z});              // unit, player -> boss
    const float ex = P.x - D.x, ez = P.z - D.z;
    const float d = std::sqrt((ex * ex + 0.0f) + ez * ez);
    Vec3 bv{};                                                     // unit, boss -> player (0 when d == 0)
    if (d != 0.0f) { const float inv = 1.0f / d; bv = {ex * inv, 0.0f, ez * inv}; }
    if (2.95f > d) {                                               // 1. push-out
        const float k = std::max(0.0f, (2.95f - d) * 0.2f);
        out_->player.push.x += bv.x * k; out_->player.push.z += bv.z * k;
    }
    float s4 = st_.cam_d, s0 = st_.cam_h;                          // 2. phase 2: the statics and the slot
    if (b.phase == 2) {
        if (b.opt_hp[0] > 0.0f || b.opt_hp[1] > 0.0f) {
            if (s4 > 10.0f) s4 -= 0.2f;
            if (10.0f > s4) s4 += 0.1f;
            if (2.0f > s0) s0 += 0.005f;
            if (s0 > 2.0f) s0 -= 0.005f;
            if (b.cam_slot != 4 && b.cam_slot != 5) cam_slot(4, 0.025f);
        } else {
            if (s4 > 0.0f) s4 -= 0.1f;
            if (0.0f > s4) s4 = 0.0f;
            if (1.0f > s0) s0 += 0.005f;
            if (s0 > 1.0f) s0 -= 0.005f;
            if (b.cam_slot != 3 && b.cam_slot != 5) cam_slot(3, 0.025f);
        }
        if (b.mode == kSuper) s0 = s4 = 0.0f;
        st_.cam_d = s4; st_.cam_h = s0;
    }
    const float K = s4 + 35.0f;                                    // 3. targets
    b.cam_fov = fb(0x3edf66f3u);                                   // 0.43633232 = 25 deg
    b.cam_eye = {P.x + bv.x - a.x * K, P.y + 14.0f, P.z + bv.z - a.z * K};
    b.cam_look = {D.x - bv.x + a.x, D.y + (d * 0.1f - 6.0f), D.z - bv.z + a.z};
    if (b.phase == 2) { b.cam_look.y -= s0; b.cam_eye.y += s0; }
}

// FUN_0042c4f0 [CD1 §3.2].
bool Boss07::camera_hook(int k, Boss07CamRecord& rec) {
    Boss07Block& b = blk_;
    if (k == 0) return true;                                       // claimed, nothing written
    if (k == 1) {
        rec.target_fixed = {0, 0, 0}; rec.eye_mode = 3; rec.target_mode = 0; rec.eye_flags = 3;
        return true;
    }
    if (k >= 2 && k <= 4) {
        rec.eye_fixed = b.cam_eye; rec.target_fixed = b.cam_look;
        rec.eye_dist = b.cam_dist; rec.fov = b.cam_fov;
        rec.eye_mode = 0; rec.target_mode = 0;
        b.cam_n = 0; b.cam_t = 0.0f;
        return true;
    }
    if (k == 5) {                                                  // the super
        const Vec3 L{b.cam_look.x, 495.0f, b.cam_look.z};
        float t;
        if (b.cam_n == 0) { rec.eye_fixed = L; t = 0.1f; b.cam_t = 0.1f; }
        else t = b.cam_t;
        if (t >= 1.0f) t = 1.0f;
        t += 0.004f;
        ++b.cam_n; b.cam_t = t;
        rec.eye_fixed = {b.cam_look.x + (b.cam_eye.x - b.cam_look.x) * t, 495.0f + 15.0f * t,
                         b.cam_look.z + (b.cam_eye.z - 20.0f - b.cam_look.z) * t};
        rec.target_fixed = L;
        rec.eye_dist = b.cam_dist; rec.fov = b.cam_fov; rec.eye_mode = 0; rec.target_mode = 0;
        return false;                                              // falls into the common "return 0" tail [CD1 C14]
    }
    return false;
}

// FUN_00546900 [CD1 §4.1].
void Boss07::option_shot() {
    Boss07Block& b = blk_;
    const int c = b.shot;
    if (c == 0) return;
    if (c == 1) {                                                  // pick the firing option
        int i;
        if (b.opt_hp[0] > 0.0f && b.opt_hp[1] > 0.0f) i = int((W() >> 17) & 1u);   // one draw @0x54697d
        else if (b.opt_hp[0] != 0.0f) i = 0;
        else if (b.opt_hp[1] != 0.0f) i = 1;
        else { b.shot = 0; return; }                               // both dead: disarm
        b.shot_opt = i;
        se2(388);
    }
    Boss07ChildMesh& o = child_[std::size_t(b.shot_opt)];
    if (c == 1 || c == 31 || c == 61) {                            // three charge rings
        Boss07Effect& e = fx(440, o.pos, Boss07Call::kFx2);        // @0x546a8e
        e.parent = Boss07Parent::kOption; e.parent_index = b.shot_opt;
        e.at_scale = {0.65f, 0.65f, 0.65f}; e.at_mode = 2;
    }
    const int T = 90;                                              // s16[0x68bbd0 + 2 * flag144] = 90, 90, 90
    if (c == T - 25) se2(326);
    if (c != T) { b.shot = c + 1; return; }
    b.shot = 0;                                                    // FIRE
    queue_motion(b.shot_opt, "N_DAMAGE");
    o.flash_2a4 = 1.65f;
    const Vec3 pos{o.pos.x, o.pos.y + 2.25f, o.pos.z}, dir{0, 1, 0};
    const std::uint32_t r = R(5);                                  // one draw @0x546c93
    const std::uint32_t packed = (std::uint32_t(std::uint8_t(int(ppos_.x + 128.0f))) << 16) |
                                 (std::uint32_t(std::uint8_t(int(float(r) + ppos_.y + 128.0f))) << 8) |
                                 std::uint32_t(std::uint8_t(int(ppos_.z + 128.0f)));
    static const float kAccel[3] = {fb(0xb727c5acu), fb(0xb58637bdu), fb(0xb58637bdu)};   // -1e-5 / -1e-6 / -1e-6
    const float f[13] = {easy_ ? 0.6f : 0.8f, kAccel[diff_], 0.0f, -5.0f, 1.0f, ibits(53), ibits(2), ibits(2),
                         fb(0x3e97e9d8u), 1.0f, 2.0f, 60.0f, ibits(packed)};
    bullet(false, 0x25, 54, pos, dir, f);                          // @0x546d7c
}

void Boss07::dash(float lim, std::uint32_t mod, float speed, float radius) {
    const std::uint32_t w1 = W(), w2 = W();                        // z uses the first, x the second [AB C7]
    const float x = float(int(lim) - int((w2 >> 17) % mod)), z = float(int(lim) - int((w1 >> 17) % mod));
    body_.move_speed = std::fabs(speed); body_.move_radius = radius;
    body_.move_target = {x, body_.pos.y, z};
    out_->move_to = Boss07MoveTo{body_.move_target, body_.move_radius, body_.move_speed};
}

void Boss07::revive() {                                            // 0x54efda..0x54f17e
    Boss07Block& b = blk_;
    const float hp = float(kOptionHp[diff_]);
    b.opt_hp[1] = b.opt_hp[0] = hp;
    child_[kOptR].hits_off = false; child_[kOptL].hits_off = false;
    const float a = fb(0xbfc90fdbu) - at2(ppos_.z - body_.pos.z, ppos_.x - body_.pos.x);
    Boss07ChildMesh& r = child_[kOptR];
    r.pos = body_.pos;
    const float ang_r = 0.8975979f - (a + kHalfPi);
    r.pos.x -= cosf_(ang_r) * 11.0f; r.pos.z -= sinf_(ang_r) * 11.0f; r.pos.y -= 2.5f;
    b.rev[0] = 180;
    Boss07ChildMesh& l = child_[kOptL];
    l.pos = body_.pos;
    const float ang_l = (fb(0xbfc90fdbu) - a) - 0.8975979f;
    l.pos.x -= cosf_(ang_l) * 11.0f; l.pos.z -= sinf_(ang_l) * 11.0f; l.pos.y -= 2.5f;
    b.rev[1] = 180;
}

// FUN_0054ebb0 [AB §A.5].
void Boss07::start_attack_(int variant, std::uint32_t wrap) {
    Boss07Block& b = blk_;
    auto set = [&](int dt, float ts, int tt) { b.drift_scale = 0.0f; b.drift_timer = dt; b.turn_scale = ts; b.turn_timer = tt; };
    switch (variant) {
        case 0: set(60, 2.0f, 60); set_clip(kNShootR); b.variant = 0; break;
        case 1: set(60, 2.0f, 60); set_clip(kNShootR); b.variant = 1; break;
        case 2: set(60, 1.75f, 60); set_clip(kNShootL); b.variant = 0; break;
        case 3: set(60, 1.75f, 60); set_clip(kNShootL); b.variant = 1; break;
        case 4: set(90, 0.75f, 90); set_clip(kNImpactR); b.variant = 1; break;
        case 5: set(90, 0.75f, 90); set_clip(kNImpactR); b.variant = 0; break;
        case 6: set(60, 1.2f, 360); b.mode = b.next_barrier; break;
        case 7: set(240, 2.0f, 240); set_clip(kSisLazerStart); b.variant = 0; break;
        case 8: set(500, float((easy_ ? -0.25 : 0.0) + 1.25), 500); set_clip(kKiaiStart); b.variant = 0; break;
        default: break;
    }
    if (variant >= 0 && variant <= 8) b.cooldown = kCooldown[diff_];
    ++b.cycle;
    if (b.cycle >= wrap) b.cycle = 0;
}
void Boss07::start_attack(int variant, std::uint32_t wrap, Boss07Output& out) {
    Scope sc(*this, nullptr, out);
    start_attack_(variant, wrap);
}

// FUN_0054ee30 [AB §A.6; D23 §6.1, §6.2].
void Boss07::scheduler() {
    Boss07Block& b = blk_;
    auto window = [&]() { --b.window; b.drift_scale = 0.0f; b.drift_timer = 1; };   // shared code 0x54eed7
    auto dash_ok = [&](std::uint32_t r) { return r >= 10000u && r < 11500u - b.dash_bias; };   // unsigned
    auto bias = [&]() { if (b.dash_bias < 1600u) b.dash_bias += 250u; };
    if (b.mode != 0) {
        if (b.mode == kSuper) {                                    // the super pins both, every frame
            body_.pos = {0.0f, 497.0f, 20.0f};
            ppos_ = {0.0f, 495.0f, -5.0f};
            out_->player.pos = ppos_; out_->player.flyer = true;
        }
    } else if (b.phase == 0) {                                     // 0x54f762
        // (the table index is blk+0xac raw in the engine; it is always < wrap — the % only bounds a poked block)
        b.drift_scale = 0.0f; b.drift_timer = 1;                   // no drift this phase
        if (eligible()) {
            if (b.cooldown != 0) --b.cooldown;
            else if (R(5000) < 500) start_attack_(int(kAttacks0[b.cycle % 6]), 6);
        }
    } else if (b.phase == 1) {                                     // 0x54f50d
        if (b.window != 0) window();
        else if (eligible() && body_.move_speed == 0.0f) {
            if (b.cooldown != 0) --b.cooldown;
            else {
                const std::uint32_t r = R(50000);                  // draw 1, BEFORE the barrier test
                if (b.barrier == 0) {                              // raise the next barrier
                    b.meter = 0.0f; b.hit_pause = 60;
                    b.drift_scale = 0.0f; b.drift_timer = 360; b.turn_scale = 1.2f; b.turn_timer = 360;
                    int n = b.next_barrier + 1;
                    b.next_barrier = n;
                    if (n >= 4) b.next_barrier = n = 1;
                    b.mode = n;                                    // 1 W -> 2 F -> 3 E -> 1 ..
                } else if (dash_ok(r)) {
                    b.turn_scale = 1.8f; b.turn_timer = 60;
                    dash(18, 36, 0.25f, 1.0f);
                    bias();
                } else if (R(5000) < 500) start_attack_(int(kAttacks1[b.cycle % 7]), 7);
            }
        }
    } else if (b.phase == 2) {                                     // 0x54eecd
        if (b.window != 0) window();                               // the option-shot timer does not run in the window
        else {
            if (b.shot_timer != 0) --b.shot_timer;
            else {
                b.shot = 1;
                b.shot_timer = kShotBase[diff_] + int(R(360)) + (easy_ ? 60 : 0);
            }
            if (b.barrier == 0) {                                  // the window just ended: re-raise the dark barrier
                b.meter = 0.0f; b.barrier = kBarD; b.hits = 0;
                if (b.opt_hp[0] == 0.0f && b.opt_hp[1] == 0.0f) revive();
            } else if (eligible() && body_.move_speed == 0.0f) {
                if (b.cooldown != 0) --b.cooldown;
                else {
                    const std::uint32_t r = R(50000);
                    // (0x54f22f..0x54f290: a blk+0xd0 == 0 branch, unreachable here)
                    if (dash_ok(r)) {
                        b.turn_scale = 1.8f; b.turn_timer = 60;
                        dash(18, 36, 0.1875f, 0.75f);
                        bias();
                    } else if (R(5000) < 500) {
                        const std::uint32_t t = R(3);
                        start_attack_(int(kAttacks2[t][b.cycle % 8]), 8);
                        float thr = (diff_ == 1 || diff_ == 2) ? 0.35f : 0.0f;
                        if (easy_) thr = 0.0f;
                        if (thr > body_.hp / body_.max_hp) {       // Hard / Nightmare below 35 %: queue one
                            const int c = b.rr;
                            if (c == 0) b.q_w = true; else if (c == 1) b.q_f = true; else if (c == 2) b.q_e = true;
                            b.rr = c + 1 >= 3 ? 0 : c + 1;
                        }
                    }
                }
            }
        }
    }
    // TAIL 0x54f821: every frame, any mode
    if (b.hit_pause != 0) --b.hit_pause;
    if (b.dash_bias != 0) --b.dash_bias;
    if (b.phase != 0 && b.meter / body_.max_hp > 0.34f) {          // the damage cap [AB C1]
        b.window = 0; b.meter = 0.0f; b.cooldown = b.phase == 2 ? 60 : 0;
    }
    if (b.refill) {                                                // REFILL
        body_.hp += 25.0f;
        if (body_.hp >= body_.max_hp) { ++b.phase; b.refill = false; }
    }
    if (b.mode == kSuper && b.fc % 3 == 0) {                       // the super's ambience [D23 §6.2]: inline FX 33
        // [not modelled] the "particle pool full / GoEffect alloc failed" early return is the host's.
        Boss07Effect& e = fx(33, Vec3{}, Boss07Call::kInline);     // pc\adol\adol_dash_max
        e.side = 0; e.stat = false; e.or_1dc = 0x100;
        e.parent = Boss07Parent::kPlayer; e.at_rot = {0.0f, in_->cam_yaw, 0.0f}; e.at_mode = 2;
    }
}

// FUN_0054a340 [AB §A.4; D23 §6.3]. One SetClip at most per call.
void Boss07::sequencer() {
    Boss07Block& b = blk_;
    const bool fin = cnt_ == 1;
    auto fade = [&](float step, float target) { Boss07Fader f; f.step = step; f.target = target; out_->fader.push_back(f); };
    switch (b.mode) {
        case 0:
            if (sec_ == kKiaiStart) { if (fin) set_clip(kKiaiLoop); }
            else if (sec_ == kKiaiLoop) { if (fin) set_clip(kTunagi); }
            else if (sec_ == kTunagi) { if (fin) set_clip(kMeteor); }
            else if (sec_ == kSisLazerStart) { if (fin) set_clip(kSisLazer); }
            else if (sec_ == kSisLazer) { if (fin) set_clip(kNWait); }
            else if (fin) set_clip(sec_ == kMeteor ? kMeteorFromWait : kNWait);
            break;
        case 1: case 2: case 3: {
            static const int kChg[4] = {0, kN2W, kN2F, kN2G}, kWaitC[4] = {0, kWWait, kFWait, kGWait},
                             kDmg[4] = {0, kWDamage, kFDamage, kGDamage};
            const int chg = kChg[b.mode], wait = kWaitC[b.mode], dmg = kDmg[b.mode];
            if (sec_ == kNWait || sec_ == kNDamage) set_clip(chg); // at once
            else if (sec_ == wait) { if (fin) { set_clip(kNWait); b.mode = 0; } }
            else if (sec_ == dmg || sec_ == chg) { if (fin) set_clip(wait); }
            else if (fin) set_clip(sec_ == 1 ? kNWait : chg);
            break;
        }
        case 4:                                                    // the super
            if (sec_ == kKiaiStart) { if (fin) { set_clip(kKiaiLoop); fade(fb(0x3c23d70au), 0.0f); } }      // 0.01
            else if (sec_ == kKiaiLoop) { if (cnt_ == 2) { set_clip(kTunagi); queue_motion(kWing, "METEOR_TO"); } }
            else if (sec_ == kTunagi) { if (fin) { set_clip(kMeteor); queue_motion(kWing, "METEOR_LOOP"); } }
            else if (sec_ == kMeteor) { if (fin) { set_clip(kMeteorFromWait); fade(fb(0x3d4ccccdu), 1.0f); } }   // 0.05
            else if (sec_ == kMeteorFromWait) {
                if (fin) {                                         // THE RETURN 0x54a53d
                    fade(fb(0x3d4ccccdu), 0.0f);
                    body_.pos = {0.0f, 4.0f, 0.0f};
                    child_[kOptL].pos.y = 1.5f; child_[kOptR].pos.y = 1.5f;
                    ppos_ = {0.0f, 7.5f, -15.0f};
                    out_->player.pos = ppos_; out_->player.flyer = false;
                    cam_slot(4, 1.0f);
                    b.mode = 0;
                    out_->skybox_alpha = 1.0f;
                    kill(b.h_izigen);
                    out_->flags.push_back({211, 0});
                    if (in_->player_hp >= 1.0f) player_section(9);
                    out_->flags.push_back({134, 0});
                    queue_motion(kWing, "WAIT");
                }
            }
            break;
        default: break;
    }
}

// FUN_0054a890 [CD1 §5; D23 §2..§5, §9..§15].
void Boss07::executor() {
    exec_hands();
    exec_super();
    exec_barriers();
}

// CD1 §5.1: six words, then one effect.
void Boss07::spark(int id, int nd, float base) {
    const float u1 = U(), u2 = U(), u3 = U();
    const float u4 = U(), u5 = U(), u6 = U();
    Boss07Effect& e = fx(id, node(nd));
    e.scale = {u3 + base, u2 + base, u1 + base};
    e.rot = {(u6 * 360.0f + 0.0f) * kDeg, (u5 * 360.0f + 0.0f) * kDeg, (u4 * 360.0f + 0.0f) * kDeg};
}

void Boss07::ice_pair() {                                          // [0x54bb83..0x54bc62]
    Boss07Effect& a = fx(405, {ppos_.x, 0.0f, ppos_.z}); a.power = -4.0f;    // bs_07_hyoutyuu
    Boss07Effect& c = fx(406, {ppos_.x, -1.0f, ppos_.z}); c.power = -8.0f;   // bs_07_h_tyakudan
}

void Boss07::exec_hands() {
    Boss07Block& b = blk_;
    const float f = f_;
    const std::uint32_t dc = b.fc;
    if (sec_ == kNShootR) {                                        // CD1 §5.2
        if (diff_ >= 1 && b.phase > 0 && between(f, 566.0f, 580.0f)) b.arm = 1;
        if (f == 568.0f) se(312);
        if (between(f, 566.0f, 580.0f)) {                          // one shot per tick
            const Vec3 c = node(kCore), h = node(kHontai);
            const Vec3 aim = nrm({c.x - h.x, 0.0f, c.z - h.z});
            const Vec3 pos{c.x, c.y - 0.8f, c.z};
            int n = ++st_.shoot_n;
            if (n == 5) { st_.shoot_n = 0; n = 0; }
            int eff = 409;                                         // bs_07_baramaki_b
            if (diff_ == 2 && n == 4) eff = 398;                   // Nightmare: every 5th is a meteor
            const float base = at2(aim.z, aim.x);
            float fl[13] = {};
            if (b.variant == 1) {                                  // PATTERN B
                float ang = base + (U() * 20.0f - 60.0f) * kDeg;
                if (bit17()) ang = base + (60.0f - U() * 20.0f) * kDeg;
                const Vec3 dir = nrm({cosf_(ang), 0.0f, sinf_(ang)});
                float speed = 0.275f - U() * fb(0x3d4cccd0u);
                float accel = U() * fb(0x3a03126cu) - 0.00225f;
                if (diff_ == 2 && eff == 398) {                    // the Nightmare meteor overrides both
                    if (bit17()) { speed = 0.25f - U() * fb(0x3d4cccccu); accel = U() * 0.0005f - 0.00325f; }
                    else { speed = 0.4f - U() * fb(0x3d4cccd0u); accel = U() * 0.0005f - 0.00125f; }
                }
                fl[0] = speed; fl[1] = accel; fl[3] = -10.0f; fl[4] = 1.0f;
                bullet(true, 0x27, eff, pos, dir, fl);             // alloc @0x54acbe
            } else {                                               // PATTERN A
                const float ang = base + (U() * 160.0f - 80.0f) * kDeg;
                const Vec3 dir = nrm({cosf_(ang), 0.0f, sinf_(ang)});
                float accel = U() * fb(0x3a03126cu) - 0.00225f;
                if (diff_ == 2) accel = U() * 0.001f - 0.003f;
                const float speed = U() * fb(0x3d4cccccu) + 0.23f;
                fl[0] = speed; fl[1] = accel; fl[3] = -10.0f; fl[4] = 1.0f;
                bullet(true, 2, eff, pos, dir, fl);                // alloc @0x54af00
            }
        }
        if (between(f, 562.0f, 578.0f) && dc % 3 == 0) fx(168, node(kCore));   // @0x54b0b2
        if (f == 552.0f) se(311);
        if (f < 562.0f && (dc & 1u) == 0) spark(168, kHandR, 2.0f);            // the charge
        if (between(f, 562.0f, 578.0f) && (dc & 1u) == 0) spark(170, kHandR, 1.0f);   // the release
    }
    if (sec_ == kNShootL) {                                        // CD1 §5.3
        if (diff_ >= 1 && b.phase > 0 && between(f, 522.0f, 544.0f)) b.arm = 1;
        if (f == 531.0f) se(315);
        float acc_b = 0.004f, acc_a = fb(0x3b7f9724u);             // 0.0039
        int last = 531;
        float turn_a = (U() * 2.0f + 5.0f) * kDeg;                 // one word EVERY tick of the clip
        if (diff_ == 2) { last = 534; turn_a = (U() * 2.0f + 7.0f) * kDeg; acc_b = 0.006f; acc_a = 0.00625f; }
        if (525.0f <= f && f <= float(last) && (dc & 3u) == 0) {
            const Vec3 pos = node(kHandL);
            const Vec3 dir = nrm(node(kNailL1) - node(kHandL));
            const float u = U();
            float fl[13] = {};
            if (b.variant == 1) {                                  // PATTERN B: hover, then dive (kind 0x26)
                fl[0] = fb(0x3d3851ecu); fl[1] = acc_b; fl[3] = -2.0f; fl[4] = 1.0f;
                fl[5] = ibits(399); fl[6] = ibits(10); fl[7] = ibits(1);
                fl[8] = (u * 2.0f + 6.0f) * kDeg; fl[9] = 1.0f; fl[10] = 40.0f; fl[11] = 999.0f;
                bullet(true, 0x26, 398, pos, dir, fl);
            } else {                                               // PATTERN A: late homing (kind 2)
                fl[0] = fb(0x3d343958u); fl[1] = acc_a; fl[3] = -3.0f; fl[4] = 1.0f;
                fl[5] = ibits(399); fl[6] = ibits(10); fl[7] = ibits(1);
                fl[8] = turn_a; fl[9] = 1.0f; fl[10] = u * 6.0f + 10.0f; fl[11] = 60.0f;
                bullet(true, 2, 398, pos, dir, fl);
            }
        }
        if (f == 514.0f) se(311);
        if (f < 524.0f && (dc & 1u) == 0) spark(168, kHandL, 1.0f);
    }
    if (sec_ == kNImpactR) {                                       // CD1 §5.4
        if (b.variant == 0) {                                      // ICE PILLAR
            if (f == 596.0f) se(311);
            if (f == 620.0f) se(314);
            if (f == 620.0f) { ice_pair(); if (diff_ == 2) b.echo = 1; }
            if (f < 620.0f) fx(286, node(kHandR));                 // every tick
        } else {                                                   // FIRE WAVE
            if (f == 596.0f) se(317);
            if (f == 620.0f) se(318);
            if (f == 620.0f) {
                const Vec3 hr = node(kHandR);
                const float dx = ppos_.x - hr.x, dz = ppos_.z - hr.z;
                float step = 25.0f; int cnt = 7;
                if (diff_ == 2) { step = 16.0f; cnt = 11; }
                const float lo = step * -0.25f, hi = step * 0.25f;
                const float jit = ((hi - lo) * U() + lo) * kDeg;   // one word @0x54bdb8
                const Vec3 pos{hr.x, 0.75f, hr.z};
                const float half = float(cnt >> 1) * step * kDeg;
                for (int i = 0; i < cnt; ++i) {
                    const float a = at2(dz, dx) + float(i) * step * kDeg - half + jit;
                    Boss07Effect& e = fx(407, pos);                // bs_07_firewave @0x54bee4
                    e.rot = {0.0f, -kHalfPi - a, 0.0f};
                }
            }
            if (f < 620.0f) fx(133, node(kHandR));
        }
        if (f == 622.0f) { out_->shake = std::make_pair(45, Vec3{0.25f, 0.25f, 0.0f}); out_->rumble.push_back(3); }
    }
    if (diff_ == 2 && b.echo != 0) {                               // the Nightmare echo: outside the clip test
        const int e = ++b.echo;
        if (e == 20 || e == 40) ice_pair();
        if (e == 60) b.echo = 0;
    }
}

// D23 §2..§5.
void Boss07::exec_super() {
    Boss07Block& b = blk_;
    const float f = f_;
    if (sec_ == kSisLazer && f == 790.0f) {                        // §2
        se(330);
        for (int k : {int(kHandR), int(kHandL)}) {
            Boss07Effect& e = fx(459, Vec3{});                     // bs_08_VerticalThunder (hit emitters)
            e.parent = Boss07Parent::kNode; e.parent_index = k;
            e.at_off = {0.0f, -node(k).y - 3.25f, 0.0f}; e.at_scale = {1.0f, 0.675f, 1.0f}; e.at_mode = 2;
        }
        // (dead code 0x54c354..0x54c424: the 12-unit point and a distance, unused [D23 R1])
        const Vec3 C{ppos_.x, 0.75f, ppos_.z};
        fx(333, C);                                                // bs_07_mahouzin; handle not kept
        b.circle = C; b.circle_timer = 350;
    }
    // §3: the circle while alive
    if (b.circle_timer != 0 && psec_ != 5 && b.capture == 0) {
        const std::uint32_t t = --b.circle_timer;
        if (std::uint32_t(350 - kCircleDelay[diff_]) > t) {        // live
            const Vec3& C = b.circle;
            const float dx = ppos_.x - C.x, dy = ppos_.y - C.y, dz = ppos_.z - C.z;
            const float d = std::sqrt((dx * dx + dy * dy) + dz * dz);
            if (49.0f > d * d) {                                   // CAPTURE
                b.capture = 90;
                out_->player.iframes = 91u;
                out_->flags.push_back({134, 1});
                player_section(2);
                Boss07Effect& e = fx(522, Vec3{});                 // ivent\gaikotu: side 0, no stat [D23 R4]
                e.side = 0; e.stat = false;
                e.parent = Boss07Parent::kPlayer; e.at_rot = {0.0f, in_->cam_yaw, 0.0f}; e.at_mode = 2;
            }
            if (!(in_->player_hp < 1.0f)) {                        // SUCTION (cast 13 of the actor pool)
                const Vec3 n = mul(nrm({ppos_.x - C.x, 0.0f, ppos_.z - C.z}), 15.0f);
                const int c = char_;
                if (c >= 0 && c <= 3) {
                    float x = float(diff_) * kSuctionB[c]; x += (kSuctionA[c] + 0.00375f); x *= n.x;
                    float z = float(diff_) * kSuctionB[c]; z += (kSuctionA[c] + 0.00375f); z *= n.z;
                    if (easy_) { x *= 0.5f; z *= 0.5f; }
                    out_->player.suction.x -= x; out_->player.suction.z -= z;
                }
            }
        }
    }
    // §4: the capture countdown and the entry into the super
    if (b.capture != 0) {
        const std::uint32_t c = --b.capture;
        if (c == 30) {
            Boss07Fader fd; fd.zero_30 = true; fd.colour = 0xffffffu; fd.step = fb(0x3c888889u); fd.target = 1.0f;
            out_->fader.push_back(fd);
            se(351);
        }
        if (c == 0) {                                              // ENTER THE SUPER [0x54c917..0x54cb8e]
            out_->flags.push_back({550, 0});
            out_->player.kill_7e0 = true;
            out_->player.boost_tick = true;                        // FUN_00462c40(cl 1)
            Boss07Fader fd; fd.zero_30 = true; fd.colour = 0xffffffu; fd.step = 1.0f; fd.target = 1.0f;
            out_->fader.push_back(fd);
            b.drift_scale = 0.0f; b.drift_timer = 360;
            b.turn_scale = 30.0f; b.turn_timer = 10;
            set_clip(kKiaiStart);
            b.mode = kSuper;
            cam_slot(5, 1.0f);
            child_[kOptR].alpha = 1.0f; child_[kOptL].alpha = 1.0f;
            b.arm = 0; b.rock_timer = 0;
            out_->particles_wipe = true;
            {
                Boss07Effect& e = fx(337, {0.0f, 495.0f, -5.0f});  // bs_07_izigen
                e.handle = b.h_izigen = next_handle_++;
            }
            fx(11, {0.0f, 495.0f, -5.0f});                         // bs_07_hobaku; handle not kept
            out_->skybox_alpha = 0.0f;
            mini(0).timer = 0;
            out_->flags.push_back({211, 1});
            b.shot = 0;
            player_section(char_ == 0 ? 2 : 318);
        }
    }
    // §5: KIAI_START, METEOR_FROM_WAIT, METEOR
    const bool S = b.mode == kSuper;
    if (sec_ == kKiaiStart && f == 678.0f) {
        se(S ? 352 : 328);
        Boss07Effect& e = fx(338, Vec3{});                         // bs_07_tame
        e.handle = b.h_charge = next_handle_++;
        e.parent = Boss07Parent::kBody; e.at_mode = 2;
        out_->rumble.push_back(4);
    }
    if (sec_ == kMeteorFromWait && f == 778.0f) kill(b.h_saikyou);
    if (sec_ == kMeteor && f == 730.0f) {
        kill(b.h_charge);
        int id = 401;                                              // bs_07_LB
        if (S) { se(353); id = 339; se(575); }                     // bs_07_saikyou_L
        else se(329);
        out_->rumble.push_back(5);
        {
            Boss07Effect& e = fx(id, Vec3{});                      // the beam: the hit emitters are this effect's
            e.parent = Boss07Parent::kBody; e.at_mode = 1;         // full follow
        }
        if (S) {
            Boss07Effect& e = fx(334, Vec3{});                     // bs_07_saikyou, on the player
            e.handle = b.h_saikyou = next_handle_++;
            e.parent = Boss07Parent::kPlayer; e.at_mode = 2;
        }
        out_->zoom = std::make_pair(0.5f, fb(0xbb03126fu));        // ZoomBlur(0.5, -0.002): both cases [D23 R7]
    }
}

// D23 §9..§15.
void Boss07::exec_barriers() {
    Boss07Block& b = blk_;
    const float f = f_;
    auto flash_both = [&]() { child_[kOptR].flash_2a4 = 1.65f; child_[kOptL].flash_2a4 = 1.65f; };
    auto opt_fx = [&](int id, int i) {                             // attached 6 in front of the option, mode 1
        Boss07Effect& e = fx(id, Vec3{});
        e.parent = Boss07Parent::kOption; e.parent_index = i;
        e.at_off = {0.0f, 0.75f, -6.0f}; e.at_mode = 1; e.flag201 = true;
    };
    // §9: N_WAIT housekeeping and the W event
    if (sec_ == kNWait) kill(b.h_charge);
    if (sec_ == kN2W || b.q_w) {
        if (f == 361.0f) { if (b.barrier != kBarW) se(319); b.barrier = kBarW; }
        if (f == 378.0f) se(321);
        if (f == 398.0f || b.q_w) {                                // THE W ATTACK
            b.q_w = false;
            flash_both();
            for (int i = 0; i < 2; ++i) if (b.opt_hp[i] > 0.0f) opt_fx(402, i);   // bs_07_tama_b (impact 404)
            b.se322 = 5;
        }
    }
    // §10: the F event
    if (sec_ == kN2F || b.q_f) {
        if (f == 411.0f) { if (b.barrier != kBarF) se(319); b.barrier = kBarF; }
        if (f == 428.0f) se(324);
        if (f == 448.0f || f == 438.0f || f == 428.0f || b.q_f) {  // THE F ATTACK
            b.q_f = false;
            flash_both();
            for (int i = 0; i < 2; ++i) if (b.opt_hp[i] > 0.0f) {
                const Vec3& op = child_[std::size_t(i)].pos;
                const Vec3 pos{op.x, op.y + 1.5f, op.z};
                const Vec3 dir = nrm({0, 1, 0});
                const float u1 = U(), u2 = U(), u3 = U(), u4 = U(), u5 = U();
                float fl[13] = {};
                fl[0] = u5 * fb(0x3d4cccceu) + 0.1f;              // speed
                fl[1] = u4 * 1e-4f + 1e-4f;                        // accel
                fl[4] = 1.0f;
                fl[5] = ibits(399); fl[6] = ibits(8); fl[7] = ibits(1);
                fl[8] = (u3 * 2.0f + 1.0f) * kDeg;                 // max turn 1..3 deg
                fl[10] = u2 * 40.0f + 30.0f;                       // homing start age
                fl[11] = u1 * 70.0f + 250.0f;                      // homing end age
                bullet(true, 2, 403, pos, dir, fl);                // bs_07_tama_c
            }
        }
    }
    // §11.1: the E event
    if (sec_ == kN2G || b.q_e) {
        if (f == 461.0f) { if (b.barrier != kBarE) se(319); b.barrier = kBarE; }
        if (f == 478.0f) se(321);
        if (f == 498.0f || b.q_e) {                                // THE E ATTACK
            flash_both();
            b.q_e = false; b.rock_timer = 80;
            for (int i = 0; i < 2; ++i) if (b.opt_hp[i] > 0.0f) opt_fx(335, i);   // bs_07_tama_d
            b.se322 = 5;
        }
    }
    // §11.2: the rock bullets, 80 frames later
    if (b.rock_timer != 0 && --b.rock_timer == 0) {
        for (int i = 0; i < 2; ++i) {
            if (b.opt_hp[i] == 0.0f) continue;
            const Vec3 c = in_->opt_core[std::size_t(i)], h = in_->opt_hontai[std::size_t(i)];
            const Vec3 v = nrm({c.x - h.x, 0.0f, c.z - h.z});
            const Vec3 pos{c.x + 4.5f * v.x, c.y + 4.5f * v.y + 2.5f, c.z + 4.5f * v.z};
            const Vec3& a = child_[std::size_t(i)].pos;
            const Vec3 dir = nrm({ppos_.x - a.x, 0.0f, ppos_.z - a.z});
            float fl[13] = {};
            fl[0] = fb(0x3e19999au); fl[4] = 1.0f;                 // speed 0.15, power 0, knock 1
            fl[5] = ibits(396); fl[6] = ibits(30); fl[7] = ibits(201);
            fl[8] = k2Pi; fl[11] = 600.0f;
            bullet(false, 4, 400, pos, dir, fl);                   // bs_07_tama_a, trail bs_07_iwa @0x54dd5a
        }
    }
    // §12
    if (b.se322 != 0 && --b.se322 == 0) se(322);
    // §13 / §14 need BOTH option alphas == 1.0 exactly
    if (child_[kOptR].alpha == 1.0f && child_[kOptL].alpha == 1.0f) {
        b.aimed = false;
        auto base_of = [&](int i, Vec3& n, Vec3& pos, float& a0) {
            const Vec3 c = in_->opt_core[std::size_t(i)], h = in_->opt_hontai[std::size_t(i)];
            n = nrm({c.x - h.x, 0.0f, c.z - h.z});
            pos = {c.x - 1.25f * n.x, c.y - 1.25f * n.y, c.z - 1.25f * n.z};
            a0 = at2(n.z, n.x);
        };
        auto fire3 = [&](const Vec3& pos, const Vec3 dirs[3], float speed) {
            for (int k = 0; k < 3; ++k) {
                float fl[13] = {};
                fl[0] = speed; fl[3] = -10.0f; fl[4] = 1.0f;
                bullet(false, 4, 408, pos, dirs[k], fl);           // bs_07_baramaki_a
            }
        };
        if (between(f, 568.0f, 580.0f)) {                          // §13: option volley
            if (f == 580.0f) b.volley_latch = 0;
            b.aimed = true;
            if (b.fc % 5 == 0) {
                for (int i = 0; i < 2; ++i) {
                    if (b.opt_hp[i] == 0.0f) continue;
                    if (b.volley_latch == 0) { b.volley_latch = 1; se(313); }
                    child_[std::size_t(i)].flash_2a4 = 1.65f;      // FUN_005458d0
                    Vec3 n, pos; float a0;
                    base_of(i, n, pos, a0);
                    const float s = i == 1 ? -1.0f : 1.0f;
                    const Vec3 d1 = nrm({cosf_(a0 + s * 0.3926991f), 0.0f, sinf_(a0 + s * 0.3926991f)});
                    const Vec3 d2 = nrm({cosf_(a0 + s * 0.7853982f), 0.0f, sinf_(a0 + s * 0.7853982f)});
                    pos.y -= 0.8f;
                    const Vec3 dirs[3] = {d1, n, d2};
                    fire3(pos, dirs, fb(0x3ecccccdu));             // 0.4
                }
            }
        }
        // §14: option barrage
        if (between(f, 591.0f, 629.0f) && b.variant != 0) b.aimed = true;   // the fire-wave window
        if (!(!b.behind && b.arm == 0)) {
            if (b.mode == kSuper) return;
            if (b.arm != 0) --b.arm;
            if (b.fc % 12 == 0) {
                for (int i = 0; i < 2; ++i) {
                    if (b.opt_hp[i] == 0.0f) continue;
                    se(355);
                    child_[std::size_t(i)].flash_2a4 = 1.65f;
                    Vec3 n, pos; float a0;
                    base_of(i, n, pos, a0);
                    const float s = i == 1 ? -1.0f : 1.0f;
                    Vec3 dA = nrm({cosf_(a0 - s * 0.3926991f), 0.0f, sinf_(a0 - s * 0.3926991f)});
                    Vec3 dC = nrm({cosf_(a0 + s * 0.3926991f), 0.0f, sinf_(a0 + s * 0.3926991f)});
                    float speed = 0.4f;
                    if (!b.aimed) {                                // scatter: six Rf, x and z drawn separately
                        const float base = at2(dA.z, dA.x);
                        dA.x = cosf_(base + Rf(-60.0f, 60.0f) * kDeg); dA.z = sinf_(base + Rf(-60.0f, 60.0f) * kDeg);
                        n.x = cosf_(base + Rf(-60.0f, 60.0f) * kDeg); n.z = sinf_(base + Rf(-60.0f, 60.0f) * kDeg);
                        dC.x = cosf_(base + Rf(-60.0f, 60.0f) * kDeg); dC.z = sinf_(base + Rf(-60.0f, 60.0f) * kDeg);
                        dA = nrm(dA); n = nrm(n); dC = nrm(dC);
                        speed = 0.2f;
                    }
                    pos.y -= 0.8f;
                    const Vec3 dirs[3] = {dA, n, dC};
                    fire3(pos, dirs, speed);
                }
            }
        }
    }
    // §15: the barrier aura
    if (b.mode == kSuper) return;
    if (b.barrier < 1 || b.barrier > 4) return;
    if ((b.fc & 0xfu) != 0) return;                                // every 16th frame
    static const int kAura[5] = {0, 341, 395, 340, 10};            // bs_07_baria_W / E / F / D
    Vec3 rot{};
    if (b.barrier != kBarW) {                                      // x, y, z in draw order; none for W
        rot.x = Rf(0.0f, 360.0f) * kDeg; rot.y = Rf(0.0f, 360.0f) * kDeg; rot.z = Rf(0.0f, 360.0f) * kDeg;
    }
    Boss07Effect& e = fx(kAura[b.barrier], node(kCore));           // @0x54ea66
    e.rot = rot;
    e.parent = Boss07Parent::kNode; e.parent_index = kCore; e.at_mode = 1;
}

// FUN_0054fb20 [D23 §8]. k = blk+0x2a0 as the function sees it (0 on the killing frame).
void Boss07::death() {
    Boss07Block& b = blk_;
    const float php = in_->player_hp_mirror;
    out_->flags.push_back({194, 0});                               // every death frame
    if (!b.death_started) {                                        // HEAD: until the latch
        if (rush_ && php >= 1.0f) out_->player.iframes = 9999u;    // boss rush only
        if (sec_ != kMeteor) set_clip(kMeteor);                    // the death pose
        // (part[0..2]+0x19c = 0)
        out_->exp = true;                                          // FUN_004fa4a0(&D+0x94) [D23 R14]
        body_.alpha_target = 0.0f; body_.alpha_step = fb(0x3ada740eu);          // 1/600
        for (Boss07ChildMesh& c : child_) { c.alpha_target = 0.0f; c.alpha_step = fb(0x3bb60b61u); }   // 1/180
        if (php >= 1.0f) {
            b.death_started = true;
            for (int n = 529; n <= 532; ++n) out_->flags.push_back({n, 0});
            if (!rush_) out_->flags.push_back({226, 1});           // THE BEATEN FLAG, not in boss rush
        }
    }
    if (!(1.0f > body_.hp) || !b.death_started) return;
    const std::uint32_t k = b.death_fc;
    if (k == 0) { se2(186); se2(575); se2(187); }
    if (k > 60) {
        if (0.2f >= body_.alpha) {
            if (rush_) out_->flags.push_back({156, 0});            // boss rush: the gate, every such frame
            else {
                if (php >= 1.0f && !b.buffs_cleared) {
                    b.buffs_cleared = true;
                    for (int n = 544; n <= 546; ++n) out_->flags.push_back({n, 0});
                }
                out_->kill_record = true;                          // FUN_00403cd0(), every such frame
            }
        }
        if (0.0f >= body_.alpha) {                                 // REMOVED
            body_.alpha = 0.0f;
            out_->gauge_hide = true; out_->destroy = true;
            body_.removed = true;
            for (Boss07ChildMesh& c : child_) c.removed = true;
            return;
        }
        if (0.1f >= child_[kWing].alpha) child_[kWing].translucent = true;
    }
    // DEBRIS [0x54fe91..0x550292]: 4 draws per node per frame, nodes 100.. can never fire.
    for (int n = 0; n < kBodyNodes; ++n) {
        const float u1 = U(), u2 = U(), u3 = U();
        const std::uint32_t d4 = W();
        Vec3 q;
        q.z = body_.pos.z + (u1 * 16.0f - 8.0f);
        q.y = body_.pos.y + (u2 * 10.0f - 5.0f);
        q.x = body_.pos.x + (u3 * 16.0f - 8.0f);
        if ((d4 >> 17) % 64 != 0) continue;
        if (n >= kDebrisLatches) continue;                         // (after the latch read; same outcome)
        if (b.debris[std::size_t(n)] != 0) continue;
        const std::uint32_t d5 = W(), d6 = W(), d7 = W(), d8 = W();
        const float sx = float((d7 >> 17) % 50) * 0.01f + 0.3f, sy = float((d6 >> 17) % 50) * 0.01f + 0.3f,
                    sz = float((d5 >> 17) % 50) * 0.01f + 0.3f;
        if ((d8 >> 17) % 8 == 0) {
            const std::uint32_t d9 = W();
            out_->sprites.push_back({q, float((d9 >> 17) % 100) * 0.005f + 0.55f});   // FUN_00584550 @0x5501bd
            if (in_->dust) in_->dust(q, out_->sprites.back().size);
        } else {
            Boss07Effect& e = fx(95, q, Boss07Call::kFx2);         // bs_07_sini_yotyou @0x550230
            e.scale = {1.1f * sx, 1.1f * sy, 1.1f * sz}; e.rtype = -1;
        }
        b.debris[std::size_t(n)] = 1;
    }
    // TIMED ONE-SHOTS
    if (k == 1) {
        se2(576);
        Boss07Effect& e = fx(97, body_.pos, Boss07Call::kFx2);     // bs_07_onryou
        e.scale = {1.25f, 1.25f, 1.25f}; e.rtype = -1;
    }
    if (k == 120) {
        se2(577); se2(586);
        Boss07Effect& e = fx(96, body_.pos, Boss07Call::kFx2);     // bs_07_bakuhatu
        e.rtype = -1;
    }
    if (k == 600) se2(188);                                        // [UNKNOWN V4] whether the fade lets it play
    if (k % 60 == 0) out_->rumble.push_back(5);
}

}  // namespace cleria
