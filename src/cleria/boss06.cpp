// Zava's two heads (boss06a / boss06b) — see boss06.hpp and build/boss_re/ZAVA*.md.
#include "cleria/boss06.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cleria/enemy_ai.hpp"   // rotate_toward (FUN_005eecc0), ai::angle_diff (FUN_005eec50)

namespace cleria {
namespace boss06 {

const int kClipSection[kClips] = {0, 1, 2, 3, 4, 12, 13, 14, 8, 9, 10, 11, 4, 7, 5, 6};

const SectionInfo kSectionTable[kSections] = {
    {"WAIT", 0, 28, 0, 28, true},          {"ATTACK1", 240, 263, 32, 55, false},  {"ATTACK0", 53, 90, 60, 70, false},
    {"DAMAGE", 93, 105, 73, 85, false},    {"SHAKE", 110, 125, 90, 105, true},    {"CHARGE_0", 130, 137, 110, 117, false},
    {"CHARGE_1", 140, 149, 120, 131, false}, {"BITE", 153, 170, 135, 152, false}, {"LASER_0", 32, 39, 32, 43, false},
    {"LASER_1", 39, 42, 43, 50, false},    {"LASER_2", 42, 44, 50, 52, false},    {"LASER_3", 42, 50, 50, 55, false},
    {"PIYORI_1", 170, 190, 155, 180, false}, {"PIYORI_2", 193, 210, 184, 200, true}, {"PIYORI_3", 213, 235, 203, 235, false},
};

int section_ticks(int head, int section) {
    const SectionInfo& s = kSectionTable[section];
    return (head == 0 ? s.a_end - s.a_start : s.b_end - s.b_start) * 4;
}

const WaypointTable kWaypoints[5] = {
    {15, {{0, 0}, {3.2f, -7}, {11, -10}, {17.8f, -7}, {20, 0}, {17.8f, 7}, {11, 10}, {3.2f, 7}, {-3.2f, -7}, {-11, -10},
          {-17.8f, -7}, {-20, 0}, {-17.8f, 7}, {-11, 10}, {-3.2f, 7}}},
    {15, {{0, 0}, {5.6f, -4}, {10, -5}, {14.4f, -4}, {16, 0}, {14.4f, 4}, {10, 5}, {5.6f, 4}, {-3.2f, -7}, {-11, -10},
          {-17.8f, -7}, {-20, 0}, {-17.8f, 7}, {-11, 10}, {-3.2f, 7}}},
    {15, {{0, 0}, {3.2f, -7}, {11, -10}, {17.8f, -7}, {20, 0}, {17.8f, 7}, {11, 10}, {3.2f, 7}, {-5.6f, -4}, {-10, -5},
          {-14.4f, -4}, {-16, 0}, {-14.4f, 4}, {-10, 5}, {-5.6f, 4}}},
    {12, {{0, -10}, {6.4f, -10}, {14, -7.4f}, {17.4f, 0}, {14.4f, 10}, {6.4f, 10}, {0, 10}, {-6.4f, 10}, {-14.4f, 7.4f},
          {-17.4f, 0}, {-14, -7.4f}, {6.4f, -10}, {0, 0}, {0, 0}, {0, 0}}},
    {12, {{0, 4}, {6.4f, 4}, {10, 3}, {12.4f, 2}, {10.4f, 10}, {6.4f, 10}, {0, 10}, {-6.4f, 10}, {-10.4f, 7.4f},
          {-12.4f, 2}, {-10, 3}, {6.4f, 4}, {0, 0}, {0, 0}, {0, 0}}},
};

Vec3 waypoint_world(int table, int index) {
    Vec3 w{};                                                      // FUN_00535050: out of range -> (0, 0, 0)
    if (table >= 0 && table < 5 && index >= 0 && index < kWaypoints[table].count)
        w = {kWaypoints[table].xz[index][0] * 0.8f, 0.0f * 0.8f, kWaypoints[table].xz[index][1] * 0.8f};
    return {w.x - 34.0f, w.y + 6.0f, w.z + 29.0f};                 // 0x6bda0c, 0x6bd880, 0x6bd9e0
}

}  // namespace boss06

using namespace boss06;
using ef2sim::M4;

namespace {
constexpr float kPi = 3.1415927f;        // 0x6bd6ac
constexpr float kHalfPi = 1.5707964f;    // 0x6bd4e4
constexpr float k2Pi = 6.2831855f;       // 0x6bd898
constexpr float kDeg = 0.017453292f;     // 0x6bd0dc "0.0174533" [H the exact bits: pi/180 as f32]

float fb(std::uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
float ibits(int i) { float f; std::memcpy(&f, &i, 4); return f; }
float at2(float y, float x) { return float(std::atan2(double(y), double(x))); }   // FUN_0066f80a
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 nrm(const Vec3& a) {                                          // FUN_0042cc50: zero length -> (0, 0, 0)
    const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return l == 0.0f ? Vec3{} : mul(a, 1.0f / l);
}
float w2pi(float x) {                                              // FUN_005ef010
    if (x > k2Pi) return float(std::fmod(double(x), double(k2Pi)));
    if (x < 0) return float(std::fmod(double(x), double(k2Pi))) + k2Pi;
    return x;
}
Vec3 row3(const M4& m) { return {m.m[3][0], m.m[3][1], m.m[3][2]}; }
bool in_arena(const Vec3& v) {                                     // FUN_005354f0
    return !(-50.0f > v.x) && !(v.x > -18.0f) && !(19.0f > v.z) && !(v.z > 39.0f);
}
const Vec3 kBack{0, 0, -1};
}  // namespace

struct Boss06::Scope {
    Boss06& b;
    const Boss06Input* in; Boss06Output* out; Boss06Zava* z;
    Scope(Boss06& bb, const Boss06Input& i, Boss06Zava* zz, Boss06Output& o) : b(bb), in(bb.in_), out(bb.out_), z(bb.z_) {
        b.in_ = &i; b.out_ = &o; b.z_ = zz;
        for (int k = 0; k < kHeads; ++k) b.count_[std::size_t(k)] = i.head[std::size_t(k)].count;
        b.ppos_ = i.player_pos;
    }
    ~Scope() { b.in_ = in; b.out_ = out; b.z_ = z; }
};

// FUN_0053bbb0, once per head [P1 §3.2].
Boss06::Boss06(const Boss06Setup& s) {
    bb5c_ = s.diff_index; easy_ = s.diff_easy; very_easy_ = s.diff_very_easy; char_ = s.character;
    lvl_ = very_easy_ ? 0 : easy_ ? 1 : bb5c_ == 1 ? 3 : bb5c_ == 2 ? 4 : 2;   // FUN_00535280
    for (int i = 0; i < kHeads; ++i) {
        Boss06Head& hb = h_[std::size_t(i)];
        hb.id = std::uint8_t(i);                                   // 0x70 by the mesh name
        hb.pos = i == 0 ? s.pos_a : s.pos_b;
        hb.pos.y = 6.1f;                                           // obj+0x54 = 0x40c33333 [0x53c09f]
        hb.move = 0; hb.step = 0; hb.timer = 0;                    // the inline "reset move"
        hb.speed_target = 0.08f; hb.accel = 0.001f;
        hb.clip = 0;                                               // SetClip(0) twice (the host starts in WAIT)
        hb.radius = 3.0f; hb.wp_stop = 0xff;
        hb.max_hp = hb.hp = 5000.0f; hb.str = 232.0f; hb.def = 320.0f;
        hb.alpha_target = 1.0f; hb.alpha_step = 1.0f;
        hb.turn_on = 1;
        // The difficulty table [0x53c0aa..0x53c14c]: flag 203, then 143, then 144.
        if (very_easy_) { hb.k_speed = 0.7f; hb.k_time = 1.3f; }
        else if (easy_) { hb.k_speed = 0.8f; hb.k_time = 1.3f; }
        else if (bb5c_ == 1) { hb.k_speed = 1.1f; hb.k_time = 1.0f; }
        else if (bb5c_ == 2) { hb.k_speed = 1.2f; hb.k_time = 0.8f; hb.def += 5.0f; }
        else { hb.k_speed = 1.0f; hb.k_time = 1.0f; }
    }
}

M4 Boss06::matrix(int i) const {
    const Boss06Head& hb = h_[std::size_t(i)];
    M4 m = ef2sim::rot_ypr(hb.rot.y, hb.rot.x, hb.rot.z);
    m.m[3][0] = hb.pos.x; m.m[3][1] = hb.pos.y; m.m[3][2] = hb.pos.z;
    return m;
}

std::uint32_t Boss06::draw() { return in_ && in_->rng ? in_->rng() : 0u; }

Vec3 Boss06::rand_between(const Vec3& a, const Vec3& b) {          // FUN_00535320: x, y, z in that order
    Vec3 o;
    o.x = float(R(100)) * (b.x - a.x) / 100.0f + a.x;
    o.y = float(R(100)) * (b.y - a.y) / 100.0f + a.y;
    o.z = float(R(100)) * (b.z - a.z) / 100.0f + a.z;
    return o;
}

Vec3 Boss06::p_pos() const { return in_->player ? ppos_ : Vec3{}; }
Vec3 Boss06::dir_p(int i) const { return nrm(p_pos() - h_[std::size_t(i)].pos); }
float Boss06::d_p2(int i) const {
    const Vec3 p = p_pos(); const Vec3& h = h_[std::size_t(i)].pos;
    return (p.x - h.x) * (p.x - h.x) + (p.z - h.z) * (p.z - h.z);
}
bool Boss06::burst() const {                                       // FUN_005352d0
    if (!in_->player) return false;
    const int s = in_->player_section;
    return (char_ == 1 && s == 0x139) || (char_ == 2 && s == 0x12c) || (char_ == 3 && s == 0x13d);
}
Vec3 Boss06::node(int i, int k) const { return row3(in_->head[std::size_t(i)].nodes[std::size_t(k)]); }

void Boss06::se(int id, const Vec3& p) { out_->sounds.push_back({id, p}); }
Boss06Effect& Boss06::fx2(int i, int id, const Vec3& p) {         // "plain": rot 0, scale 1, stat 0, power 0, knock 1
    Boss06Effect e; e.head = i; e.id = id; e.pos = p;
    out_->effects.push_back(e);
    return out_->effects.back();
}

void Boss06::set_clip(int i, int k, int blend) {                   // A+0x24 = 1.0; FUN_005f5c80; hb+0x77 = k
    out_->clips.push_back({i, k, kClipSection[k], blend});
    h_[std::size_t(i)].clip = std::uint8_t(k);
    count_[std::size_t(i)] = 0;                                    // FUN_005f5c80 zeroes A+0x1c
}

void Boss06::set_pattern(int i, int p) {
    Boss06Head& hb = h_[std::size_t(i)];
    hb.queued = hb.pattern = std::uint8_t(p); hb.pstep = 0;
}
void Boss06::start(int i, int m) {                                 // word hb+0x74 = m; hb+0x64 = 0
    Boss06Head& hb = h_[std::size_t(i)];
    hb.move = std::uint8_t(m); hb.step = 0; hb.timer = 0;
}
void Boss06::start_move_(int i, int m) {                           // FUN_0053c390
    Boss06Head& hb = h_[std::size_t(i)];
    hb.move = std::uint8_t(m); hb.step = 0; hb.timer = 0;
    if (m == 0) { hb.speed_target = 0.08f; hb.accel = 0.001f; set_clip(i, kWait); }
}
void Boss06::finish(int i) {                                       // FINISH 0x542e3e
    Boss06Head& hb = h_[std::size_t(i)];
    hb.move = 0; hb.step = 0; hb.timer = 0; hb.speed_target = 0.08f; hb.accel = 0.001f;
    set_clip(i, kWait);
}
void Boss06::abort_(int i) {                                       // FUN_0053c400
    Boss06Head& hb = h_[std::size_t(i)];
    if (hb.step < 0x80) hb.step = 0x80;
    hb.speed_target = 0.08f; hb.accel = 0.01f;
    move_tick(i);
}
void Boss06::start_move(int i, int m, Boss06Output& out) {
    Boss06Output* keep = out_;
    out_ = &out;
    start_move_(i, m);
    out_ = keep;
}
void Boss06::abort_move(int i, const Boss06Input& in, Boss06Zava& z, Boss06Output& out) {
    Scope sc(*this, in, &z, out);
    abort_(i);
}

// FUN_00535dc0 [D §6.1].
void Boss06::turn(int i, Vec3 d, float rate, bool pitch_on, bool clamp) {
    Boss06Head& hb = h_[std::size_t(i)];
    float tp = 0.0f;
    if (pitch_on) {
        const float h = std::sqrt(d.x * d.x + d.z * d.z);
        tp = at2(h, -d.y) - kHalfPi;
        if (clamp) tp = std::min(0.5235988f, tp);                  // 0x6bd314: at most +30 deg
    }
    hb.pitch = rotate_toward(hb.pitch, tp, rate);
    hb.yaw = rotate_toward(hb.yaw, at2(d.x, d.z) + kPi, rate);
    set_fwd(i, false);
}
// FUN_00535c20 [D §6.1b]: ready-made angles; zeroes the x of the flat forward (as the bytes have it, 0x535da3).
void Boss06::turn_ang(int i, const Vec3& ang, float rate) {
    Boss06Head& hb = h_[std::size_t(i)];
    hb.pitch = rotate_toward(hb.pitch, ang.x, rate);
    hb.yaw = rotate_toward(hb.yaw, ang.y, rate);
    set_fwd(i, true);
}
void Boss06::set_fwd(int i, bool zero_x) {
    Boss06Head& hb = h_[std::size_t(i)];
    const Vec3 v = nrm(ef2sim::transform_coord(kBack, ef2sim::rot_ypr(hb.yaw, hb.pitch, hb.roll)));
    hb.aim = v; hb.fwd = v;
    if (zero_x) hb.fwd.x = 0.0f; else hb.fwd.y = 0.0f;             // not re-normalised
    hb.rot = {hb.pitch, hb.yaw, hb.roll};                          // FUN_00503990 SetRot
}

bool Boss06::near_other(int i, const Vec3& p) const {              // FUN_0053c2f0
    const Boss06Head& hb = h_[std::size_t(i)]; const Boss06Head& ob = h_[std::size_t(1 - i)];
    const float r = hb.radius + ob.radius;
    return !(r * r < (ob.pos.x - p.x) * (ob.pos.x - p.x) + (ob.pos.z - p.z) * (ob.pos.z - p.z));
}
bool Boss06::probe(int i, const Vec3& from, const Vec3& delta) const {
    return in_->probe ? in_->probe(i, from, delta) : false;
}

void Boss06::slot_set(int i, int k, int id) {                      // the inline SetSlot / FUN_0053d6b0
    Boss06FxSlot& S = h_[std::size_t(i)].slots[std::size_t(k)];
    if (S.id != 0 && S.handle != 0) { out_->kill_ids.push_back(S.id); S.handle = 0; }
    S.id = std::uint16_t(id); S.count = 0; S.timer = 0;
}
void Boss06::slot_kill(int i, int k, bool soft) {                  // FUN_00534eb0
    Boss06FxSlot& S = h_[std::size_t(i)].slots[std::size_t(k)];
    if (S.handle != 0) {
        if (soft) out_->kill_handles.push_back(S.handle); else out_->kill_ids.push_back(S.id);
        S.handle = 0;
    }
    S.id = 0;
}
void Boss06::orb_kill(int i, int k) {                              // FUN_0053f1b0
    Boss06Head& hb = h_[std::size_t(i)];
    if (k >= kOrbiters) { for (int n = 0; n < kOrbiters; ++n) orb_kill(i, n); return; }
    int& h = hb.orb[std::size_t(k)];
    if (h != 0) { out_->kill_handles.push_back(h); h = 0; }
}

void Boss06::fog_fade(int i, const Vec3& target) {                 // FUN_00535170 [E2 §3.5]
    Boss06FogFade& f = h_[std::size_t(i)].fog;
    const Vec3 t = target;                                         // may alias f.saved (the restore call)
    if (in_->fog_colour) { f.to = t; f.saved = *in_->fog_colour; f.from = *in_->fog_colour; }
    f.dur = 60;
}
void Boss06::fog_tick(int i, Vec3* rec) {                          // FUN_005351d0
    Boss06FogFade& f = h_[std::size_t(i)].fog;
    if (f.dur == 0) return;
    float t = float(f.t) / float(f.dur);
    if (t > 1.0f) { t = 1.0f; f.dur = 0; }
    f.t = std::uint8_t(f.t + 1);                                   // never reset: only the first fade is gradual
    if (rec) {
        rec->x = (f.to.x - f.from.x) * t + f.from.x;
        rec->y = (f.to.y - f.from.y) * t + f.from.y;
        rec->z = (f.to.z - f.from.z) * t + f.from.z;
    }
}

// ---------------------------------------------------------------------------
Boss06Output Boss06::tick(const Boss06Input& in, Boss06Zava& z) {
    Boss06Output out;
    Scope sc(*this, in, &z, out);
    brain(0);
    brain(1);
    return out;
}

// FUN_00544980 [D §2].
void Boss06::brain(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    // [not modelled] the HP < 1.0 branch (dead: the HP is refilled every frame, the part list is empty) and the
    // brain modes other than 2 (hb+0x6c: only 2 is ever written).
    if (hb.move == 0 && hb.queued != hb.pattern) set_pattern(i, hb.queued);   // 0x5449bd
    const int p = hb.pattern;
    if (p == 0xff) {                                               // DEATH 0x544beb
        if (hb.pstep == 0) {
            hb.hittable = false;                                   // every collision entry +0x8b = +0x8a = 0
            if (hb.step < 0x80) hb.step = 0x80;
            hb.speed_target = 0.08f; hb.accel = 0.01f;
            move_tick(i);                                          // the old move's finishing step
            hb.move = 15; hb.step = 0; hb.timer = 0;
            ++hb.pstep;
        }
        move_tick(i);                                              // 0x544c75
    } else {
        switch (p) {
            case 0:                                                // 0x5449f4
                if (hb.pstep == 0) {
                    hb.move = 0; hb.step = 0; hb.timer = 0; hb.speed_target = 0.08f; hb.accel = 0.001f;
                    set_clip(i, kWait); set_clip(i, kWait);
                    ++hb.pstep;
                }
                move_tick(i);
                break;
            case 1: pattern1(i); break;
            case 2: pattern2(i); break;
            case 3: pattern3(i); break;
            default: break;   // 4: nothing; 5: [not modelled] never written (0x544ab1..0x544be6); > 5: nothing
        }
    }
    {   // SPEED 0x544c7c
        float v = hb.speed; const float t = hb.speed_target;
        if (v != t) {
            if (t > v) { v += hb.accel; hb.speed = v; if (!(v < t)) hb.speed = t; }
            else if (v > t) { v -= hb.accel; hb.speed = v; if (!(t < v)) hb.speed = t; }
        }
    }
    // PERFRAME 0x544d36
    orbiters(i);
    fx_slots(i);
    if (hb.clip == kDamage && done(i)) set_clip(i, kWait);         // the flinch returns to WAIT
    life_tick(i);
    // (the unused cast-0xad scan 0x544d93..0x544dc2)
    if (in_->player && in_->player_floor != kFloorNone && in_->player_grounded) {   // the ride counter 0x544dc4
        const bool mine = in_->player_floor == (hb.id == 0 ? kFloorA : kFloorB);
        if (mine) { if (hb.ride < 0xff) ++hb.ride; }
        else hb.ride = 0;
    }
    if (hb.flash != 0.0f) { hb.flash -= 0.075f; if (0.0f > hb.flash) hb.flash = 0.0f; }
    if (hb.timer != 0) --hb.timer;
    if (hb.hold != 0) --hb.hold;
    if (hb.laser_cd != 0) --hb.laser_cd;
    ++hb.fc;
}

// FUN_005434f0 [D §3.1]: roam, breathe, shake off.
void Boss06::pattern1(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const Boss06Head& ob = h_[std::size_t(1 - i)];
    const Vec3 dP = dir_p(i);
    const float dP2 = d_p2(i);
    if (hb.stun_req) { abort_(i); start(i, 5); hb.stun_req = 0; }  // 0x5435bd
    if (hb.move == 5) { move_tick(i); return; }
    const int T = hb.id == 0 ? 2 : 1;
    switch (hb.pstep) {
        case 0: {                                                  // 0x54364c
            hb.wp = 0; hb.speed = 0.0f; hb.dir = {0.0f, 0.0f, -0.0f};
            hb.free = 1; hb.hold = std::uint16_t(R(121) + 60);     // RNG 0x5436c2
            hb.wp = hb.id == 0 ? 6 : 13;
            hb.speed_target = 0.08f; hb.accel = hb.id == 0 ? 0.001f : 0.0001f;
            if (hb.wp_stop != hb.wp) { ++hb.wp; if (hb.wp >= kWaypoints[T].count) hb.wp = 0; }
            hb.target = waypoint_world(T, hb.wp);
            hb.dir = nrm(hb.target - hb.pos);
            ++hb.pstep;
            [[fallthrough]];
        }
        case 1: {                                                  // 0x5437ea
            if (h_[1].pattern == 2 && hb.id == 0 && hb.move == 0 && dP2 > 289.0f) start(i, 13);
            if (h_[0].pattern == 2 && hb.move == 0 && hb.hold == 0 && hb.id == 1 && dP2 > 100.0f) start(i, 14);
            int m = hb.move;
            if ((m == 0 || m == 3) && hb.ride > 180) { abort_(i); start(i, 6); m = 6; }
            if (m == 0 && hb.b76 == 3) start(i, 3);                // dead [D4]
            if (hb.phase2 == 0 && hb.move == 0 && ob.move != 2 && hb.hold == 0 && dP2 > 9.0f && 144.0f > dP2)
                start(i, 2);                                       // breath
            if (hb.turn_on != 0 && hb.move != 4 && hb.move != 1 && hb.move != 3) turn(i, dP, 0.025f, false, true);
            break;
        }
        default: break;
    }
    mover(i, T);
    move_tick(i);
}

// FUN_005439d0 [D §3.2]: hunt (bite, laser), free steering.
void Boss06::pattern2(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const Boss06Head& ob = h_[std::size_t(1 - i)];
    const Vec3 dP = dir_p(i);
    const Vec3 Pp = p_pos();
    const float dP2 = d_p2(i);
    switch (hb.pstep) {
        case 0: hb.speed = 0.0f; hb.hold = 120; hb.pstep = 1; [[fallthrough]];
        case 1: if (hb.hold != 0) break; hb.pstep = 2; [[fallthrough]];
        case 2: {
            hb.speed_target = hb.k_speed * 0.1f; hb.accel = 0.1f;
            const int m = hb.move;
            if ((m == 0 || m == 3) && hb.ride > 30) { abort_(i); start(i, 6); }
            const float thr = (very_easy_ || easy_) ? 36.0f : 100.0f;
            if (hb.move == 0 && hb.hold == 0 && dot(hb.fwd, dP) > 0.7f && thr > dP2 && in_arena(Pp)) start(i, 1);
            if (hb.move == 0 && hb.hold == 0 && hb.laser_cd == 0) start(i, 3);
            if (hb.move != 4 && hb.move != 1 && hb.move != 3) turn(i, dP, 0.03f, false, true);
            break;
        }
        default: break;
    }
    // OTHER 0x543c09
    if (ob.blocked > 60) hb.unstick = 120;
    if (hb.unstick != 0) { mover(i, 3); move_tick(i); return; }
    if (hb.free == 0) { move_tick(i); return; }
    hb.dir = hb.fwd;                                               // free steering along the flat forward
    const float s = hb.speed; const Vec3 d = hb.dir;
    const Vec3 n = add(hb.pos, mul(d, s));
    const bool out_of_box = -50.0f > n.x || n.x > -18.0f || 19.0f > n.z || n.z > 39.0f;
    if (out_of_box || probe(i, hb.pos, add(mul(d, hb.radius), mul(d, s)))) {   // BLOCKED 0x543dd4
        ++hb.blocked;
        if (hb.blocked > 60) hb.unstick = 60;
        move_tick(i);
        return;
    }
    if (near_other(i, n)) { move_tick(i); return; }                // the other head in the way: stay, no count
    hb.blocked = 0; hb.pos = n;
    move_tick(i);
}

// FUN_00543e00 [D §3.3]: follow table 3, laser at close range.
void Boss06::pattern3(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    if (hb.stun_req) { abort_(i); start(i, 5); hb.stun_req = 0; }
    if (hb.move == 5) { move_tick(i); return; }
    int m = hb.move;
    switch (hb.pstep) {
        case 0: hb.pstep = 1; [[fallthrough]];
        case 1: if (hb.hold != 0) break; hb.pstep = 2; [[fallthrough]];
        case 2:
            if ((m == 0 || m == 3) && hb.ride > 60) { abort_(i); start(i, 6); m = 6; }
            if (m == 0 && in_->player && 64.0f > d_p2(i)) { start(i, 3); m = 3; }
            break;
        default: break;
    }
    if (m != 3 && m != 4) turn(i, dir_p(i), 0.025f, false, true);  // TURN 0x543f39
    mover(i, 3);
    move_tick(i);
}

// FUN_00544030 [D §6.2].
void Boss06::mover(int i, int T) {
    Boss06Head& hb = h_[std::size_t(i)];
    const Boss06Head& ob = h_[std::size_t(1 - i)];
    if (hb.free == 0) return;
    const float cur = at2(hb.dir.x, hb.dir.z) + kPi;
    const float tgt = at2(hb.target.x - hb.pos.x, hb.target.z - hb.pos.z) + kPi;
    float dl = w2pi(tgt) - w2pi(cur);
    if (0.0f > dl) { if (!(dl >= -kPi)) dl += k2Pi; }
    else { if (!(kPi >= dl)) dl -= k2Pi; }
    const float rate = float(std::fabs(double(dl / kPi))) * 0.1f;
    const float ang = rotate_toward(cur, tgt, rate);
    const Vec3 d = nrm(ef2sim::transform_coord(kBack, ef2sim::rot_ypr(ang, 0.0f, 0.0f)));
    hb.dir = d;
    const Vec3 n = add(hb.pos, mul(d, hb.speed));
    const float ex = hb.pos.x - hb.target.x, ez = hb.pos.z - hb.target.z;
    if (4.0f > ex * ex + ez * ez) {                                // within 2.0 of the target
        if (hb.wp_stop == hb.wp) return;
        ++hb.wp; if (hb.wp >= kWaypoints[T].count) hb.wp = 0;      // FUN_005350e0
        hb.target = waypoint_world(T, hb.wp);
        return;                                                    // no step on the arrival frame
    }
    if (!near_other(i, n) && !probe(i, hb.pos, add(mul(d, hb.speed), mul(d, hb.radius)))) {
        hb.blocked = 0; hb.pos = n;
        if (hb.unstick != 0) --hb.unstick;
        return;
    }
    if (ob.move != 7 && hb.move == 0) {                            // blocked: the roar
        if (R(1000) < 10) { start(i, 7); ++hb.blocked; return; }   // RNG 0x5444d6
    }
    if (hb.wp_stop != hb.wp) { ++hb.wp; if (hb.wp >= kWaypoints[T].count) hb.wp = 0; }   // skip a waypoint
    hb.target = waypoint_world(T, hb.wp);
    hb.dir = nrm(hb.target - hb.pos);
    ++hb.blocked;
}

// FUN_00544750 [D §4.1].
void Boss06::life_tick(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    if (1.0f > in_->player_hp_mirror) { hb.hits_off = true; return; }
    hb.hp = hb.max_hp;                                             // H+0x27c != 0: always [P1 U11]
    if (hb.stunned == 0) {
        static const std::uint8_t kTh[5][4] = {{1, 1, 1, 1}, {1, 2, 3, 4}, {2, 4, 6, 9}, {2, 4, 8, 12}, {4, 6, 10, 14}};
        const std::uint8_t* th = very_easy_ ? kTh[0] : easy_ ? kTh[1] : bb5c_ == 1 ? kTh[3] : bb5c_ == 2 ? kTh[4] : kTh[2];
        const std::uint8_t idx = std::uint8_t(4 - hb.lives);
        if (z_ && z_->present && idx < 4 && z_->hits >= th[idx] && hb.shield != 0 && hb.rearm == 0) {
            out_->zoom = std::make_pair(0.5f, -0.05f);
            z_->hits = 0;
            std::uint8_t dl = hb.lives;
            hb.hold = 60; hb.rearm = 60;                           // dword hb+0x78 = 0x003c003c
            if (dl != 0) { --dl; hb.lives = dl; }
            orb_kill(i, dl);                                       // this head's orbiter #[lives left]
            if (hb.lives == 0) { abort_(i); hb.queued = 1; start(i, 8); }   // last life: fusion break
            else { abort_(1); abort_(0); start(1, 12); start(0, 12); }
        }
    }
    if (hb.rearm != 0) --hb.rearm;
}

// FUN_00542cd0 [D §5].
void Boss06::move_tick(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    switch (hb.move) {
        case 0:                                                    // idle 0x542cfa
            if (hb.step == 0) { set_clip(i, kWait); ++hb.step; }
            break;
        case 1: move_bite(i); break;
        case 2:                                                    // breath 0x542eab
            if (hb.step == 0) {
                hb.speed_target = 0.01f; hb.accel = 0.001f;
                set_clip(i, kAttack1);
                slot_set(i, 0, 385);
                ++hb.step;
            }
            if (hb.step == 1) {
                if (!done(i)) break;
                const std::uint32_t w = draw();                    // RNG 0x542fa1
                const float k = hb.k_time;
                const int n = int(k * 180.0f + 1.0f - k * 120.0f);
                hb.hold = std::uint16_t(int(float(int((w >> 17) % std::uint32_t(n))) + k * 120.0f));
                hb.step = 0x80;                                    // DONE
            }
            if (hb.step == 0x80) finish(i);                        // no hb+0x7f write
            break;
        case 3: move_laser(i); break;
        case 5: move_stun(i); break;
        case 6:                                                    // shake the player off 0x543020
            if (hb.step == 0) { set_clip(i, kShake); ++hb.step; hb.timer = 120; }
            if (hb.step == 1) {
                if (hb.ride != 0 && std::uint32_t(hb.timer) < 90u && in_->player) {
                    const Vec3 d = dir_p(i);
                    out_->player_vel = Vec3{d.x * 0.5f, 0.5f, d.z * 0.5f};
                }
                if (hb.timer != 0) break;
                hb.step = 0x80;
            }
            if (hb.step == 0x80) finish(i);
            break;
        case 7:                                                    // roar when blocked 0x542d57
            if (hb.step == 0) {
                set_clip(i, kShake);
                se(366, hb.pos);
                hb.free = 0;
                hb.timer = int(R(61)) + 60;                        // RNG 0x542e0c
                ++hb.step;
            }
            if (hb.step == 1) {
                if (hb.timer != 0) break;
                hb.step = 0x80;
            }
            if (hb.step == 0x80) { hb.free = 1; finish(i); }       // FINISH7 0x542e36
            break;
        case 8: move_fusion(i); break;
        case 9:                                                    // face Zava 0x5430ed
            if (hb.step == 0) { set_clip(i, kWait); ++hb.step; hb.busy = 1; hb.free = 0; hb.speed = 0.0f; }
            if (hb.step == 1) {
                if (!(z_ && z_->present)) break;
                turn(i, nrm(z_->pos - hb.pos), 0.005f, false, true);
            }
            break;                                                 // never finishes by itself
        case 10:                                                   // barrier shake 0x543262
            if (hb.step == 0) {
                out_->shake = std::make_pair(30, Vec3{0.2f, 0.2f, 0.2f});
                set_clip(i, kShake);
                slot_set(i, 0, 426);
                ++hb.step; hb.timer = 60;
            }
            if (hb.step == 1) {
                turn(i, hb.fwd, 0.05f, false, true);               // its own flat forward: levels the pitch
                if (hb.timer != 0) break;
                hb.step = 0x80;
            }
            if (hb.step == 0x80) finish(i);
            break;
        case 11: move_floor(i); break;
        case 12: move_swap(i); break;
        case 13: move_suction(i); break;
        case 14: move_bomb(i); break;
        case 15: move_death(i); break;
        case 17: move_dive(i); break;
        default: break;   // 4: returns; 16 (FUN_0053c480): [not modelled] never started; > 17: returns
    }
}

// Move 1 — bite / grab, FUN_0053c9b0 [EF §E1.1].
void Boss06::move_bite(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const bool P = in_->player;
    const Vec3 Pp = p_pos();
    const Vec3 dP = dir_p(i);
    switch (hb.step) {
        case 0: set_clip(i, kCharge0); hb.step = 1; hb.timer = 30; [[fallthrough]];
        case 1:
            if (hb.timer != 0) return;
            set_clip(i, kBite); hb.step = 2; hb.free = 0; hb.timer = 30;
            [[fallthrough]];
        case 2:
            turn(i, dP, 0.05f, false, true);
            if (hb.timer != 0) return;
            se(366, hb.pos);
            hb.dir = hb.fwd; hb.step = 3;
            hb.saved_speed = hb.speed; hb.speed = 0.4f; hb.timer = 20;
            [[fallthrough]];
        case 3: {                                                  // the lunge
            hb.speed_target = 0.5f; hb.accel = 0.5f;
            if ((hb.fc & 7u) == 0) {                               // a dust puff; the draw precedes the pool test
                const float s = fb((draw() & 0x7fffffu) | 0x3f800000u) - 1.0f + 1.0f;
                Boss06Effect& e = fx2(i, 19, {hb.pos.x, 6.3f, hb.pos.z});
                e.call = Boss06Call::kInline; e.scale = {s, s, s}; e.or_1dc = 0x100;
            }
            turn(i, dP, 0.05f, true, true);
            hb.dir = hb.fwd;
            const Vec3 np = add(hb.pos, mul(hb.dir, hb.speed));
            const Vec3 e = node(i, kFireEfc) - Pp;
            if (2.0f > std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z)) { hb.step = 4; hb.shield = 0; return; }   // CAUGHT
            if (in_arena(np) && !near_other(i, np) && hb.timer != 0) { hb.pos = np; return; }
            hb.step = 7;
            return;
        }
        case 4:
            set_clip(i, kShake2); hb.esc = 0; hb.esc_x = 0; hb.esc_y = 0;
            hb.step = 5; hb.timer = 480;
            [[fallthrough]];
        case 5: {                                                  // the hold
            out_->flags.push_back({134, 1});
            turn(i, nrm(ef2sim::transform_coord(kBack, ef2sim::rot_ypr(hb.yaw, -0.5235988f, hb.roll))), 0.05f, true, false);
            const bool bst = burst();
            if (P) {
                Boss06Hold h; h.head = i;
                h.pos = add(node(i, kFireEfc), mul(hb.aim, 0.5f));
                h.facing = at2(hb.fwd.z, hb.fwd.x);
                h.section4 = !bst && in_->player_hp != 0.0f && in_->player_section != 4;
                out_->hold = h; ppos_ = h.pos;
                const int t = hb.timer;
                if (t % 60 == 0) out_->rumble.push_back(2);
                if (t % 30 == 0) se(304, hb.pos);
            }
            if (!bst && P && !in_->player_guard) {
                const int t = hb.timer;
                if (t % 30 == 0) {
                    Boss06Effect& e = fx2(i, 279, node(i, kFireEfc));
                    e.scale = {2, 2, 2}; e.stat = true;
                }
                if (t % 60 == 0) out_->damage.push_back({i, 237.0f, false});   // H+0xa4 = 237.0 for the call
            }
            if (in_->pad_pressed) ++hb.esc;                        // the escape input 0x53d2a8
            else if ((in_->pad_x == 1 || in_->pad_x == -1) && hb.esc_x != in_->pad_x) { hb.esc_x = in_->pad_x; ++hb.esc; }
            else if ((in_->pad_y == 1 || in_->pad_y == -1) && hb.esc_y != in_->pad_y) { hb.esc_y = in_->pad_y; ++hb.esc; }
            static const std::uint8_t kEsc[5] = {2, 4, 8, 8, 8};
            if (hb.esc >= kEsc[lvl_]) hb.timer = 0;
            // The exe reads P.HP here without a null test; with no player the test is skipped.
            if (P && in_->player_hp == 0.0f) hb.timer = 0;
            else if (hb.timer != 0) return;
            hb.step = 6;
            [[fallthrough]];
        }
        case 6: {                                                  // the release
            if (P) {
                Boss06Release r; r.head = i;
                r.pos = add(node(i, kFireEfc), hb.fwd);
                if (in_->player_hp != 0.0f) r.vel = mul(hb.fwd, 1.5f);   // + ASO SetSection(9)
                out_->release = r; ppos_ = r.pos;
            }
            out_->flags.push_back({134, 0});
            hb.step = 7;
            [[fallthrough]];
        }
        case 7:
            set_clip(i, kWait); hb.step = 8; hb.shield = 1; hb.rearm = 60; hb.timer = 60;
            [[fallthrough]];
        case 8:                                                    // recovery
            if (P && hb.fc % 10u == 0) out_->bursts.push_back({i, ppos_});   // FUN_0058a5f0 [not modelled: its draws]
            turn(i, hb.fwd, 0.1f, true, true);
            if (hb.timer != 0) return;
            hb.hold = 60; hb.step = 0x80;
            [[fallthrough]];
        case 0x80:                                                 // exit / abort
            if (P) out_->player_free = true;
            out_->flags.push_back({134, 0});
            start_move_(i, 0);
            hb.free = 1; hb.shield = 1; hb.rearm = 60; hb.speed = hb.saved_speed;
            return;
        default: return;
    }
}

// Move 5 — stun, FUN_0053f210 [EF §E1.2].
void Boss06::move_stun(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    switch (hb.step) {
        case 0:
            se(367, hb.pos);
            set_clip(i, kPiyori1); hb.step = 1; hb.stunned = 1; hb.busy = 1;
            [[fallthrough]];
        case 1: {
            if (!done(i)) return;
            set_clip(i, kPiyori2);
            Boss06Effect& e = fx2(i, 60, node(i, kPiyoEfc));       // InlineFX piyopiyo; [H] the pool is never full
            e.call = Boss06Call::kInline; e.or_1dc = 0x100; e.scale = {1.25f, 1.25f, 1.25f}; e.stat = true;
            e.handle = next_handle_++;
            e.parent = Boss06Parent::kNode; e.parent_index = kPiyoEfc; e.at_scale = {1.25f, 1.25f, 1.25f}; e.at_mode = 2;
            hb.stars = e.handle;
            hb.step = 2; hb.timer = 300;
            [[fallthrough]];
        }
        case 2:
            if (hb.timer != 0) return;
            out_->kill_ids.push_back(60);
            set_clip(i, kPiyori3);
            slot_set(i, 0, 385);                                   // the wake-up breath
            hb.step = 3;
            [[fallthrough]];
        case 3:
            if (!done(i)) return;
            hb.hold = 60; hb.step = 0x80;
            [[fallthrough]];
        case 0x80:
            hb.busy = 0; hb.stunned = 0;
            if (hb.stars != 0) { out_->kill_ids.push_back(60); hb.stars = 0; }
            start_move_(i, 0);
            return;
        default: return;
    }
}

// Move 3 — laser, FUN_0053f9e0 [EF §E1.3].
void Boss06::move_laser(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const Boss06Head& ob = h_[std::size_t(1 - i)];
    const float k = hb.k_time;
    switch (hb.step) {
        case 0:
            slot_set(i, 0, 389);                                   // the charge glow
            hb.timer = 120;
            if (hb.pattern == 3) hb.timer = int(k * 60.0f);
            if (ob.move == 11) hb.timer = 60;
            hb.step = 1;
            [[fallthrough]];
        case 1:
            turn(i, dir_p(i), 0.05f, false, true);
            if (hb.timer != 0) return;
            set_clip(i, kLaser0);
            slot_set(i, 0, 322);                                   // the tell beam
            hb.step = 2; hb.free = 0; hb.speed = 0.0f; hb.timer = 30;
            [[fallthrough]];
        case 2: {
            if (!done(i)) return;
            if (hb.timer != 0) return;
            slot_kill(i, 0, true);                                 // FUN_00534eb0(S, soft 1)
            set_clip(i, kLaser1);
            slot_set(i, 0, 391);                                   // FUN_0053d6b0: THE LASER
            out_->shake = std::make_pair(30, Vec3{0.0f, 0.5f, 0.0f});
            hb.v8c = {hb.pitch, hb.yaw, hb.roll};
            const Vec3 d = dir_p(i);
            const float yawP = at2(d.x, d.z) + kPi;
            const float yawAim = at2(hb.aim.x, hb.aim.z) + kPi;
            hb.v8c.y += ai::angle_diff(yawAim, yawP) > 0 ? fb(0x3f32b8c2) : -fb(0x3f32b8c2);   // 40 deg, 0x6bd364
            hb.step = 3; hb.timer = 45;
            [[fallthrough]];
        }
        case 3:                                                    // the sweep
            turn_ang(i, hb.v8c, hb.pattern == 3 ? fb(0x3be56042) : fb(0x3c03126f));   // 0.007 / 0.008
            if (done(i)) { set_clip(i, kLaser2); out_->shake = std::make_pair(60, Vec3{0.05f, 0.05f, 0.05f}); }
            if (hb.timer != 0) return;
            set_clip(i, kLaser3); hb.step = 4;
            [[fallthrough]];
        case 4:
            if (!done(i)) return;
            set_clip(i, kWait);
            hb.timer = hb.pattern == 3 ? 0 : int(k * 120.0f);
            hb.step = 5;
            [[fallthrough]];
        case 5:
            turn(i, dir_p(i), 0.01f, false, true);
            if (hb.timer != 0) return;
            if (hb.pattern == 2) { hb.laser_cd = 240; hb.hold = std::uint16_t(int(k * 60.0f)); }
            else if (hb.pattern == 3) { hb.laser_cd = 0; hb.hold = 0; }
            else { hb.laser_cd = 240; hb.hold = std::uint16_t(int(k * 120.0f)); }
            hb.step = 0x80;
            [[fallthrough]];
        case 0x80:
            out_->kill_ids.push_back(389 + (hb.id == 0 ? 1 : 0)); // every charge glow of that id
            slot_kill(i, 0, false);                                // by slot id 391: misses head a's 394 [EF §E1.3]
            start_move_(i, 0); hb.free = 1; hb.speed = 0.0f;
            return;
        default: return;
    }
}

// Move 8 — fusion break, FUN_0053ff70 [E2 §2]. One frame.
void Boss06::move_fusion(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    if (hb.step == 0) {
        hb.shield = 0; hb.phase2 = 0;
        hb.busy = 0;
        for (int k = 0; k < kOrbiters; ++k) orb_kill(i, k);
        if (z_ && z_->present) {
            const Vec3 v = rand_between({1, 0, -1}, {-1, 0, -1}); // 3 draws (y and z wasted)
            const Vec3 vh = nrm(v);
            const Vec3 n = node(i, kBodyEfc);
            Boss06ZavaThrow t; t.head = i;
            t.pos = add(add(vh, vh), n);
            t.vel = mul(vh, 0.5f);
            out_->zava_throw = t;
            z_->pos = t.pos;
            fx2(i, 440, n);
            se(307, hb.pos);
        }
        if (bb5c_ != 2 && hb.busy == 0) hb.stun_req = 1;           // 0x540274
        hb.free = 1; hb.step = 0x80;
    }
    if (hb.step == 0x80) finish(i);
}

// Move 11 — ice floor (a) / fire floor (b), FUN_00540d40 [E2 §3].
void Boss06::move_floor(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    Boss06Head& ob = h_[std::size_t(1 - i)];
    const bool isA = hb.id == 0, isB = hb.id == 1;
    switch (hb.step) {
        case 0:
            set_clip(i, kLaser3); hb.busy = 1; hb.free = 0; hb.speed = 0.0f;
            fx2(i, 441, {hb.pos.x, 6.05f, hb.pos.z});
            se(295, hb.pos);
            hb.step = 1; hb.timer = 30;
            return;
        case 1:
            if (hb.timer != 0) return;
            hb.alpha_target = 0.9f; hb.alpha_step = 1.0f;
            hb.step = 2;
            [[fallthrough]];
        case 2:
            hb.pos.y -= 0.1f;
            if (hb.pos.y + 8.0f > 6.0f) return;
            hb.alpha_target = 0.0f; hb.alpha_step = 1.0f;          // hidden
            ob.queued = 3;                                         // the OTHER head: chase
            hb.step = 3;
            [[fallthrough]];
        case 3:
            slot_set(i, 0, 427);
            fog_fade(i, isA ? Vec3{0, 0, 1.0f} : Vec3{1.0f, 0, 0});
            out_->zoom = std::make_pair(0.5f, -0.001f);
            hb.pil_interval = 0x28; hb.pil_count = 0;
            hb.v8c = p_pos();
            hb.step = 4; hb.timer = 60; hb.hold = 60;
            [[fallthrough]];
        case 4: {                                                  // the floor, until an AbortMove
            bool on_floor = false;
            if (in_->player && in_->player_grounded) on_floor = 7.0f > ppos_.y - in_->player_r324;
            if (isA && on_floor) out_->surface_ice = true;         // byte P+0x4b9 = 0x0b
            if (isA) out_->pillar_break_test = true;               // FUN_00540600 [not modelled: host-side scan]
            if (isB && on_floor && hb.fc % 30u == 0) {             // THE BURN
                out_->shake = std::make_pair(1000, Vec3{0.05f, 0.05f, 0.05f});
                if (in_->player) out_->damage.push_back({i, 226.0f, true});
            }
            const float s = isB ? 1.5f : 1.2f;
            if (hb.fc % std::uint32_t(hb.pil_interval) != 0) return;
            if (hb.pil_count >= 0x40) return;
            const std::uint32_t w = draw() >> 17;                  // one draw 0x5411b7
            const std::uint32_t n = lvl_ <= 1 ? (isB ? w % 81u + 100u : w % 61u + 120u)
                                               : (isB ? w % 41u + 20u : w % 21u + 60u);
            hb.pil_interval = std::uint8_t(n);
            Vec3 Tg;
            if (isA) {
                Tg = p_pos();
                if (in_->player) Tg = add(Tg, mul(in_->player_vel, 80.0f));
                if (!in_arena(Tg)) Tg = p_pos();
                ++hb.pil_count;
            } else {
                Tg = hb.v8c; hb.v8c = p_pos();                     // where the player was at the previous shot
            }
            Tg.y = 6.02f;
            Boss06Effect& e = fx2(i, isB ? 256 : 290, Tg);
            e.scale = {s, s, s}; e.stat = true; e.power = isB ? 10.0f : 4.0f; e.knock = 0.0f; e.rtype = 3;
            se(isB ? 301 : 298, Tg);
            return;
        }
        case 0x80:
            out_->shake = std::make_pair(0, Vec3{});
            if (in_->zoom_active || out_->zoom) out_->zoom = std::make_pair(0.5f, -0.01f);   // only when a blur is live
            slot_set(i, 1, 429);                                   // the floor's break-up
            slot_kill(i, 2, false);
            hb.step = 0x81; hb.timer = 30;
            return;
        case 0x81:
            if (hb.timer != 0) return;
            out_->kill_ids.push_back(291); out_->kill_ids.push_back(290);
            slot_kill(i, 0, false);
            fog_fade(i, hb.fog.saved);
            fx2(i, 441, {hb.pos.x, 6.05f, hb.pos.z});
            se(295, hb.pos);
            hb.step = 0x82; hb.timer = 30;
            return;
        case 0x82:
            if (hb.timer != 0) return;
            hb.alpha_target = 1.0f; hb.alpha_step = 1.0f;
            hb.step = 0x83;
            [[fallthrough]];
        case 0x83:
            hb.pos.y += 0.1f;
            if (6.1f > hb.pos.y) return;
            ob.queued = 1;
            hb.pos.y = 6.1f;
            start_move_(i, 0);                                     // hb+0x114 = 1 and hb+0x7f = 0 are left as they are
            return;
        default: return;
    }
}

// Move 17 — dive and wait, FUN_005416a0 [E2 §4].
void Boss06::move_dive(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    Boss06Head& ob = h_[std::size_t(1 - i)];
    switch (hb.step) {
        case 0: {
            set_clip(i, kWait); hb.busy = 1; hb.free = 0; hb.speed = 0.0f;
            const Vec3 q{hb.pos.x, 6.05f, hb.pos.z};
            fx2(i, 441, q); se(295, q);
            hb.step = 1; hb.timer = 30;
            return;
        }
        case 1:
            if (hb.timer != 0) return;
            hb.alpha_target = 0.9f; hb.alpha_step = 1.0f; hb.step = 2;
            [[fallthrough]];
        case 2:
            hb.pos.y -= 0.1f;
            if (hb.pos.y + 8.0f > 6.0f) return;
            hb.alpha_target = 0.0f; hb.alpha_step = 1.0f;
            hb.step = 4; hb.timer = 30;                            // "inc dl" twice: no case 4
            return;
        case 0x80: hb.step = 0x81; hb.timer = 30; return;
        case 0x81:
            if (hb.timer != 0) return;
            fx2(i, 441, {hb.pos.x, 6.05f, hb.pos.z});              // no sound here
            hb.timer = 30; hb.step = 0x82;
            return;
        case 0x82:
            if (hb.timer != 0) return;
            hb.alpha_target = 1.0f; hb.alpha_step = 1.0f;
            se(296, hb.pos);
            hb.step = 0x83;
            [[fallthrough]];
        case 0x83:
            hb.pos.y += 0.1f;
            if (6.1f > hb.pos.y) return;
            ob.queued = 1;
            hb.pos.y = 6.1f;
            start_move_(i, 0);
            return;
        default: return;
    }
}

// Move 12 — head swap, FUN_005419f0 [E2 §5].
void Boss06::move_swap(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const Boss06Head& ob = h_[std::size_t(1 - i)];
    switch (hb.step) {
        case 0:
            set_clip(i, kWait, 0);
            fx2(i, 439, node(i, kBodyEfc)); se(306, hb.pos);
            hb.speed = 0.0f; hb.busy = 1; hb.free = 0;
            if (hb.shield != 0) {                                  // this head is fused
                Boss06Head& a = h_[0]; Boss06Head& b = h_[1];
                std::swap(a.pos, b.pos);
                std::swap(a.target, b.target);
                std::swap(a.fwd, b.fwd);
                std::swap(a.aim, b.aim);
                std::swap(a.pitch, b.pitch); std::swap(a.yaw, b.yaw); std::swap(a.roll, b.roll);   // no SetRot
                std::swap(a.wp, b.wp);
                std::swap(a.speed_target, b.speed_target); std::swap(a.accel, b.accel);
                a.hold = b.hold = 60;
                a.speed = b.speed = 0.0f;
                out_->zava_reseat = true;                          // FUN_0053aa90(FUN_005355e0())
            }
            hb.step = 1; hb.timer = 60;
            [[fallthrough]];
        case 1:
            if (hb.timer != 0) return;
            if (ob.move == 12 && ob.step < 1) return;              // the other head has not run its step 0 yet
            hb.step = 0x80;
            [[fallthrough]];
        case 0x80:
            if (hb.shield == 0) hb.busy = 0;
            hb.free = 1;
            if (z_ && z_->present) z_->hits = 0;                   // Z+0x7a8 = 0
            finish(i);
            return;
        default: return;
    }
}

// Move 13 — suction (head a), FUN_00541de0 [E2 §6].
void Boss06::move_suction(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    switch (hb.step) {
        case 0: hb.step = 1; hb.speed_target = 0.0f; hb.accel = 0.01f; [[fallthrough]];   // brake
        case 1:
            if (hb.speed >= 0.01f) return;
            slot_set(i, 0, 270); slot_set(i, 1, 348);
            hb.step = 2; hb.timer = 300; hb.free = 0;
            [[fallthrough]];
        case 2:
            if (in_->player && !burst()) out_->player_vel = mul(dir_p(i), -0.1f);
            if (hb.timer != 0) {
                const Vec3 q = p_pos();
                const float r = hb.radius + 2.0f;
                if ((hb.pos.z - q.z) * (hb.pos.z - q.z) + (hb.pos.x - q.x) * (hb.pos.x - q.x) > r * r) return;
            }
            slot_kill(i, 0, false); slot_kill(i, 1, false);
            hb.step = 3; hb.timer = 120;
            return;
        case 3:
            if (hb.timer != 0) return;
            hb.step = 0x80;
            [[fallthrough]];
        case 0x80:
            hb.free = 1; slot_kill(i, 0, false); slot_kill(i, 1, false);
            start_move_(i, 0);
            out_->kill_ids.push_back(270); out_->kill_ids.push_back(348);
            return;
        default: return;
    }
}

// Move 14 — bomb (head b), FUN_005420b0 [E2 §7].
void Boss06::move_bomb(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    switch (hb.step) {
        case 0: set_clip(i, kAttack1); hb.step = 1; hb.timer = 45; [[fallthrough]];
        case 1: {
            if (hb.timer != 0) return;
            const Vec3 m = node(i, kFireEfc);
            Boss06Bullet b; b.head = i; b.kind = 4; b.effect = 442; b.pos = m; b.dir = hb.fwd;
            b.f[0] = 0.15f; b.f[1] = -0.0005f; b.f[2] = 0.0f; b.f[3] = 8.0f; b.f[4] = 3.0f;
            b.f[5] = ibits(0); b.f[6] = ibits(0); b.f[7] = ibits(3);
            b.f[8] = fb(0x3dd67750); b.f[9] = 1.0f; b.f[10] = 20.0f; b.f[11] = 100.0f; b.f[12] = 0.0f;
            out_->bullets.push_back(b);
            Boss06Effect& g = fx2(i, 321, m);
            g.rot = {0, kHalfPi, 0}; g.stat = true;
            g.parent = Boss06Parent::kNode; g.parent_index = kFireEfc; g.at_off = {0, 0, -1.0f}; g.at_mode = 1;
            se(305, m);
            hb.step = 2;
            [[fallthrough]];
        }
        case 2:
            if (!done(i)) return;
            hb.hold = std::uint16_t(int(hb.k_time * 60.0f));
            hb.step = 0x80;
            start_move_(i, 0);
            return;
        case 0x80: start_move_(i, 0); return;
        default: return;
    }
}

// Move 15 — head death, FUN_005423b0 [E2 §8].
void Boss06::move_death(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const bool isA = hb.id == 0;
    const auto puff = [&](const Vec3& p, float s) { fx2(i, 303, p).scale = {s, s, s}; };
    switch (hb.step) {
        case 0:
            for (int k = 0; k < kOrbiters; ++k) orb_kill(i, k);
            for (int k = 0; k < kFxSlots; ++k) slot_kill(i, k, false);
            out_->kill_ids.push_back(442); out_->kill_ids.push_back(321);
            se(186, hb.pos);
            set_clip(i, kLaser2);
            puff(node(i, kBodyEfc), 3.0f);
            puff(hb.pos, 3.0f);
            hb.step = 2; hb.timer = isA ? 90 : 0;                  // step 1 does not exist
            [[fallthrough]];
        case 2:
            if (hb.fc % 20u == 0) puff(node(i, kBodyEfc), 3.0f);
            if (hb.fc % 40u == 0) puff(hb.pos, 3.0f);
            turn(i, {hb.fwd.x, fb(0x3f32b8c2), hb.fwd.z}, 0.02f, true, false);   // nose up (a y component, as the bytes)
            if (hb.timer != 0) return;
            out_->materials_black[std::size_t(i)] = true;
            puff(node(i, kBodyEfc), 3.0f);
            puff(hb.pos, 2.0f);
            set_clip(i, kPiyori1);                                 // (A'+0x24 = 0.5 is overwritten by SetClip's 1.0)
            hb.step = 3;
            [[fallthrough]];
        case 3:
            if (isA && hb.fc % 30u == 0) out_->rumble.push_back(4);
            turn(i, {hb.fwd.x, fb(0x3f32b8c2), hb.fwd.z}, 0.02f, true, false);
            if (!done(i)) return;
            hb.alpha_target = 0.0f; hb.alpha_step = fb(0x3b888889);   // 1/240
            set_clip(i, kPiyori2);
            fx2(i, 422, {hb.pos.x, 6.01f, hb.pos.z});
            se(571, hb.pos); if (isA) se(572, hb.pos);
            hb.step = 4;
            [[fallthrough]];
        case 4:
            if (isA && hb.pos.y > 3.0f && hb.fc % 30u == 0) out_->rumble.push_back(4);
            if (hb.fc % 30u == 0) {
                const Vec3 r = rand_between({-2, -2, -2}, {2, 2, 2});   // 3 draws
                fx2(i, 67, add(node(i, kBodyEfc), r));
            }
            turn(i, {hb.fwd.x, fb(0x3ff5be0b), hb.fwd.z}, 0.02f, true, false);
            hb.pos.y -= 0.02f;
            if (hb.pos.y > -1.0f) return;
            hb.step = 5;
            [[fallthrough]];
        case 5:
            hb.step = 0x80;                                        // (A'+0x24 = 1.0)
            hb.dead = true; hb.collide = false;                    // H+0x11 |= 2; H+0x60 &= ~0x80
            return;
        default: return;
    }
}

// FUN_0053eab0 [EF §F.3].
void Boss06::spawn_orbiters(int i, const Boss06Input& in, Boss06Output& out) {
    Scope sc(*this, in, z_, out);
    Boss06Head& hb = h_[std::size_t(i)];
    const Vec3 b = node(i, kBodyEfc);
    const int id = 424 + (hb.id != 0 ? 1 : 0);
    for (int k = 0; k < int(hb.lives) && k < kOrbiters; ++k) {
        Boss06Effect& e = fx2(i, id, b);
        e.rot = {1.0f, 1.0f, 1.0f}; e.scale = {0.9f, 0.9f, 0.9f}; e.stat = true; e.knock = 0.0f;
        e.handle = next_handle_++;                                 // [H] the pool is never full (the exe does not check)
        e.parent = Boss06Parent::kOrbit; e.parent_index = k; e.at_scale = {0.9f, 0.9f, 0.9f}; e.at_mode = 1;
        e.flag201 = true;
        hb.orb[std::size_t(k)] = e.handle;
    }
}

// FUN_0053ecd0 [EF §F.4].
void Boss06::orbiters(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    int n = 0;
    for (int k = 0; k < kOrbiters; ++k) n += hb.orb[std::size_t(k)] != 0;
    if (n == 0) return;                                            // nothing to do without a handle
    const Vec3 b = node(i, kBodyEfc);
    static const int kPhase[4] = {0, 180, 90, 270};
    float factor = 1.0f; bool mode = false;
    if (z_ && z_->present) {
        const float r = z_->hp / z_->max_hp;
        factor = (1.0f - r + 1.0f) * 1.5f;
        if (!very_easy_ && !easy_) mode = 0.4f > r;
    }
    for (int k = 0; k < kOrbiters; ++k) {
        if (hb.orb[std::size_t(k)] == 0) continue;
        const float spd = std::min(3.0f, (float(4 - n) + 1.0f) * factor);
        int a = int(spd * float(hb.fc)) + kPhase[k];
        a %= 360;
        M4 M = ef2sim::mul(ef2sim::translation(0, 0, 3.5f), ef2sim::rot_ypr(float(a) * kDeg, 0, 0));
        float tilt = mode ? float((hb.fc * 2u) % 360u) : 45.0f;
        tilt *= -kDeg;
        M = ef2sim::mul(M, ef2sim::rot_ypr(0, tilt, 0));
        M = ef2sim::mul(M, ef2sim::rot_ypr(at2(hb.fwd.x, hb.fwd.z) + kPi, 0, 0));
        M = ef2sim::mul(M, ef2sim::translation(b.x, b.y, b.z));
        hb.orbit[std::size_t(k)] = M;
        if (mode && (k & 1) == 0 && hb.fc % 10u == 0) {
            const bool isB = hb.id == 1;
            Boss06Effect& e = fx2(i, isB ? 385 : 393, row3(M));
            e.call = Boss06Call::kFx; e.rot = {1.0f, 1.0f, 1.0f};
            e.scale = isB ? Vec3{0.01f, 0.01f, 0.01f} : Vec3{1, 1, 1};
            e.stat = true; e.knock = 0.0f;
        }
    }
}

// FUN_0053d700 [EF §F.2].
void Boss06::fx_slots(int i) {
    Boss06Head& hb = h_[std::size_t(i)];
    const bool isA = hb.id == 0;
    const auto cam = [&] { return nrm(in_->cam_eye - in_->cam_target); };
    for (int k = 0; k < kFxSlots; ++k) {
        Boss06FxSlot& S = hb.slots[std::size_t(k)];
        switch (S.id) {
            case 385: {                                            // the breath
                ++S.timer; if (S.timer < 40) break;
                if (hb.fc % 3u != 0) break;
                if (S.count == 0) se(291 + (isA ? 1 : 0), hb.pos);
                if (S.count >= 10) { S.id = 0; break; }
                ++S.count;
                const Vec3 pos = node(i, kFireEfc);
                Boss06Bullet b; b.head = i; b.kind = 0x21; b.effect = 385 + (isA ? 1 : 0); b.pos = pos; b.dir = hb.fwd;
                b.f[0] = 0.18f; b.f[1] = -0.001f; b.f[2] = 0.0005f; b.f[3] = isA ? 0.0f : 3.0f; b.f[4] = 3.0f;
                b.f[5] = ibits(0); b.f[6] = ibits(0); b.f[7] = ibits(0);
                b.f[8] = 0.0f; b.f[9] = 1.0f; b.f[10] = 15.0f; b.f[11] = 0.0f; b.f[12] = 0.0f;
                b.flag201 = true;
                out_->bullets.push_back(b);
                Boss06Effect& g = fx2(i, 389 + (isA ? 1 : 0), pos);   // the muzzle puff
                g.rot = {0, kHalfPi, 0}; g.stat = true;
                g.parent = Boss06Parent::kNode; g.parent_index = kFireEfc; g.at_off = {0, 0, -0.5f}; g.at_mode = 1;
                break;
            }
            case 389: {                                            // the charge glow
                ++S.count; ++S.timer; if (S.timer < 40) break;
                if ((hb.fc & 7u) != 0) break;
                Boss06Effect& g = fx2(i, 389 + (isA ? 1 : 0), node(i, kFireEfc));
                g.rot = {0, kHalfPi, 0}; g.stat = true;
                g.parent = Boss06Parent::kNode; g.parent_index = kFireEfc; g.at_off = {0, 0, -0.5f}; g.at_mode = 1;
                break;
            }
            case 322: {                                            // the tell beam
                const Vec3 n = node(i, kFireEfc);
                S.m = ef2sim::mul(ef2sim::translation(0, 0, 1.0f),
                                  ef2sim::mul(ef2sim::rot_ypr(hb.yaw, hb.pitch, hb.roll), ef2sim::translation(n.x, n.y, n.z)));
                if (S.count >= 1) break;
                S.count = 1;
                Boss06Effect& e = fx2(i, 322, n);
                e.scale = {1, 1, 2}; e.stat = true; e.knock = 0.0f;
                e.handle = S.handle = next_handle_++;              // [H] the spawn never fails
                e.parent = Boss06Parent::kSlot; e.parent_index = k; e.at_scale = {1, 1, 2}; e.at_mode = 1; e.flag201 = true;
                se(365, n);
                break;
            }
            case 391: {                                            // the laser beam
                const Vec3 n = node(i, kFireEfc);
                S.m = ef2sim::mul(ef2sim::rot_ypr(hb.yaw, hb.pitch, hb.roll), ef2sim::translation(n.x, n.y, n.z));
                if (S.count == 0) se(293 + (isA ? 1 : 0), hb.pos);
                if (S.count >= 1) break;
                S.count = 1;
                Boss06Effect& e = fx2(i, isA ? 394 : 391, add(n, mul(hb.aim, 2.0f)));
                e.stat = true; e.power = isA ? 9.0f : 12.0f; e.knock = 0.0f; e.rtype = 3;
                e.handle = S.handle = next_handle_++;
                e.parent = Boss06Parent::kSlot; e.parent_index = k; e.at_mode = 1; e.flag201 = true;
                break;
            }
            case 427: {                                            // the floor
                if (S.count >= 1) break;
                S.count = 1;
                const int id2 = 427 + (isA ? 0 : 1);
                S.id = std::uint16_t(id2);                         // b becomes 428 (an id with no case)
                const Vec3 pos{-34.0f, 6.01f, 29.0f};
                Boss06Effect& e = fx2(i, id2, pos);
                e.stat = true;
                e.handle = S.handle = next_handle_++;
                out_->rumble.push_back(3);
                se(isA ? 297 : 300, pos);
                break;
            }
            case 429: {                                            // the floor burst
                const Vec3 pos{-34.0f, 6.02f, 29.0f};
                fx2(i, isA ? 429 : 438, pos).stat = true;
                se(isA ? 299 : 302, pos);
                S.id = 0;
                out_->rumble.push_back(3);
                break;
            }
            case 270: {                                            // the eye charge (move 13)
                const Vec3 e3 = node(i, kEyeCEfc);
                S.m = ef2sim::mul(ef2sim::rot_ypr(0, kHalfPi, 0),   // D3DXMatrixRotationX(pi/2)
                                  ef2sim::mul(ef2sim::translation(0, 0, -0.1f), ef2sim::translation(e3.x, e3.y, e3.z)));
                if (hb.fc % 10u != 0) break;
                se(303, hb.pos);
                Boss06Effect& e = fx2(i, 270, node(i, kPiyoEfc));
                e.handle = S.handle = next_handle_++;              // overwritten every 10th frame
                e.parent = Boss06Parent::kSlot; e.parent_index = k; e.at_scale = {1.5f, 1.5f, 1.5f}; e.at_mode = 1;
                break;
            }
            case 348: {                                            // the marker on the player (move 13, slot 1)
                const Vec3 c = add(p_pos(), cam());
                S.m = ef2sim::translation(c.x, c.y, c.z);
                if (S.count >= 1) break;
                S.count = 1;
                Boss06Effect& e = fx2(i, 348, p_pos());
                e.scale = {1.5f, 1.5f, 1.5f};
                e.handle = S.handle = next_handle_++;
                e.parent = Boss06Parent::kSlot; e.parent_index = k; e.at_mode = 2; e.flag203 = true;
                break;
            }
            case 426: {                                            // the barrier (move 10)
                ++S.timer;
                if (S.timer > 60) { slot_kill(i, k, true); break; }
                const Vec3 n = mul(cam(), 4.0f);
                S.m = ef2sim::mul(in_->head[std::size_t(i)].nodes[kBodyEfc], ef2sim::translation(n.x, n.y, n.z));
                if (S.count >= 1) break;
                S.count = 1;
                Boss06Effect& e = fx2(i, 426, add(hb.pos, n));
                e.scale = {2, 2, 2}; e.stat = true; e.knock = 3.0f;
                e.handle = S.handle = next_handle_++;
                e.parent = Boss06Parent::kSlot; e.parent_index = k; e.at_mode = 2;
                se(395, hb.pos);
                out_->rumble.push_back(3);
                break;
            }
            default: break;
        }
    }
}

// FUN_0053b930 [D §7.1].
bool Boss06::accept_hit(int i, bool entry_hittable, const Vec3& point, std::uint32_t now_ms, const Boss06Input& in,
                        Boss06Output& out) {
    Scope sc(*this, in, z_, out);
    Boss06Head& hb = h_[std::size_t(i)];
    if (hb.stunned == 0 && in.player_section == 0x10f && bb5c_ != 2 && hb.busy == 0) hb.stun_req = 1;
    const Boss06HeadIn& a = in.head[std::size_t(i)];
    if (a.count == 0 && a.section == 0) set_clip(i, kDamage);      // the flinch (WAIT playing, not completed [U-D3])
    if (hb.shield != 0) {
        const std::int32_t d = std::int32_t(last_splash_ms_ - now_ms);
        if ((d < 0 ? -d : d) > 200) {
            if (entry_hittable) {                                  // [H] the effect pool has room
                Boss06Effect& e = fx2(i, 344, point);              // bs_02_taieki
                e.call = Boss06Call::kInline; e.scale = {2.0f, 2.0f, 2.0f}; e.or_1dc = 0x106;
            }
            last_splash_ms_ = now_ms;
        }
    }
    return false;                                                  // xor al, al @0x53bb9b
}

// FUN_0053b780 [D §7.2]. [not modelled] the contact-damage call: dead, no head entry has +0x8b [D5].
std::optional<Vec3> Boss06::touch(int i, int entry, const Vec3& actor_pos) const {
    if (entry == 1) return std::nullopt;
    const Boss06Head& hb = h_[std::size_t(i)];
    return mul(nrm(actor_pos - hb.pos), hb.speed);
}

}  // namespace cleria
