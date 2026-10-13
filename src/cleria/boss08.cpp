// Darm (boss08) — see boss08.hpp and build/boss_re/DARM*.md.
#include "cleria/boss08.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace cleria {
namespace boss08 {

const ClipInfo kBodyClipTable[kBodyClips] = {
    {"01_NormalWait", 10, 49, true},                 {"02_LeftHandSwing", 60, 100, false},
    {"03_LeftHandRaise", 110, 159, false},           {"04_RightHandSwing", 170, 210, false},
    {"05_RightHandRaise", 220, 269, false},          {"06_BellyMouthBreath_Start", 280, 303, false},
    {"07_BellyMouthBreath_Loop", 303, 310, true},    {"08_BellyMouthBreath_End", 311, 329, false},
    {"09_Normal>>Low", 340, 359, false},             {"10_LowWait", 370, 409, true},
    {"11_Jump", 420, 460, false},                    {"12_SpearRightSwing", 470, 500, false},
    {"13_SpearLeftSwing", 505, 540, false},          {"14_Spear3timesAttack", 545, 595, false},
    {"15_SpearOnTheGround&Concentration_Start", 600, 662, false},
    {"16_SpearOnTheGround&Concentration_Loop", 663, 665, true},
    {"17_SpearOnTheGround&Concentration_End", 665, 714, false},
    {"18_Low>>Normal", 720, 739, false},             {"19_Normal>>High", 750, 774, false},
    {"20_HighWait", 775, 814, true},                 {"21_RotateSpearAboveTheHead_Start", 820, 827, false},
    {"22_RotateSpearAboveTheHead_Loop", 828, 851, true}, {"23_RotateSpearAboveTheHead_End", 852, 859, false},
    {"24_MachineGunStab", 865, 905, false},          {"25_QuakeTheWing_Start", 910, 929, false},
    {"26_QuakeTheWing_Loop", 930, 937, true},        {"27_QuakeTheWing_End", 938, 950, false},
    {"28_High>>Normal", 955, 974, false},            {"29_Normal>>OnBack", 980, 1009, false},
    {"30_OnBackWait", 1015, 1054, true},             {"31_LeftHandThunder", 1060, 1110, false},
    {"32_LeftHandPoint", 1115, 1164, false},         {"33_RightHandThunder", 1175, 1225, false},
    {"34_RightHandPoint", 1230, 1279, false},        {"35_DoubleHandThunder", 1285, 1349, false},
    {"36_DoubleLariat", 1360, 1405, false},          {"37_NormalDown", 1410, 1411, false},
    {"38_LowDown", 1415, 1416, false},               {"39_HighDown", 1420, 1421, false},
};
// [H] "15_Right&LeftRotate_Loop": the spec abbreviates the middle of that one name ("15_…_Loop").
const ClipInfo kTailClipTable[kTailClips] = {
    {"01_NormalWait", 10, 89, true},           {"02_Normal>>Low", 380, 400, false},     {"03_LowWait", 410, 489, true},
    {"04_Jump", 500, 539, false},              {"05_Low>>Normal", 800, 819, false},     {"06_Normal>>High", 830, 854, false},
    {"07_HighWait", 855, 935, true},           {"08_LeftRotate_Start", 945, 964, false}, {"09_LeftRotate_Loop", 965, 1044, true},
    {"10_LeftRotate_End", 1045, 1064, false},  {"11_RightRotate_Start", 1075, 1094, false},
    {"12_RightRotate_Loop", 1095, 1174, true}, {"13_RightRotate_End", 1175, 1194, false},
    {"14_Right&LeftRotate_Start", 1205, 1224, false}, {"15_Right&LeftRotate_Loop", 1225, 1304, true},
    {"16_Right&LeftRotate_End", 1305, 1324, false},   {"17_High>>Normal", 1335, 1354, false},
    {"18_Normal>>OnBack", 1360, 1389, false},  {"19_OnBackWait", 1395, 1474, true},     {"20_Beat", 1480, 1560, false},
    {"21_Guard_Start", 1570, 1599, false},     {"22_Guard_Loop", 1600, 1639, true},     {"23_Guard_End", 1640, 1660, false},
    {"24_StabInTheGround", 1670, 1689, false}, {"25_PullFromTheGround", 1690, 1709, false},
    {"26_SwingTail", 1720, 1800, false},       {"27_DoubleLariat", 1810, 1855, false},
};
const ClipInfo kCoreClipTable[kCoreClips] = {
    {"01_Swim", 5, 10, true}, {"02_Swim>>Wait", 11, 19, false}, {"03_Wait", 20, 29, true},
    {"04_Wait>>Swim", 30, 39, false}, {"05_Damage", 42, 44, false}, {"06_NoMove", 20, 21, false},
};
const ClipInfo kArmClipTable[kArmClips] = {
    {"01_Clap", 5, 47, false}, {"02_RightBeat", 65, 110, false}, {"03_LeftBeat", 115, 160, false},
};
const ClipInfo kFloorClipTable[kFloorClips] = {
    {"a", 320, 640, true},   {"b", 160, 160, true},    {"c", 820, 1780, true},  {"d", 2460, 3420, true},
    {"e", 2, 160, false},    {"f", 660, 820, false},   {"g", 2300, 2460, false}, {"h", 160, 319, false},
    {"i", 1960, 2119, false}, {"j", 3580, 3720, false}, {"k", 1780, 1940, false}, {"l", 2120, 2280, false},
    {"m", 3430, 3570, false},
};

int clip_by_name(int mesh, const char* name) {
    const ClipInfo* t = nullptr; int n = 0;
    switch (mesh) {
        case kB: t = kBodyClipTable; n = kBodyClips; break;
        case kT: t = kTailClipTable; n = kTailClips; break;
        case kW: t = kCoreClipTable; n = kCoreClips; break;
        case kA: t = kArmClipTable; n = kArmClips; break;
        case kY: t = kFloorClipTable; n = kFloorClips; break;
        default: return -1;
    }
    for (int i = 0; i < n; ++i) {                                  // stricmp(sectionRec+0x44, name)
        const char *a = t[i].name, *b = name;
        while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

void advance_clip(const ClipInfo& c, float& frame, std::uint32_t& count, float rate) {
    float f = frame + rate;
    if (c.loop) {                                                  // next == -2: inc @0x5f5efa
        while (f > float(c.last) + 1.0f) { f -= float(c.last - c.first + 1); ++count; }
    } else if (f >= float(c.last)) {                               // stop: mov @0x5f5f32
        f = float(c.last); count = 1;
    }
    frame = f;
}

const float kLimbStep[3] = {0.03f, 0.025f, 0.0225f};               // 0x3cf5c28f / 0x3ccccccd / 0x3cb851ec
const float kPushOthers[3] = {-0.1f, -0.12f, -0.14f};
const float kPushToal[3] = {-0.1f, -0.15f, -0.2f};
const std::uint32_t kHomingExtra[3] = {90, 60, 0};
const std::uint32_t kHomingBase[3] = {420, 390, 360};
const std::uint32_t kBarrierHits[3] = {60, 70, 80};
const std::uint32_t kWindow[3] = {900, 720, 600};
const std::uint32_t kSwingCd[3] = {440, 390, 340};
const std::uint32_t kAttackCd[3] = {180, 140, 100};
const float kPartRadius[kBodyParts] = {1.6f, 1.2f, 4.8f, 3.8f, 2.2f, 2.5f, 2.2f, 2.5f};
const Vec3 kBitSpawn[2][6] = {
    {{15, 20, 0}, {-15, 20, 0}, {15, 20, 0}, {-15, 20, 0}, {15, 20, 0}, {-15, 20, 0}},
    {{10, 20, 0}, {-10, 20, 0}, {10, 20, 0}, {-10, 20, 0}, {10, 20, 0}, {-10, 20, 0}},
};
const Vec3 kBitTarget[2][6] = {
    {{7.5f, 1.5f, -5}, {-7.5f, 1.5f, -5}, {7.5f, 1.5f, -10}, {-7.5f, 1.5f, -10}, {7.5f, 1.5f, 0}, {-7.5f, 1.5f, 0}},
    {{5.5f, 1.5f, -4}, {-5.5f, 1.5f, -4}, {5.5f, 1.5f, -8}, {-5.5f, 1.5f, -8}, {5.5f, 1.5f, 0}, {-5.5f, 1.5f, 0}},
};
const Vec3 kVortex[kTracks] = {
    {2.2f, 0.1f, -8.85f},  {4.83f, 0.1f, -8.83f},  {3.6f, 0.1f, -7.88f},  {1.08f, 0.1f, -9.95f},
    {-2.2f, 0.1f, -8.85f}, {-4.83f, 0.1f, -8.83f}, {-3.2f, 0.1f, -8.88f}, {-1.08f, 0.1f, -9.95f},   // k 6: sic
};
const char* const kTentacleNode[kTracks] = {
    "Bone_LeftTentacle1-1",  "Bone_LeftTentacle2-1",  "Bone_LeftTentacle3-1",  "Bone_LeftTentacle4-1",
    "Bone_RightTentacle1-1", "Bone_RightTentacle2-1", "Bone_RightTentacle3-1", "Bone_RightTentacle4-1",
};
const IntroCam kIntroCams[3] = {
    {{-20.8f, -3.695f, -30.651f}, {-1.0f, 0.505f, 5.349f}, 0.349066f},
    {{39.839f, 13.305f, -33.165f}, {6.507f, 1.505f, 2.835f}, 0.314159f},
    {{0.0f, 10.905f, -30.651f}, {0.0f, 1.205f, 5.349f}, 0.296706f},
};

void track_bind(Track& t, int k, int section) {
    t.root = k; t.section = section; t.releasing = false; t.owns = true;
    const float blend = 0.05f;                                     // c+0x3c of 20_Beat / 24_Stab / 25_Pull [DATA; CE §2.2]
    t.weight = 1.0f > blend ? 1.0f : 0.0f;
    t.count = 0; t.frame = float(kTailClipTable[section].first);
}
void track_release(Track& t) { t.weight = 1.0f; t.releasing = true; }
void track_step(Track& t, float dt) {
    const ClipInfo& c = kTailClipTable[t.section];
    float w = t.weight;
    bool advance = true;
    if (w > 0.0f) {
        w -= dt * 0.05f;                                           // f32 0x6bd13c
        t.weight = w;
        if (w <= 0.0f) { t.weight = 0.0f; advance = false; }
    } else if (w == 0.0f) advance = false;
    if (!advance) {                                                // ZERO
        if (t.releasing) { t.releasing = false; t.owns = false; return; }   // the sub-tree goes back; no advance
    }
    t.frame += 0.25f * dt * t.speed;                               // c+0x38 (rate) * dt * rec+0x10
    if (c.loop) {
        while (t.frame > float(c.last) + 1.0f) { ++t.count; t.frame -= float(c.last - c.first + 1); }
    } else if (t.frame >= float(c.last)) {
        t.frame = float(c.last); t.count = 1;
    }
}

void mover_step(Vec3& pos, const Vec3& target, float radius, float& speed) {
    if (speed == 0.0f) return;
    const float dx = target.x - pos.x, dy = target.y - pos.y, dz = target.z - pos.z;
    const float L = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (L != 0.0f) { pos.x += dx / L * speed; pos.y += dy / L * speed; pos.z += dz / L * speed; }
    const float ex = target.x - pos.x, ey = target.y - pos.y, ez = target.z - pos.z;
    const float L2 = std::sqrt(ex * ex + ey * ey + ez * ez);
    if (L2 > L || radius >= L2) {
        if (radius < 0.01f) pos = target;                          // f32 0x6bd084
        speed = 0.0f;
    }
}

}  // namespace boss08

using namespace boss08;
using ef2sim::M4;

namespace {
constexpr float kPi = 3.1415927f;        // 0x6bd6ac
constexpr float kHalfPi = 1.5707964f;    // 0x6bd4e4
constexpr float k2Pi = 6.2831855f;
constexpr double k2PiD = 6.2831854820251465;
constexpr float kDeg = 0.017453292f;     // 0x6bd0dc

float fb(std::uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
const float kThird = fb(0x3eaaa64cu);    // 0x6bd298 ("0.3333", not 1/3)
float at2(float a, float b) { return float(std::atan2(double(a), double(b))); }   // FUN_0066f80a / FUN_00427410
float cosf_(float a) { return float(std::cos(double(a))); }        // FUN_0041c530
float sinf_(float a) { return float(std::sin(double(a))); }        // FUN_0041c510
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 nrm(const Vec3& a) {                                          // FUN_0042cc50: zero length -> (0, 0, 0)
    const float l = std::sqrt((a.y * a.y + a.x * a.x) + a.z * a.z);
    return l == 0.0f ? Vec3{} : mul(a, 1.0f / l);
}
// angNorm / wrapPi / angStep = FUN_005eecc0 [A §0; DOC DALLES_AB §0] (the same code as boss07.cpp's).
float ang_norm(float x) {
    if (x > k2Pi) return float(std::fmod(double(x), k2PiD));
    if (x < 0.0f) return float(std::fmod(double(x), k2PiD)) + k2Pi;
    return x;
}
float wrap_pi(float d) {
    if (0.0f > d) { if (d < -kPi) d += k2Pi; }
    else if (d > kPi) d -= k2Pi;
    return d;
}
float fmodf_(float a, float b) { return float(std::fmod(double(a), double(b))); }
float ang_step(float rate, float cur, float tgt) {
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
// The inline pattern of the shell [A §0 "step"].
void step(float& cur, float tgt, float dn, float up) {
    if (cur > tgt) { cur -= dn; if (tgt > cur) cur = tgt; }
    else if (tgt > cur) { cur += up; if (cur > tgt) cur = tgt; }
}
float shown(float a, float tgt) { return (a < 0.9f && a > 0.0f && tgt != 1.0f) ? a + 0.1f : a; }   // 0x6bd3e0 / 0x6bd19c
// (X << 16) | (Y << 8) | Z of a kind-0x25 target [B1 §5]: truncation toward zero of coord + 128.
float packed(float x, float y, float z) {
    const std::uint32_t X = std::uint8_t(int(x + 128.0f)), Y = std::uint8_t(int(y + 128.0f)), Z = std::uint8_t(int(z + 128.0f));
    return fb((X << 16) | (Y << 8) | Z);
}
// FUN_00443a80(deg) for deg not in {-1, 360}: (float)wrap180(90 - deg) * 0.017453292, no draw [B23 §5.3].
float ang443(int deg) {
    int d = 90 - deg;
    while (d > 180) d -= 360;                                      // [H] the wrap's boundary; never reached by Darm's values
    while (d < -180) d += 360;
    return float(d) * kDeg;
}
const int kWeakNode[3] = {kBerryMouthMark, kLeftLeg, kRightLeg};   // u32 0x69ed84 = 19, 5, 3
const int kStigma[6] = {9, 12, 10, 13, 11, 14};                    // u32 0x69ee40
}  // namespace

struct Boss08::Scope {
    Boss08& b;
    const Boss08Input* in; Boss08Output* out;
    Scope(Boss08& bb, const Boss08Input* i, Boss08Output& o) : b(bb), in(bb.in_), out(bb.out_) {
        b.in_ = i; b.out_ = &o;
        b.sec_[kB] = i->b_section; b.cnt_[kB] = i->b_count; b.f_[kB] = i->b_frame;
        b.sec_[kT] = i->t_section; b.cnt_[kT] = i->t_count; b.f_[kT] = i->t_frame;
        b.sec_[kW] = i->w_section; b.sec_[kY] = i->y_section; b.cnt_[kY] = i->y_count;
        b.ppos_ = i->player_pos;
    }
    ~Scope() { b.in_ = in; b.out_ = out; }
};

// FUN_00550aa0, once per object [P1 §3.3].
Boss08::Boss08(const Boss08Setup& s) {
    char_ = s.character; diff_ = std::clamp(s.diff_index, 0, 2); easy_ = s.diff_easy; rush_ = s.boss_rush;
    st_ = s.statics;
    // Item 2: FUN_00443b80(7, obj+0x94) — the loader's difficulty scaling [DOC PICTIMOS P12; D §2.4, §7], then
    // STR += 1.0, for every kind.
    static const float kHpMul[3] = {1.0f, 1.25f, 1.5f};            // f32 0x68a4f4 / f8
    float hp = s.eex_hp * kHpMul[diff_], str = s.eex_str, def = s.eex_def;
    if (s.diff_very_easy) { hp *= 0.75f; str -= 1.0f; def -= 2.0f; }
    else if (easy_) def -= 1.0f;
    str += 1.0f;                                                   // 0x550b28..0x550b49
    for (Boss08Mesh& m : mesh_) { m.max_hp = m.hp = hp; m.str = str; m.def = def; }
    Boss08Mesh &B = mesh_[kB], &T = mesh_[kT], &Wm = mesh_[kW], &A = mesh_[kA];
    // Kind 0: eight parts (item 5: the stat copy, HP x 10, +0xfc = 1.0), the block defaults (item 6), Touch_Tail.
    B.pos = s.pos; B.touch = true;
    for (int i = 0; i < kBodyParts; ++i) { parts_[std::size_t(i)].radius = kPartRadius[i]; parts_[std::size_t(i)].def = def; parts_[std::size_t(i)].pool = hp * 10.0f; }
    // Kind 1: DEF 999, maxHP = HP = 0; created at (B.x, B.y - 999, B.z), then glued to Bone_Skirt1 every frame.
    T.def = 999.0f; T.max_hp = T.hp = 0.0f; T.pos = {B.pos.x, B.pos.y - 999.0f, B.pos.z};
    // Kind 2: DEF 999, pos (10, 2, -10), one part r 1.6 (obj+0x298 |= 0x8000: [not modelled], D U-D3).
    Wm.def = 999.0f; Wm.pos = {10.0f, 2.0f, -10.0f};
    parts_[kBodyParts].radius = 1.6f; parts_[kBodyParts].def = def; parts_[kBodyParts].pool = hp * 10.0f;
    // Kind 3: obj+0x298 |= 0x40.
    A.no_hit = true; A.pos = {B.pos.x, B.pos.y - 999.0f, B.pos.z};
    // Item 15: the retry softening on each object's own STR / DEF (skipped in boss rush), then >= 1. The parts
    // copied the stats before this.
    if (!rush_) {
        const int n = s.retries;
        float k = 0.0f;
        if (s.diff_very_easy) k = float(std::pow(double(n), 1.25));
        else if (easy_) k = float(n < 20 ? n >> 1 : n - 10);
        if (s.diff_very_easy || easy_)
            for (Boss08Mesh& m : mesh_) { m.str = std::max(1.0f, m.str - k); m.def = std::max(1.0f, m.def - k); }
    }
    mat_alpha_.fill(1.0f);
    sec_[kW] = 2;
    // The host's at creation: the family's first motions (B section 0, T "01_NormalWait", W "03_Wait"),
    // MatMode(B, 0), CamIdx = 1 @ 0.025, the grey player tint (item 11), flag227 = 0, flag215 = 1, the region
    // cameras 7 / 8 / 9 (boss08::kIntroCams), the SE preload, the emblem word (kEmblem).
}

M4 Boss08::matrix() const {
    M4 m = ef2sim::rot_ypr(bb_.yaw, bb_.pitch, bb_.roll);
    m.m[3][0] = mesh_[kB].pos.x; m.m[3][1] = mesh_[kB].pos.y; m.m[3][2] = mesh_[kB].pos.z;
    return m;
}

std::uint32_t Boss08::W() { return in_ && in_->rng ? in_->rng() : 0u; }
float Boss08::U() { return fb((W() & 0x7fffffu) | 0x3f800000u) - 1.0f; }
float Boss08::Rf(float lo, float hi) { return lo + (hi - lo) * U(); }      // FUN_00429e40
float Boss08::pv() const { return char_ == 3 ? kPushToal[diff_] : kPushOthers[diff_]; }

void Boss08::set_clip(int mesh, int section) {                     // FUN_005f5c80(M, section, 1; xmm3 0)
    out_->clips.push_back({mesh, section});
    sec_[mesh] = section; cnt_[mesh] = 0;
    const ClipInfo* t = mesh == kB ? kBodyClipTable : mesh == kT ? kTailClipTable : kCoreClipTable;
    f_[mesh] = float(t[section].first);                            // [H]
}
void Boss08::queue_motion(int mesh, const char* name, bool flag) { // strncpy(+0x424); +0x444 = flag; An+0x1c = 0
    out_->motions.push_back({mesh, name, flag});
    cnt_[mesh] = 0;
}
void Boss08::move_to(int mesh, const Vec3& p, float v1, float v2) {    // FUN_00503100, rel 0
    Boss08Mesh& m = mesh_[std::size_t(mesh)];
    m.move_target = p; m.move_speed = std::fabs(v2); m.move_radius = v1 > 0.0f ? v1 : std::fabs(v2);
}
void Boss08::touch(int mesh, bool on) { mesh_[std::size_t(mesh)].touch = on; }
void Boss08::kill(int& h) { if (h != 0) { out_->kill_handles.push_back(h); h = 0; } }
Boss08Effect& Boss08::fx(int id, Boss08Call call) {                // "plain": pos 0, rot 0, scale 1, power 0, knock 1
    Boss08Effect e; e.call = call; e.id = id;
    out_->effects.push_back(e);
    return out_->effects.back();
}
// InlineFX [CE §0]: refused when the particle pool is full.
Boss08Effect* Boss08::inline_fx(int id, Boss08Stat stat) {
    if (in_->fx_pool_full) return nullptr;
    Boss08Effect& e = fx(id, Boss08Call::kInline);
    e.stat = stat; e.or_1dc = 0x100;
    return &e;
}
Boss08Effect& Boss08::on_body(Boss08Effect& e, const Vec3& off) {  // Attach(fx, B+0x20, off, 0, 1, 1); +0x201 = 1
    e.parent = Boss08Parent::kBody; e.at_off = off; e.at_mode = 1; e.flag201 = true;
    return e;
}
Boss08Effect& Boss08::on_node(Boss08Effect& e, int n) {
    e.parent = Boss08Parent::kBodyNode; e.parent_index = n; e.at_mode = 1; e.flag201 = true;
    return e;
}
void Boss08::bullet(bool raw, int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f, bool f201) {
    Boss08Bullet b; b.raw = raw; b.kind = kind; b.effect = effect; b.pos = pos; b.dir = dir;
    for (int i = 0; i < 13; ++i) b.f[i] = f[i];
    b.flag201 = f201;
    out_->bullets.push_back(b);
}
void Boss08::fade(std::uint32_t col, float st, float target) {
    Boss08Fader f; f.colour = col; f.current = 1.0f; f.step = st; f.target = target;
    out_->fader.push_back(f);
}

Boss08Hit Boss08::hit_part(int mesh, int i, int dmg) {             // FUN_004faf20 case 7 [D §4.3]
    Boss08Part& p = parts_[std::size_t(mesh == kW ? kBodyParts : i)];
    if (1.0f > p.pool) return Boss08Hit::kRefused;
    p.dmg += float(dmg);                                           // also when dmg == 0
    if (dmg > 0) {
        p.pool -= float(dmg);                                      // [not modelled] dmg' (GELALDY G9)
        if (1.0f > p.pool) p.pool = 0.0f;
        return Boss08Hit::kLanded;
    }
    return Boss08Hit::kZero;                                       // type 7: no sound on a 0-damage hit
}

// ---------------------------------------------------------------------------
// FUN_00562990 case 12, for the four objects in creation order [H A G2].
Boss08Output Boss08::tick(const Boss08Input& in) {
    Boss08Output out;
    Scope sc(*this, &in, out);
    Boss08Mesh &B = mesh_[kB], &T = mesh_[kT], &Wm = mesh_[kW], &A = mesh_[kA];

    // --- B: FUN_0055ddc0 kind 0 [A §A.1.1]
    motion();
    pushout();
    bank();
    chain();
    homing();
    death();
    if (bb_.form_req != bb_.form) form_change(bb_.form_req);
    shell_body();
    fall();
    mat_flags();
    if (in.flag156) {                                              // COMMON [A §A.1.6]
        damage_body();
        dispatcher();
        ++bb_.fc;
        float f = B.flash;
        if (f != 0.0f) { f -= 0.075f; B.flash = f; if (f < 0.0f) B.flash = 0.0f; }
    }
    mover_step(B.pos, B.move_target, B.move_radius, B.move_speed);

    // --- T: kind 1. (T+0x20..0x5f = the world matrix of B's Bone_Skirt1: the host's glue.)
    tail_chain();
    tail_fx();
    if (in.flag156) {
        tail_decide();                                             // (FUN_0055aa80(T): no parts)
        ++tb_.fc;
        T.flash = B.flash;
    }
    for (Track& t : trk_) track_step(t);                           // the tail model's tick FUN_005f6000 [H CE K4]

    // --- W: kind 2
    core_shell();
    if (!in.flag156) {
        if (!bb_.dead) Wm.pos.y += 3.0f;                           // the core drifts up out of sight
    } else {
        damage_core();
        Wm.alpha = wb_.alpha;
        ++wb_.fc;
        float f = Wm.flash;
        if (f != 0.0f) { f -= 0.075f; Wm.flash = f; if (f < 0.0f) Wm.flash = 0.0f; }
        if (Wm.bar_hp != Wm.hp) { Wm.bar_hp = Wm.hp; out.hp_bar = Boss08HpBar{Wm.hp, Wm.max_hp}; }
    }
    mover_step(Wm.pos, Wm.move_target, Wm.move_radius, Wm.move_speed);

    // --- A: kind 3
    arms_shell();
    if (in.flag156) {
        ++ab_.fc;
        float f = A.flash;
        if (f != 0.0f) { f -= 0.075f; A.flash = f; if (f < 0.0f) A.flash = 0.0f; }
    }
    // [not modelled] byte 0x765553 (OtherLightWeightMode mirror, A C17); the engine's +0x418 -> +0x41c stepper.
    return out;
}

// FUN_00552120 [A §A.2].
void Boss08::motion() {
    Boss08BodyBlock& b = bb_;
    if (!in_->flag156) return;
    if (1.0f > mesh_[kW].hp) return;
    Vec3& pos = mesh_[kB].pos;
    float dx = pos.x - ppos_.x, dz = pos.z - ppos_.z;
    if (sec_[kB] == 10 || b.form == kNormal || b.form == kLow || b.form == kOnBack) {
        dx = pos.x - 0.0f; dz = pos.z - (-100.0f);                 // f32 0x6be138
    }
    const float tgt = kHalfPi - at2(dz, dx);
    float r;
    if (b.turn_over_t != 0) { r = b.turn_over; --b.turn_over_t; }
    else if (b.turn_base_t != 0) { r = b.turn_base; --b.turn_base_t; }
    else { r = 1.0f; b.turn_base = 1.0f; }
    b.yaw = ang_step(r * 0.0035f, b.yaw, tgt);                     // 0x6bd028 = 0x3b656042
    float m;
    if (b.move_over_t != 0) { m = b.move_over; --b.move_over_t; }
    else if (b.move_base_t != 0) { m = b.move_base; --b.move_base_t; }
    else { m = 1.0f; b.move_base = 1.0f; }
    const float va = m * 0.04f, vb = m * 0.05f;                    // 0x6bd12c / 0x6bd13c
    const float d = std::sqrt((dx * dx + 0.0f) + dz * dz);
    float nx = dx, nz = dz;
    if (d > 1.1920929e-07f) { const float inv = 1.0f / d; nx *= inv; nz *= inv; }
    float x = pos.x, z = pos.z;
    if (d < 7.5f) { x = pos.x + nx * va; z = pos.z + nz * va; }    // too close: back away
    else if (d > 7.75f) { x = pos.x - nx * vb; z = pos.z - nz * vb; }
    float t = b.y_target, c = b.y;                                 // the dead y lerp
    if (t > c) { c += b.y_step; if (c >= t) c = t; }
    else if (c > t) { c -= b.y_step; if (t >= c) c = t; }
    b.y = c;
    const float y = c;
    const float L = std::sqrt((y * y + x * x) + z * z);
    Vec3 o{x, y, z};
    if (L > 21.0f) o = mul(o, 21.0f / L);                          // 0x6bd9a0
    pos = o;                                                       // B+0x20..0x5f = YawPitchRoll(yaw, 0, 0) + o
}

// FUN_00552510 [A §A.3] — not gated.
void Boss08::pushout() {
    Boss08BodyBlock& b = bb_;
    const Vec3 c = ef2sim::transform_coord({0.0f, 0.0f, 1.5f}, matrix());
    const float dx = ppos_.x - c.x, dz = ppos_.z - c.z;
    const float d = std::sqrt((dx * dx + 0.0f) + dz * dz);
    Vec3 n{dx, 0.0f, dz};
    if (d != 0.0f) n = mul(n, 1.0f / d);
    const float r = b.down ? 2.0f : 4.0f;
    if (r > d) {
        const float s = std::max(0.0f, (r - d) * 0.2f);
        if (b.settled) { out_->player.push.x += n.x * s; out_->player.push.z += n.z * s; }
    }
    if (b.form == kHigh) {                                         // the slot-3 camera block
        b.cam_fov = 0.523599f; b.cam_off.y = 5.55f;                // 0x3f060a92 / 0x40b1999a
        b.cam_eye = {ppos_.x + n.x * 46.0f, ppos_.y + n.y * 46.0f + 25.0f, ppos_.z + n.z * 46.0f};
        b.cam_target = {ppos_.x - n.x, ppos_.y - n.y + 1.0f, ppos_.z - n.z};
    }
}

// FUN_00552ef0 [P1 §6; B1; B23]: no early exit, every block every frame, in the code's order.
void Boss08::bank() {
    bank_hands();
    bank_spear();
    bank_onback();
    bank_arms();
    bank_smoke();
    // Clip 35 36_DoubleLariat [B23 §8].
    if (sec_[kB] == 35) {
        Boss08Mesh& B = mesh_[kB];
        if (feq(1361.0f)) move_to(kB, {B.pos.x, B.pos.y, 0.0f}, 0.4f, 0.4f);
        if (feq(1368.0f)) {
            Boss08Effect& e = fx(512); e.pos = B.pos; e.flag201 = true;       // not attached
            zoom(0.1f, -0.001f);
            const Vec3 d = nrm({ppos_.x - B.pos.x, 0.0f, ppos_.z - B.pos.z});
            push(mul(d, -pv()), 30);                               // radial, away from B
            bits(diff_bits(4, 6, 6));
            se(335);
        }
        if (feq(1382.0f)) move_to(kB, {0.0f, B.pos.y, 8.5f}, 0.1f, 0.1f);
    }
}

// Clips 1..6 [B1 §2..§4].
void Boss08::bank_hands() {
    Boss08BodyBlock& b = bb_;
    Boss08Mesh& B = mesh_[kB];
    const int sec = sec_[kB];
    const float f = f_[kB];
    const bool nm = diff_ == 2;
    // The swings: clip 1 (left) / 3 (right) [B1 §2].
    struct Swing { int clip; float f0, f1, f2, f3; int trail; int n[3]; };
    static const Swing kSwing[2] = {
        {1, 61.0f, 72.0f, 76.0f, 80.0f, 450, {kLeftToe31, kLeftFoot, kLeftLeg}},
        {3, 171.0f, 182.0f, 186.0f, 190.0f, 451, {kRightToe31, kRightFoot, kRightLeg}},
    };
    // The hand raises: clip 2 (left) / 4 (right) [B1 §3].
    struct Raise { int clip; float lo, hi; int node; float off_x; };
    static const Raise kRaise[2] = {{2, 125.0f, 130.0f, kLeftToe23Mark, 3.0f}, {4, 235.0f, 240.0f, kRightToe23Mark, -3.0f}};
    for (int side = 0; side < 2; ++side) {
        const Swing& s = kSwing[side];
        if (sec == s.clip) {
            if (f == s.f0) {                                       // LUNGE
                const Vec3 tgt{(ppos_.x - B.pos.x) * 0.9f + B.pos.x, (B.pos.y - B.pos.y) * 0.9f + B.pos.y,
                               ((ppos_.z + 8.5f) - B.pos.z) * 0.9f + B.pos.z};
                move_to(kB, tgt, 0.2f, 0.2f);
            }
            if (f == s.f1) {                                       // SWING
                on_body(fx(s.trail));
                se(332);
                for (int k : s.n) { Boss08Effect& h = on_node(fx(490), k); h.knock = 10.0f; h.rtype = 2; }
            }
            if (f == s.f2) { shake(30, 0.1f, 0.1f); zoom(0.1f, -0.001f); push({0.0f, 0.0f, pv()}, 30); }   // IMPACT
            if (f == s.f3) move_to(kB, {0.0f, B.pos.y, 8.5f}, 0.05f, 0.05f);                              // RETURN
            if (nm && f == s.f3) bits(4);
        }
        const Raise& r = kRaise[side];
        if (sec == r.clip) {
            if (f > r.lo && r.hi > f && b.fc % 6 == 0) {
                const int n = nm ? 32 : 16;
                for (int i = 0; i < n; ++i) {
                    Vec3 p = node(r.node); p.y = 1.5f;
                    const float dx = ppos_.x - p.x, dz = ppos_.z - p.z;
                    const float u1 = U();
                    const float a = at2(dz, dx) + ((u1 * 360.0f) - 180.0f) * kDeg;
                    const Vec3 dir = nrm({cosf_(a), 0.0f, sinf_(a)});
                    const float u2 = U();                          // drawn before the allocation
                    const float acc = nm ? 0.0035f - u2 * fb(0x3b03126fu) : 0.003f - u2 * fb(0x3b03126eu);
                    const float bf[13] = {0.0f, acc, 0.0f, -10.0f, 1.0f, 0.0f, 0.0f, fb(2), 0, 0, 0, 0, 0};
                    bullet(true, 7, 519, p, dir, bf);
                }
            }
            if (f == r.lo) {
                se(356);
                on_body(fx(459), {r.off_x, 0.0f, -6.5f});
                shake(12, 0.25f, 0.25f);
                flash();
            }
        }
    }
    // Clip 6 07_BellyMouthBreath_Loop: no frame test [B1 §4.1..§4.3].
    if (sec == 6) {
        const int v = b.breath;
        if (v == 0x63) {                                           // the cannon's pull toward x = 0
            if (!(in_->player_hp < 1.0f)) {
                Vec3 n = nrm({ppos_.x - 0.0f, 0.0f, 0.0f});
                n = mul(n, 15.0f);
                const float k = float(diff_) * 0.0011f + 0.005f;   // 0x6bcff4 / 0x6bd044
                if (char_ >= 0 && char_ <= 2) { out_->player.knock2.x -= (k * n.x) * kThird; out_->player.knock2.z -= (k * n.z) * kThird; }
                else if (char_ == 3) { out_->player.knock2.x -= k * n.x; out_->player.knock2.z -= k * n.z; }
            }
        } else if ((v == 0 && (b.fc & 3) == 0) || (v == 1 && (b.fc & 1) == 0)) {
            Vec3 p = node(kBerryMouthMark); p.y -= 1.5f;
            const Vec3 base = nrm({0.0f, 0.0f, -1.0f});            // world -z, not the body's facing
            int n = v == 0 ? 6 : 3, eff = 518;
            float acc = U() * fb(0x3a03126cu) - fb(0x3b1374bcu);   // 0.0005 / 0.00225
            if (nm) {
                n *= 2;
                acc = U() * 0.0035f - fb(0x3b8b4396u);             // 0.00425
                eff = R(3) == 0 ? 515 : 518;
            }
            const float span = v == 0 ? 230.0f : 60.0f, half = v == 0 ? 115.0f : 30.0f;
            for (int i = 0; i < n; ++i) {
                const float u = U();
                const float a = at2(base.z, base.x) + ((u * span) - half) * kDeg;
                const Vec3 dir = nrm({cosf_(a), 0.0f, sinf_(a)});
                const float spd = U() * fb(0x3d4cccccu) + fb(0x3e6b851fu);   // 0.05 / 0.23
                const float bf[13] = {spd, acc, 0.0f, -5.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
                bullet(true, 2, eff, p, dir, bf);
            }
        }
    }
    // Clip 5 06_BellyMouthBreath_Start [B1 §4.4].
    if (sec == 5) {
        if (b.breath == 0x63) {
            if (f == 292.0f) {
                Boss08Effect& g = on_node(fx(460), kBerryMouthMark);
                g.handle = b.h_beam = next_handle_++;
                se(331);
            }
            if (f == 296.0f) { zoom(0.5f, -0.001f); out_->rumble.push_back(5); }
            if (f == 302.0f) shake(300, 0.3f, 0.3f);
        }
        const int v = b.breath;
        if (v == 1 && f == 302.0f) bits(6);
        if ((v == 0 || v == 1) && f == 302.0f) se(312);
    }
    if (sec != 5 && sec != 6 && sec != 7) kill(b.h_beam);          // soft-kill the beam outside the breath clips
}

// Clips 11..16, 10, 20..25 [B23 §3, §4].
void Boss08::bank_spear() {
    Boss08BodyBlock& b = bb_;
    Boss08Mesh& B = mesh_[kB];
    const int sec = sec_[kB];
    const float f = f_[kB];
    auto hit = [&]() { shake(36, 0.1f, 0.1f); zoom(0.1f, -0.001f); push({0.0f, 0.0f, pv()}, 30); bits(diff_bits(2, 4, 6)); };
    if (sec == 11) {
        if (f == 476.0f) { on_body(fx(452)); se(342); }
        if (f == 480.0f) hit();
    }
    if (sec == 12) {
        if (f == 513.0f) { on_body(fx(453)); se(342); }
        if (f == 517.0f) hit();
    }
    if (sec == 13) {
        if (f == 554.0f) { on_body(fx(458)); se(341); }
        if (f == 557.0f) shake(130, 0.25f, 0.25f);
    }
    if (sec == 14) {
        if (f == 606.0f) se(344);
        if (f == 616.0f) {
            kill(st_.h_darkwave);
            Boss08Effect& e = on_body(fx(486), {0.0f, 0.25f, -11.0f});
            e.handle = st_.h_darkwave = next_handle_++;
            shake(480, 0.125f, 0.125f); out_->rumble.push_back(4);
        }
    } else if (sec != 15) kill(st_.h_darkwave);
    if (sec == 16 && f == 683.0f) { se(364); out_->rumble.push_back(0); }
    if (sec == 10 && f == 443.0f) {                                // the landing [B23 §3.4]
        se(343); shake(36, 0.3f, 0.3f);
        Boss08Effect& e = fx(516); e.pos = B.pos; e.power = -8.0f; // not attached, no +0x201
        zoom(0.1f, -0.001f);
        const Vec3 d = nrm({ppos_.x - B.pos.x, 0.0f, ppos_.z - B.pos.z});
        push(mul(d, -pv()), 30);
        out_->rumble.push_back(3); bits(6);
    }
    if (sec == 20) {
        if (f == 826.0f) {
            kill(st_.h_rolling);
            Boss08Effect& e = on_body(fx(478));
            e.handle = st_.h_rolling = next_handle_++;
            bits(6); se(347);
        }
    } else if (sec == 21) {                                        // the spear rain and the suction [B23 §4.2]
        if ((b.fc & 1) == 0) {
            const int n = diff_ == 2 ? 3 : 1;
            for (int i = 0; i < n; ++i) {
                Vec3 dir{}, pos = B.pos;
                const float a = Rf(-kPi, kPi) + 0.0f;
                dir.x = cosf_(a);
                dir.y = Rf(-2.5f, -1.0f);
                dir.z = sinf_(a);
                dir = nrm(dir);
                pos.x += dir.x * 9.0f; pos.z += dir.z * 9.0f; pos.y += 18.0f;
                const float g = Rf(0.00125f, 0.0025f);
                const float bf[13] = {fb(0x3d48b439u), 0.01f, g, 0.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};   // speed 0.049
                bullet(false, 2, 479, pos, dir, bf);
            }
        }
        Vec3 d = mul(nrm({ppos_.x - B.pos.x, 0.0f, ppos_.z - B.pos.z}), -0.125f);
        if (char_ != 3) d = mul(d, kThird);
        push(d, 1);
    } else kill(st_.h_rolling);
    if (sec == 23) {                                               // 24_MachineGunStab [B23 §4.4]
        if (f == 877.0f) { shake(100, 0.125f, 0.125f); on_node(fx(456), kSpearMark); }
        if (877.0f <= f && f <= 893.0f) {
            if (b.fc % 18 == 0) on_node(fx(456), kSpearMark);
            bool fire = false;
            for (int k = 0; k < 9; ++k) fire = fire || f == 877.0f + 2.0f * float(k);
            if (fire) {
                se(348);
                const Vec3 pos = node(kSpearMark);
                const Vec3 dir = nrm({pos.x - node(kSpear).x, 0.0f, pos.z - node(kSpear).z});
                const float bf[13] = {fb(0x3dc8b439u), 0.02f, 0.01f, 20.0f, 1.0f, 0, 0, 0, 0.0f, 1.0f, 0, 0, 0};   // speed 0.098
                bullet(false, 2, 457, pos, dir, bf);
            }
        }
    }
    if (sec == 24) {                                               // 25_QuakeTheWing_Start [B23 §4.5]
        if (f == 925.0f) {
            Boss08Effect& e = fx(489); e.pos = {0.0f, 0.1f, 0.0f}; // world-placed, no +0x201
            b.quake_n = 0;
            if (b.mgcra) { out_->mgcra_delete = true; b.mgcra = false; }
            shake(660, 0.1f, 0.125f); out_->rumble.push_back(4); se(357);
        }
    } else if (sec == 25) {                                        // the sixth mesh [B23 §4.6]
        if (cnt_[kB] == 2) {
            ++b.quake_n;
            if (b.quake_n > 18 && !b.mgcra) { b.mgcra = true; out_->mgcra_spawn = true; }
        }
    } else {
        if (b.mgcra) { out_->mgcra_delete = true; b.mgcra = false; }
        b.quake_n = 0;
    }
}

// Clips 30..34 [B23 §5].
void Boss08::bank_onback() {
    Boss08BodyBlock& b = bb_;
    const int sec = sec_[kB];
    const float f = f_[kB];
    if (sec == 30 && f == 1079.0f) { on_body(fx(461)); flash(); se(362); }
    if (sec == 32 && f == 1194.0f) { on_body(fx(462)); flash(); se(362); }
    for (int side = 0; side < 2; ++side) {                         // raikyu x5 [B23 §5.2]
        if (!(side == 0 ? sec == 31 && f == 1131.0f : sec == 33 && f == 1246.0f)) continue;
        const Vec3 pos = node(side == 0 ? kLeftToe23Mark : kRightToe23Mark);
        Vec3 dir = nrm(ppos_ - pos);
        const Vec3 t = ppos_;
        const int r = int((W() >> 17) % 5);
        const float pk = packed(t.x, float(r) + t.y, t.z);
        for (int i = 0; i < 5; ++i) {                              // dir is not reset between iterations
            if (i == 0) { dir.x -= 5.0f; dir.y += 5.0f; }
            if (i == 1) { dir.x -= 5.0f; dir.y -= 5.0f; }
            if (i == 2) dir.z -= 1.0f;
            if (i == 3) { dir.x += 5.0f; dir.y += 5.0f; }
            if (i == 4) { dir.x += 5.0f; dir.y -= 5.0f; }
            dir = nrm(dir);
            const float bf[13] = {0.6f, fb(0xb8d1b717u), 0.0f, -5.0f, 1.0f, fb(488), fb(3), fb(2),
                                  fb(0x3e97e9d8u), 1.0f, 2.0f, 60.0f, pk};
            bullet(false, 0x25, 455, pos, dir, bf);
        }
        se(361);
    }
    if (sec == 34) {                                               // 35_DoubleHandThunder [B23 §5.3]
        if (f == 1316.0f) { on_body(fx(463)); flash(); bits(6); se(360); }
        if (1319.0f <= f && f <= 1331.0f && b.fc % 10 == 0) {
            for (int i = 0; i < 6; ++i) {
                Vec3 pos = in_->w_ora; pos.z -= 5.0f; pos.y = 1.75f;
                const int deg = int(float(i) * 20.0f - 50.0f);
                float a = ang443(deg) - kHalfPi;
                a = ang443(180) + a;
                const Vec3 dir = nrm({cosf_(a), 0.0f, sinf_(a)});
                pos.x += dir.x * 2.0f; pos.y += dir.y * 2.0f; pos.z += dir.z * 2.0f;
                const float bf[13] = {0.1f, 0.001f, 0.0f, 0.0f, 1.0f, 0, 0, fb(20), fb(0x3c8efa35u), 6.0f, 0.9f, 0, 0};
                bullet(false, 6, 519, pos, dir, bf);
            }
        }
    }
}

// The detached arms: three timers, LeftBeat, RightBeat, Clap [B23 §6].
void Boss08::bank_arms() {
    Boss08BodyBlock& b = bb_;
    Boss08Mesh &B = mesh_[kB], &A = mesh_[kA];
    auto yaw_to_p = [&]() { return -kHalfPi - at2(ppos_.z - B.pos.z, ppos_.x - B.pos.x); };
    auto place = [&](float yaw) { A.yaw = yaw; A.pos = ppos_; A.pos.y = 0.0f; };   // RotationYawPitchRoll(&A+0x20, yaw, 0, 0)
    auto beat = [&](std::uint32_t& t, float& hand_t, const char* name) {
        if (t == 0) return;
        if (t == 1) {
            se(333); hand_t = 0.0f;
            Boss08Effect& e = fx(466); e.pos = {ppos_.x, 0.1f, ppos_.z}; e.rot = {0.0f, yaw_to_p(), 0.0f};   // both beats: 466
        }
        if (t == 20) { place(yaw_to_p()); ab_.alpha_t = 1.0f; queue_motion(kA, name); }
        if (t == 40) A.no_hit = false;
        if (t == 110) { shake(30, 0.25f, 0.25f); out_->rumble.push_back(1); }
        if (t == 200) { ab_.alpha_t = 0.0f; A.no_hit = true; }
        if (t > 240) { t = 0; hand_t = 1.0f; } else ++t;
    };
    beat(b.beat_l, b.hand_l_t, "03_LeftBeat");
    beat(b.beat_r, b.hand_r_t, "02_RightBeat");
    std::uint32_t& t = b.clap;
    if (t != 0) {
        if (t == 1) {
            se(334); b.hand_l_t = 0.0f; b.hand_r_t = 0.0f;
            const float y = yaw_to_p();
            place(y);
            Boss08Effect& e = fx(468); e.pos = A.pos; e.rot = {0.0f, y, 0.0f}; e.flag201 = true;
            place(yaw_to_p());                                     // db+0x110 == 0, always [B23 P14]
            ab_.alpha_t = 1.0f; queue_motion(kA, "01_Clap");
            A.no_hit = false;
        }
        if (t == 60 && diff_ == 2) {                               // Nightmare: one homing shot
            const int r = int((W() >> 17) % 5);
            const float bf[13] = {1.0f, fb(0xba449ba6u), 0.0f, -6.0f, 1.0f, fb(483), fb(2), fb(2),
                                  fb(0x3ed67750u), 1.0f, 2.0f, 60.0f, packed(ppos_.x, float(r) + ppos_.y, ppos_.z)};
            bullet(false, 0x25, 484, A.pos, {0.0f, 1.0f, 0.0f}, bf);
        }
        if (t == 80) { shake(30, 0.3f, 0.3f); out_->rumble.push_back(3); }
        if (t == 140) { ab_.alpha_t = 0.0f; A.no_hit = true; }
        if (t > 200) { t = 0; b.hand_l_t = 1.0f; b.hand_r_t = 1.0f; } else ++t;
    }
}

// The limb smoke [B23 §7]: 11 draws per puff here; FUN_00593b90 draws 3 more for effect 0x1e7 (the host's).
void Boss08::bank_smoke() {
    static const int kN0[3] = {kSkirt1, kLeftFoot, kRightFoot};    // u32 0x69ee28 + 8g
    static const int kN1[3] = {kSkirt1, kLeftLeg, kRightLeg};      // u32 0x69ee2c + 8g
    for (int g = 0; g < 3; ++g) {
        const std::uint32_t c = bb_.smoke[g];
        if (c == 0) continue;
        for (int k = 0; k < 2; ++k) {
            Vec3 pos = node(kN0[g]);
            if (g == 0) pos.y -= 1.5f;
            if (k == 1) {
                if (g == 0) pos.y -= 1.5f;
                else {
                    const Vec3 o = node(kN1[g]);
                    pos.x += (o.x - pos.x) * 0.5f; pos.y += (o.y - pos.y) * 0.5f; pos.z += (o.z - pos.z) * 0.5f;
                }
            }
            pos.x += Rf(-1.0f, 1.0f); pos.y += Rf(-1.0f, 1.0f); pos.z += Rf(-1.0f, 1.0f);
            W(); W(); W();                                         // three raw draws, discarded
            Vec3 d;
            d.x = Rf(-1.0f, 1.0f); d.y = Rf(-1.0f, 1.0f); d.z = Rf(-1.0f, 1.0f);
            d = nrm(d);
            const float acc = Rf(0.001f, 0.004f);
            const float spd = Rf(0.02f, 0.025f);
            const float bf[13] = {spd, acc, 0.0f, 0.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
            bullet(false, 2, 487, pos, d, bf, false);              // result unused: no +0x201
        }
        bb_.smoke[g] = c > 90 ? 0 : c + 1;
    }
}

// FUN_00552730 [B1 §5].
void Boss08::bits(int n) {
    Boss08BodyBlock& b = bb_;
    if (in_->bits_deployed > 6) return;                            // no SE, the layout toggle unchanged
    se2(336);
    if (char_ != 3 && n >= 4) n -= 2;                              // only Toal gets the full count
    const int lay = b.bit_layout == 1 ? 1 : 0;
    for (int i = 0; i < n; ++i) {
        const Vec3 pos = kBitSpawn[lay][i], tgt = kBitTarget[lay][i];
        float z = tgt.z;
        if (b.form == kLow) z -= 12.0f; else if (b.form == kOnBack) z -= 3.0f;
        const int r = int(R(5));
        const float bf[13] = {0.3f, fb(0xba83126fu), 0.0f, -6.0f, 1.0f, fb(487), fb(3), fb(4),
                              fb(0x3e97e9d8u), 1.0f, 2.0f, 60.0f, packed(tgt.x, float(r) + tgt.y, z)};
        bullet(false, 0x25, 467, pos, {0.0f, -1.0f, 0.0f}, bf);
    }
    b.bit_layout = b.bit_layout + 1 == 2 ? 0 : b.bit_layout + 1;
}

// FUN_00552c30 [B1 §7]: the low-HP homing volley.
void Boss08::homing() {
    Boss08BodyBlock& b = bb_;
    if (!b.homing_req) return;
    b.homing_req = false;
    se2(350);
    static const int kPair[3] = {0, 2, 4};
    const int s = kPair[R(3)];
    for (int k = s; k < s + 2; ++k) {
        const Vec3 pos = node(kStigma[k]);
        const float sg = (k % 2 == 1) ? -1.0f : 1.0f;
        const Vec3 dir{sg * 0.5f, 0.5f, 0.0f};                     // not normalised
        const int r = int(R(5));
        const float bf[13] = {1.2f, fb(0xba83126fu), 0.0f, -6.0f, 1.0f, fb(483), fb(2), fb(2),
                              fb(0x3e97e9d8u), 1.0f, 2.0f, 60.0f, packed(ppos_.x, float(r) + ppos_.y, ppos_.z)};
        bullet(false, 0x25, 484, pos, dir, bf);
    }
}

// FUN_00559a50 [A §A.4].
void Boss08::chain() {
    Boss08BodyBlock& b = bb_;
    const int s = sec_[kB];
    const std::uint32_t c = cnt_[kB];
    auto wait = [&](int clip) { if (c == 1) { set_clip(kB, clip); b.thunder_r_t = 0.0f; b.thunder_l_t = 0.0f; } };
    switch (b.form) {
        case kNormal:
            if (s == 5) { if (c == 1) set_clip(kB, 6); }
            else if (s == 6) {
                const std::uint32_t need = b.breath == 0 ? 2 : b.breath == 1 ? 1 : b.breath == 0x63 ? 8 : 1;
                if (c == need) set_clip(kB, 7);
            } else if (s == 36 || s == 0) {}
            else wait(0);
            break;
        case kLow:
            if (s == 14) { if (c == 1) set_clip(kB, 15); }
            else if (s == 15) { if (c == 30) set_clip(kB, 16); }
            else if (s == 37 || s == 9) {}
            else wait(9);
            break;
        case kHigh:
            if (s == 20) { if (c == 1) set_clip(kB, 21); }
            else if (s == 21) { if (c == 4) set_clip(kB, 22); }
            else if (s == 24) { if (c == 1) set_clip(kB, 25); }
            else if (s == 25) { if (c == 20) set_clip(kB, 26); }
            else if (s == 23) { if (c == 1) set_clip(kB, 19); }    // no thunder-target reset on this path
            else if (s == 38 || s == 19) {}
            else wait(19);
            break;
        case kOnBack:
            if (s != 29) wait(29);
            break;
        default: break;
    }
    b.settled = false;
    static const int kLoop[4] = {0, 2, 1, 3};
    static const char* const kName[4] = {"a", "c", "b", "d"};
    if (b.form < 0 || b.form > 3) return;
    if (sec_[kY] == kLoop[b.form]) b.settled = true;
    else if (cnt_[kY] == 1) queue_motion(kY, kName[b.form]);
}

// FUN_0055a150 [A §A.5].
void Boss08::form_change(int nf) {
    Boss08BodyBlock& b = bb_;
    Boss08Mesh& B = mesh_[kB];
    if (1.0f > mesh_[kW].hp) return;                               // the request stays pending
    se(345);
    const int old = b.form;
    float z = 0.0f;
    auto weak = [&]() { const std::uint32_t r = R(3); wb_.weak = r == 0 ? 1 : r == 1 ? 2 : 0; };
    switch (nf) {
        case kNormal:
            if (old == 1) { set_clip(kB, 17); queue_motion(kY, "k"); queue_motion(kT, "05_Low>>Normal"); }
            else if (old == 2) { set_clip(kB, 27); queue_motion(kY, "h"); queue_motion(kT, "17_High>>Normal"); }
            else if (old == 3) queue_motion(kY, "a");
            tb_.form = 0; b.form = 0; b.y_target = 0.0f; b.eye = 1.0f; wb_.alpha_t = 0.0f;
            weak();
            z = 8.5f;
            break;
        case kLow:
            if (old == 0) { b.low_latch = true; queue_motion(kY, "f"); queue_motion(kT, "02_Normal>>Low"); }
            else if (old == 2) { b.low_latch = true; queue_motion(kY, "i"); queue_motion(kT, "17_High>>Normal"); }
            else if (old == 3) queue_motion(kY, "c");
            tb_.form = 1; b.form = 1; set_clip(kB, 8);             // whatever `old` is
            b.y_target = 0.0f; b.eye = 1.0f; wb_.alpha_t = 0.0f;
            weak();
            z = -1.0f;
            break;
        case kHigh:
            if (old == 0) { queue_motion(kY, "e"); queue_motion(kT, "06_Normal>>High"); }
            else if (old == 1) { queue_motion(kY, "l"); queue_motion(kT, "05_Low>>Normal"); }
            else if (old == 3) queue_motion(kY, "b");
            tb_.form = 2; b.form = 2; set_clip(kB, 18);
            b.y_target = 0.0f; b.eye = 1.0f; wb_.alpha_t = 0.0f;
            weak();
            z = 0.0f;
            break;
        case kOnBack:
            if (old == 0) { queue_motion(kY, "g", false); queue_motion(kT, "18_Normal>>OnBack"); }
            else if (old == 1) { queue_motion(kY, "m", false); queue_motion(kT, "05_Low>>Normal"); }
            else if (old == 2) { queue_motion(kY, "j", false); queue_motion(kT, "17_High>>Normal"); }
            tb_.form = 3; b.form = 3; set_clip(kB, 28);
            b.y_target = 0.0f; b.eye = 0.0f;
            b.leg_r_t = b.leg_l_t = b.chest_t = b.hand_r_t = b.hand_l_t = 1.0f;
            wb_.alpha_t = 1.0f; wb_.weak = 0;                      // no draw
            z = 10.0f;
            break;
        default: break;
    }
    if (nf >= 0 && nf <= 3) {
        B.move_target = {0.0f, B.pos.y, z}; B.move_radius = 0.2f; B.move_speed = 0.2f;    // the inline MoveTo
        b.turn_over_t = 360; b.turn_over = 6.0f;
        if (b.fall == 0) cam(6, 0.02f);                            // not while the player is falling
        zoom(0.5f, -0.01f);
    }
    b.cooldown = 180; b.homing_timer = 180; tb_.tentacle_cd = 180; tb_.swing_cd = 180;
    mat_mode(2);
}

// A §A.1.1 blocks 1..7.
void Boss08::shell_body() {
    Boss08BodyBlock& b = bb_;
    Boss08CoreBlock& w = wb_;
    Boss08Mesh& Wm = mesh_[kW];
    // 1. alpha lerps
    step(b.thunder_l, b.thunder_l_t, 0.025f, 0.025f);
    step(b.thunder_r, b.thunder_r_t, 0.025f, 0.025f);
    step(b.leg_l, b.leg_l_t, 0.005f, 0.01f);
    step(b.leg_r, b.leg_r_t, 0.005f, 0.01f);
    step(b.hand_l, b.hand_l_t, 0.005f, 0.01f);
    step(b.hand_r, b.hand_r_t, 0.005f, 0.01f);
    step(b.chest, b.chest_t, 0.005f, 0.01f);
    // 2. regrow timers
    float* const tgt[3] = {&b.chest_t, &b.leg_l_t, &b.leg_r_t};
    for (int g = 0; g < 3; ++g)
        if (b.regrow[g] != 0) {
            ++b.regrow[g];
            if (b.regrow[g] > 600 && w.exposure == 0) { *tgt[g] = 1.0f; b.regrow[g] = 0; }
        }
    // 3. material alphas
    auto set3 = [&](int a, int c, int d, float v) { mat_alpha_[std::size_t(a)] = mat_alpha_[std::size_t(c)] = mat_alpha_[std::size_t(d)] = v; };
    set3(27, 26, 25, shown(b.leg_l, b.leg_l_t));
    set3(30, 29, 28, shown(b.hand_l, b.hand_l_t));
    set3(33, 32, 31, shown(b.leg_r, b.leg_r_t));
    set3(36, 35, 34, shown(b.hand_r, b.hand_r_t));
    const float c = shown(b.chest, b.chest_t);
    if (b.form == kLow) set3(2, 1, 0, c);                          // the chest follows only in Low
    for (int i = 12; i <= 19; ++i) mat_alpha_[std::size_t(i)] = c;
    mat_alpha_[20] = b.thunder_l; mat_alpha_[21] = b.thunder_r;
    if (b.form == kOnBack) { mat_alpha_[18] = b.eye; mat_alpha_[7] = mat_alpha_[18]; }
    // 4. start the regrow timers
    if (b.leg_l == 0.0f && b.regrow[1] == 0) b.regrow[1] = 1;
    if (b.leg_r == 0.0f && b.regrow[2] == 0) b.regrow[2] = 1;
    if (b.chest == 0.0f && b.regrow[0] == 0) b.regrow[0] = 1;
    // 5. DOWN: the group the core sits on is gone
    if (((b.leg_l == 0.0f && w.weak == 1) || (b.leg_r == 0.0f && w.weak == 2) || (b.chest == 0.0f && w.weak == 0)) &&
        w.exposure == 0) {
        w.alpha_t = 1.0f; w.exposure = 1;
        if (b.form == kNormal) { set_clip(kB, 36); queue_motion(kT, "01_NormalWait"); }
        else if (b.form == kLow) { set_clip(kB, 37); queue_motion(kT, "03_LowWait"); }
        else if (b.form == kHigh) { set_clip(kB, 38); queue_motion(kT, "07_HighWait"); }
        touch(kB, false);
        mat_mode(1);
        se(578);
        b.down = true; b.thunder_l_t = 0.0f; b.thunder_r_t = 0.0f;
    }
    // 6. the 20 % band refill
    if (w.refill) {
        Wm.hp += 25.0f;
        const std::uint32_t k = w.band - 1;
        if (k <= 3) {
            static const float kThr[4] = {0.8f, 0.6f, 0.4f, 0.2f}; // table 0x55f7f0
            if (Wm.hp / Wm.max_hp > kThr[k]) { w.refill = false; Wm.hp -= 50.0f; }
        }
    }
    // 7. transition bookkeeping (b.settled is this frame's value from chain())
    if (b.settled) {
        if (b.cam_back && b.cam_idx == 6 && b.fall == 0) { b.cam_back = false; cam(1 + b.form, 0.025f); }
        if (b.restore) {
            b.restore = false;
            touch(kB, true);
            mat_alpha_[20] = b.thunder_l; mat_alpha_[21] = b.thunder_r;
            mat_mode(0);
            zoom(0.5f, -0.01f); se(186); b.down = false;
        }
    } else {
        b.cam_back = true; b.restore = true;
        if (b.fc % 60 == 0) se(338);
    }
}

// A §A.1.2: the fall-off sequence.
void Boss08::fall() {
    Boss08BodyBlock& b = bb_;
    Boss08Player& p = out_->player;
    if (b.fall == 0) {
        float y = ppos_.y;
        if (y <= -1.3f && in_->player_grounded) { y -= 0.7f; ppos_.y = y; p.pos = ppos_; }   // sink through the rim
        if (in_->player_hp < 1.0f) {
            p.dead_stop = true;
            if (ppos_.y < -18.0f) p.flyer = true;
        } else if (y < -18.0f) {
            fade(0xaa0000, fb(0x3bfc0fc1u), 1.0f);                 // 1/130
            out_->flags.push_back({518, 0}); out_->flags.push_back({519, 0});
            p.fall_start = true;
            b.fall = 1; p.sections.push_back(2);
            cam(5, 1.0f);
            shake(220, 0.2f, 0.2f);
            b.fall_tint = 200;
            se(339); se(340);
        }
    } else {
        ppos_ = {0.0f, 495.0f, 0.0f}; p.pos = ppos_;
        std::uint8_t c = b.fall_tint;
        if (c != 0) { --c; b.fall_tint = c; }
        p.tint = 0xff000000u | std::uint32_t(c) * 0x010101u;
        ++b.fall;
        if (b.fall == 30) {
            Boss08Fader f; f.target = 0.0f; out_->fader.push_back(f);
            Boss08Effect& e = fx(517); e.pos = {0.0f, 495.0f, -1.0f};     // stat B, power 0: the fall's hit [A §A.1.4]
        }
        if (b.fall > 218) {
            fade(0xffffff, fb(0x3c088889u), 0.0f);                 // 1/120
            if (in_->player_hp >= 1.0f) p.sections.push_back(9);
            p.fall_end = true;
            ppos_ = {0.0f, 19.0f, -6.0f}; p.pos = ppos_;
            p.tint = 0xffffffffu;
            p.iframes = 180;
            cam(b.settled ? 1 + b.form : 6, 1.0f);
            out_->flags.push_back({134, 0});
            b.fall = 0;
        }
    }
}

// A §A.1.3: the "damaged limb" bit 0x20000.
void Boss08::mat_flags() {
    const Boss08BodyBlock& b = bb_;
    auto bit = [](float a, float tgt) { return a < 0.9f && tgt != 1.0f; };
    auto set3 = [&](int i, bool v) { for (int k = i; k < i + 3; ++k) mat_damaged_[std::size_t(k)] = v; };
    set3(25, bit(b.leg_l, b.leg_l_t));
    set3(28, bit(b.hand_l, b.hand_l_t));
    set3(31, bit(b.leg_r, b.leg_r_t));
    set3(34, bit(b.hand_r, b.hand_r_t));
    const bool c = bit(b.chest, b.chest_t);
    if (b.form == kLow) { set3(0, c); chest_zero_ = false; }
    else { set3(0, false); chest_zero_ = true; }                   // their whole flag word = 0
    for (int k = 12; k <= 17; ++k) mat_damaged_[std::size_t(k)] = c;
}

// FUN_0055aa80 for B [D §2.1..§2.3, §4.1].
void Boss08::damage_body() {
    Boss08BodyBlock& b = bb_;
    Boss08Mesh& B = mesh_[kB];
    if (1.0f > in_->player_hp_mirror) return;
    if (b.form != kOnBack) {                                       // sub-gauge anchors
        if (mini(0).timer != 0) { mini(0).anchor = node(kSkirt1); mini(0).anchor.y = (mini(0).anchor.y - 4.0f) - 3.0f; }
        if (mini(1).timer != 0) { mini(1).anchor = node(kLeftFoot); mini(1).anchor.y -= 4.0f; }
        if (mini(2).timer != 0) { mini(2).anchor = node(kRightFoot); mini(2).anchor.y -= 4.0f; }
    }
    static const int kBase[8] = {kHead, kHead, kSkirt1, kSkirt1, kLeftLeg, kLeftLeg, kRightLeg, kRightLeg};   // u32 0x69edf0
    static const int kGaugeNode[3] = {kSkirt1, kLeftFoot, kRightFoot};                                        // u32 0x69ede4
    float* const tgt[3] = {&b.chest_t, &b.leg_l_t, &b.leg_r_t};
    for (int i = 0; i < kBodyParts; ++i) {
        Boss08Part& p = parts_[std::size_t(i)];
        const Vec3 base = node(kBase[i]);
        auto lerp = [&](int other, float k) {
            const Vec3 o = node(other);
            return Vec3{(o.x - base.x) * k + base.x, (o.y - base.y) * k + base.y, (o.z - base.z) * k + base.z};
        };
        Vec3 c = base;
        switch (i) {
            case 6: c = lerp(kRightFoot, 0.75f); break;
            case 7: c = node(kRightToe11); break;
            case 4: c = lerp(kLeftFoot, 0.75f); break;
            case 5: c = node(kLeftToe11); break;
            case 2: { const Vec3 o = node(kSkirt2); const Vec3 d{o.x - base.x, o.y - base.y, o.z - base.z};
                      c = {d.x + d.x + base.x, d.y + d.y + base.y, d.z + d.z + base.z}; break; }
            case 3: c = lerp(kMaekake, 0.1f); break;
            case 0: c = lerp(kSkirt1, 0.275f); break;
            default: break;                                        // 1: Bone_Head
        }
        p.pos = c; p.def = 998.0f; p.armor = false;
        const int g = i < 4 ? 0 : i < 6 ? 1 : 2;
        enum { kOpen, kClosed, kSunk } st;
        if (b.form == kOnBack) st = i >= 4 ? kClosed : kSunk;
        else if (b.down) st = kSunk;
        else if (*tgt[g] == 0.0f) st = kSunk;
        else if (!b.settled) st = kSunk;
        else if (wb_.exposure != 0) st = kClosed;
        else if (wb_.seq != 0) st = kClosed;
        else st = kOpen;
        if (st == kSunk) p.pos.y -= 999.0f;
        else if (st == kClosed) { p.def = 999.0f; p.armor = true; }
        // intake [D §4, §4.1]: one part-frame with damage = one step
        const float d = p.dmg;
        if (d == 0.0f) continue;
        B.flash = 1.65f; p.dmg = 0.0f;
        if (d <= 0.0f) continue;
        if (b.form == kOnBack) continue;
        float v = *tgt[g] - kLimbStep[diff_];
        *tgt[g] = v;
        if (0.0f >= v) { *tgt[g] = 0.0f; v = 0.0f; }
        b.smoke[g] = 1; b.last_group = g;
        if (diff_ != 0) continue;                                  // sub-gauges on Normal and below only
        Boss08MiniGauge& m = mini(g);
        m.timer = 255; m.value = v; m.max = 1.0f;
        m.anchor = node(kGaugeNode[b.last_group]);
        m.anchor.y -= 4.0f;
        if (b.last_group == 0) m.anchor.y -= 3.0f;
    }
}

// FUN_0055aa80 for W [D §2.4, §3, §4.2].
void Boss08::damage_core() {
    Boss08BodyBlock& b = bb_;
    Boss08CoreBlock& w = wb_;
    Boss08Mesh &B = mesh_[kB], &Wm = mesh_[kW];
    if (1.0f > in_->player_hp_mirror) return;
    Boss08Part& p = parts_[kBodyParts];
    p.pos = Wm.pos;
    p.def = B.def;                                                 // the body's (softened) DEF, read live
    if (b.form == kOnBack) {
        if (b.window != 0) {
            p.def = B.def + 20.0f; if (burst()) p.def = B.def + 5.0f;
            p.radius = 2.5f;
        } else {
            p.def = 998.0f; p.power = 1.0f;
            if (burst()) p.def = B.def + 15.0f;
            p.radius = 4.5f;
        }
    } else if (w.exposure != 0) w.was_exposed = true;
    else { p.pos.y -= 999.0f; exposure_end(); }
    // intake
    const float d = p.dmg;
    if (d == 0.0f) return;
    Wm.flash = 1.65f; p.dmg = 0.0f;
    if (d <= 0.0f) return;
    if (b.form != kOnBack) set_clip(kW, 4);                        // 05_Damage (through the tail's table slot)
    else if (b.window != 0) {}
    else {                                                         // BARRIER UP
        const std::uint32_t limit = kBarrierHits[diff_] - (easy_ ? 20u : 0u);
        if (b.barrier_hits < limit) {
            ++b.barrier_hits;
            if (burst()) { b.barrier_hits += 14; if (b.barrier_hits > limit) b.barrier_hits = limit; }
            Boss08MiniGauge& m = mini(0);
            m.timer = 255; m.value = float(limit - b.barrier_hits); m.max = float(limit);
            m.anchor = in_->w_ora; m.anchor.y -= 7.0f;
        } else {                                                   // BREAK [D §4.2.1]
            b.barrier_hits = 0;
            if (w.h_barrier != 0) { kill(w.h_barrier); barrier_break(Boss08Stat::kCore); }
            b.window = kWindow[diff_] + (easy_ ? 180u : 0u);
            p.def = B.def + 15.0f;                                 // this frame only
            touch(kB, false);
            B.no_jump = false;
        }
    }
    Wm.hp -= d;                                                    // 0x55bd60 — every core path
    if (1.0f > Wm.hp) {
        if (w.band < 4) Wm.hp = 1.0f;                              // cannot die before the fourth band
        else {                                                     // KILL [D §4.2.2]
            b.dead = true; out_->flags.push_back({215, 0}); Wm.hp = 0.0f;
            queue_motion(kB, "30_OnBackWait");
            queue_motion(kT, "19_OnBackWait");
            fade(0xaaaaaa, fb(0x3d088889u), 0.0f);
        }
    }
}

// D §3: the end of an exposure — band, next form, next weak point.
void Boss08::exposure_end() {
    Boss08BodyBlock& b = bb_;
    Boss08CoreBlock& w = wb_;
    const Boss08Mesh& Wm = mesh_[kW];
    if (!w.was_exposed) return;
    if (w.alpha != 0.0f) return;
    w.was_exposed = false;
    if (!w.refill) {                                               // advance at most one band
        static const float kThr[4] = {0.8f, 0.6f, 0.4f, 0.2f};
        const float ratio = Wm.hp / Wm.max_hp;
        if (w.band < 4 && kThr[w.band] > ratio) { ++w.band; w.refill = true; }
    }
    if (w.band == 4) b.form_req = kOnBack;
    else {
        b.form_req = int(R(3));                                    // always overwritten
        do b.form_req = int(R(3)); while (b.form_req == b.form);
    }
    if (b.form >= 0 && b.form <= 2) {                              // drawn also when the next form is 3
        const std::uint32_t r = R(3);
        if (r == 0) w.weak = 1; else if (r == 1) w.weak = 2; else if (r == 2) w.weak = 0;
    }
    if (w.refill) { b.cooldown = 300; b.homing_timer = 300; tb_.tentacle_cd = 300; tb_.swing_cd = 300; }
}

// The effect block of the barrier break [D §4.2.1, C5]; the death's copy takes &B+0x94 [D C14].
void Boss08::barrier_break(Boss08Stat stat) {
    for (int k = 0; k < 6; ++k) {
        Boss08Effect& e = fx(140, Boss08Call::kFx2);
        e.stat = stat; e.parent = Boss08Parent::kCoreOra;
        const float s = 6.0f + float(k);
        e.at_scale = {s, s, s}; e.at_mode = 2;
    }
    if (Boss08Effect* e = inline_fx(514, stat)) {
        e->or_1dc = 0x108; e->parent = Boss08Parent::kCoreOra; e->at_mode = 1;
    }
    queue_motion(kT, "23_Guard_End");
    se2(320); se2(579);
}

// FUN_0055d3d0 [D §5].
void Boss08::death() {
    Boss08BodyBlock& b = bb_;
    Boss08Mesh &B = mesh_[kB], &T = mesh_[kT], &Wm = mesh_[kW];
    if (!b.dead) return;
    b.thunder_l_t = 0.0f; b.thunder_r_t = 0.0f;
    mini(0).timer = 0;
    if (wb_.h_barrier != 0) {                                      // the barrier was up at the kill
        kill(wb_.h_barrier);
        barrier_break(Boss08Stat::kBody);
        b.window = kWindow[diff_] + (easy_ ? 180u : 0u);
    }
    const bool alive = in_->player_hp_mirror >= 1.0f;
    if (alive && -15.0f > ppos_.y) {                               // a falling player is put back
        fade(0xffffff, fb(0x3c088889u), 0.0f);
        ppos_ = {0.0f, 0.95f, 0.0f}; out_->player.pos = ppos_;
    }
    Wm.alpha = 1.0f;
    b.window = 9999;
    if (!rush_ && in_->flag227 < 2) {                              // STORY, waiting for the script
        out_->kill_bit_particles = true;
        if (alive) {
            out_->flags.push_back({227, 1});
            for (int f = 529; f <= 532; ++f) out_->flags.push_back({f, 0});
        }
        return;
    }
    if (!b.death_started) {                                        // RELEASE
        if (rush_) {
            if (alive) out_->player.iframes_min = 9999;
            out_->kill_bit_particles = true;
        }
        if (alive) b.death_started = true;
    }
    out_->flags.push_back({194, 0});
    if (b.death_started) {
        const std::uint32_t t = b.death_t;
        if (t == 0) {
            se2(186);
            // (every B part +0x19c = 0: the parts are never armed)
            out_->death_light = true;
            fade(0xffffff, fb(0x3c088889u), 0.0f);
        }
        if (t > 200) {                                             // FADE
            const float a = B.alpha - 0.005f;
            B.alpha = a; B.alpha_target = a;
            T.alpha = T.alpha - a; T.alpha_target = T.alpha;       // sic: the tail loses the body's new alpha
            if (0.0f >= B.alpha) { B.alpha = 0.0f; out_->gauge_hide = true; }
            if (B.alpha == 0.0f && !rush_) {
                if (alive && !b.buffs_cleared) {
                    b.buffs_cleared = true;
                    for (int f = 544; f <= 546; ++f) out_->flags.push_back({f, 0});
                }
                out_->kill_record = true;
            }
        }
        if (b.death_t > 660 && rush_) out_->flags.push_back({156, 0});
        if (t == 0) {
            Boss08Effect& e = fx(609, Boss08Call::kFx2); e.pos = B.pos; e.src = 1;
        }
        if (t % 60 == 0) out_->rumble.push_back(5);
    }
    ++b.death_t;                                                   // also while the player is dead [D C7]
}

// FUN_0055d170 [CE §4.1].
void Boss08::dispatcher() {
    Boss08BodyBlock& b = bb_;
    const Boss08Mesh& Wm = mesh_[kW];
    if (1.0f > Wm.hp) return;
    switch (b.form) {
        case kNormal: out_->light = 0xffaa8080u; pick_normal(); break;
        case kLow:
            out_->light = 0xffaaaa80u;
            if (sec_[kB] == 10 && 433.0f <= f_[kB] && f_[kB] <= 443.0f) { b.turn_base = 24.0f; b.turn_base_t = 1; }   // dead [CE C11]
            pick_low();
            break;
        case kHigh: out_->light = 0xff8080aau; pick_high(); break;
        case kOnBack: out_->light = 0xffaa80aau; wb_.exposure = 0; wb_.seq = 0; pick_onback(); break;
        default: break;
    }
    if (wb_.exposure != 0 || wb_.seq != 0) {                       // core exposed or flying: stop moving / turning
        b.move_base = 0.0f; b.move_base_t = 1; b.turn_base = 0.0f; b.turn_base_t = 1;
        return;
    }
    if (!b.settled) return;
    if (!(0.4f > Wm.hp / Wm.max_hp)) return;
    if (b.homing_timer != 0) { --b.homing_timer; return; }
    b.homing_req = true;
    b.homing_timer = b.form == kOnBack ? kHomingExtra[diff_] + kHomingBase[diff_] + (easy_ ? 60u : 0u)
                                       : kHomingBase[diff_] + (easy_ ? 30u : 0u);
}

// FUN_0055bf50 [CE §4.2].
void Boss08::pick_normal() {
    Boss08BodyBlock& b = bb_;
    b.move_base = 0.0f; b.move_base_t = 1; b.turn_base = 0.0f; b.turn_base_t = 1;
    if (1.0f > mesh_[kW].hp) return;
    if (sec_[kB] != 0) return;
    if (b.cooldown != 0) { --b.cooldown; return; }
    if (!b.settled) return;
    if (wb_.exposure != 0 || wb_.seq != 0) return;
    auto p2 = [&](std::uint32_t n) { b.move_over = 0.0f; b.move_over_t = n; };
    auto p3 = [&](float v, std::uint32_t n) { b.turn_over = v; b.turn_over_t = n; };
    switch (R(6)) {
        case 0:
            if (ppos_.x > 0.0f) { if (b.leg_l > 0.0f && b.hand_l > 0.0f) { p2(150); set_clip(kB, 1); } }
            else if (b.leg_r > 0.0f && b.hand_r > 0.0f) { p2(150); set_clip(kB, 3); }
            break;
        case 1:                                                    // the detached-arm attacks, by the weak point
            if (wb_.weak == 2) { if (b.beat_l == 0 && b.leg_l_t > 0.0f && b.beat_r == 0 && b.clap == 0) { p2(120); b.beat_l = 1; } }
            else if (wb_.weak == 1) { if (b.beat_r == 0 && b.leg_r_t > 0.0f && b.beat_l == 0 && b.clap == 0) { p2(120); b.beat_r = 1; } }
            else if (wb_.weak == 0) {
                if (b.clap == 0 && b.leg_r_t > 0.0f && b.leg_l_t > 0.0f && b.beat_r == 0 && b.beat_l == 0) { p2(120); b.clap = 1; }
            }
            break;
        case 2: p2(360); set_clip(kB, 5); b.breath = 0; break;
        case 3: p2(360); set_clip(kB, 5); b.breath = 1; break;
        case 4: p2(180); p3(0.0f, 180); set_clip(kB, 35); queue_motion(kT, "27_DoubleLariat"); break;
        case 5:
            if (wb_.weak == 2) { p2(120); p3(0.0f, 120); set_clip(kB, 4); b.thunder_r_t = 1.0f; }
            else if (wb_.weak == 1) { p2(120); p3(0.0f, 120); set_clip(kB, 2); b.thunder_l_t = 1.0f; }
            else if (wb_.weak == 0) { p2(360); p3(4.0f, 360); set_clip(kB, 5); b.breath = 0x63; }
            break;
        default: break;
    }
    b.cooldown = R(60) + kAttackCd[diff_] + (easy_ ? 60u : 0u);    // always, also when the roll did nothing
}

// FUN_0055c490 [CE §4.3].
void Boss08::pick_low() {
    Boss08BodyBlock& b = bb_;
    b.move_base = 0.0f; b.move_base_t = 1; b.turn_base = 0.0f; b.turn_base_t = 1;
    if (1.0f > mesh_[kW].hp) return;                               // the only exit that skips TAIL
    do {
        if (sec_[kB] != 9) break;
        if (b.settled && b.low_latch) { b.low_latch = false; b.jump_lock = 600; break; }
        if (b.cooldown != 0) { --b.cooldown; break; }
        if (!b.settled) break;
        if (wb_.exposure != 0 || wb_.seq != 0) break;
        switch (R(5)) {
            case 0: set_clip(kB, 11); break;
            case 1: set_clip(kB, 12); break;
            case 2: set_clip(kB, 13); break;
            case 3: set_clip(kB, 14); break;
            case 4: if (b.jump_lock == 0) { set_clip(kB, 10); queue_motion(kT, "04_Jump"); b.jump_lock = 600; } break;
            default: break;
        }
        b.cooldown = R(60) + kAttackCd[diff_] + (easy_ ? 60u : 0u);
    } while (false);
    if (b.jump_lock != 0) --b.jump_lock;                           // TAIL
}

// FUN_0055c700 [CE §4.4].
void Boss08::pick_high() {
    Boss08BodyBlock& b = bb_;
    b.move_base = 0.0f; b.move_base_t = 1;
    if (b.settled) { b.turn_base = 2.0f; b.turn_base_t = 1; } else { b.turn_over = 1.0f; b.turn_over_t = 1; }
    if (1.0f > mesh_[kW].hp) return;
    if (sec_[kB] != 19) return;
    if (sec_[kT] != 6) return;                                     // the tail must be idle too
    if (b.cooldown != 0) { --b.cooldown; return; }
    if (!b.settled) return;
    if (wb_.exposure != 0 || wb_.seq != 0) return;
    switch (R(6)) {
        case 0: set_clip(kB, 20); break;
        case 1: set_clip(kB, 23); b.turn_base = 4.0f; b.turn_base_t = 180; break;
        case 2: set_clip(kB, 24); break;
        case 3: queue_motion(kT, "08_LeftRotate_Start"); break;
        case 4: queue_motion(kT, "11_RightRotate_Start"); break;
        case 5: queue_motion(kT, "14_Right&LeftRotate_Start"); break;
        default: break;
    }
    b.cooldown = R(60) + kAttackCd[diff_] + (easy_ ? 60u : 0u);
}

// FUN_0055c960 [CE §4.5].
void Boss08::pick_onback() {
    Boss08BodyBlock& b = bb_;
    b.move_base = 0.0f; b.move_base_t = 1; b.turn_base = 1.25f; b.turn_base_t = 1;
    if (1.0f > mesh_[kW].hp) return;
    if (sec_[kB] != 29) return;
    if (b.cooldown != 0) { --b.cooldown; return; }
    if (!b.settled) return;                                        // no core test here
    switch (R(5)) {
        case 0: set_clip(kB, 30); b.thunder_l_t = 1.0f; break;
        case 1: set_clip(kB, 31); break;
        case 2: set_clip(kB, 32); b.thunder_r_t = 1.0f; break;
        case 3: set_clip(kB, 33); break;
        case 4:
            if (b.window != 0) { if (tb_.laser == 0) tb_.laser = 1; }        // barrier down: the eye laser
            else { set_clip(kB, 34); b.thunder_r_t = 1.0f; b.thunder_l_t = 1.0f; }
            break;
        default: break;
    }
    b.cooldown = R(60) + kAttackCd[diff_] + (easy_ ? 60u : 0u);
}

// FUN_00559dd0 [CE §3.1]: done(T) = An(T)+0x1c == 1.
void Boss08::tail_chain() {
    Boss08TailBlock& t = tb_;
    const int c = sec_[kT];
    const bool done = cnt_[kT] == 1;
    auto go = [&](int from, int to) { if (c != from) return false; if (done) set_clip(kT, to); return true; };
    switch (t.form) {
        case kNormal:
            if (c == 26) { if (sec_[kB] == 35) return; set_clip(kT, 0); return; }   // cut when the BODY leaves its lariat
            if (c != 0 && done) set_clip(kT, 0);
            break;
        case kLow:
            if (go(16, 1)) return;
            if (c != 2 && done) set_clip(kT, 2);
            break;
        case kHigh:
            if (go(4, 5) || go(7, 8) || go(8, 9) || go(10, 11) || go(11, 12) || go(13, 14) || go(14, 15)) return;
            if (c != 6 && done) set_clip(kT, 6);
            break;
        case kOnBack:
            if (c == 16 || c == 4) { if (done) set_clip(kT, 17); return; }
            if (go(20, 21)) return;
            if (c == 21) return;
            if (go(22, 18)) return;
            if (c == 23) {                                         // dead: the main clip is never 23 [CE C15]
                if (done && ++t.dead_stab == 150) { t.dead_stab = 0; set_clip(kT, 24); }
                return;
            }
            if (c == 18) return;
            if (done) set_clip(kT, 18);
            break;
        default: break;
    }
}

// FUN_00558290 [CE §3.2]: every tick, every form, before the flag-156 gate.
void Boss08::tail_fx() {
    Boss08TailBlock& t = tb_;
    auto bind = [&](int k, int section) {
        const int tr = tentacle_track(k);
        track_bind(trk_[std::size_t(tr)], k, section);
        out_->tracks.push_back({true, tr, kTentacleNode[k], section});
        cnt_[kT] = 0;                                              // sic: the bind clears the MAIN completion count
    };
    auto release = [&](int k) {
        const int tr = tentacle_track(k);
        track_release(trk_[std::size_t(tr)]);
        out_->tracks.push_back({false, tr, kTentacleNode[k], -1});
    };
    // (1) SwingTail trail
    if (sec_[kT] == 25 && f_[kT] == 1730.0f)
        if (Boss08Effect* e = inline_fx(469, Boss08Stat::kTail)) on_body(*e);
    // (2) Beat timelines
    for (int k = 0; k < kTracks; ++k) {
        const std::uint32_t c = t.beat[k];
        if (c == 0) continue;
        if (c == 68) se2(358);
        if (c == 80) { Boss08Effect& e = on_body(fx(470 + k), {0.0f, 0.1f, 0.0f}); e.stat = Boss08Stat::kTail; }
        if (c == 290) { release(k); t.beat[k] = 0; }
        else t.beat[k] = c + 1;
    }
    // (3) Stab timelines
    for (int k = 0; k < kTracks; ++k) {
        const std::uint32_t c = t.stab[k];
        if (c == 0) continue;
        if (c == 40) {
            t.vortex[k] = 0;
            if (Boss08Effect* e = inline_fx(481, Boss08Stat::kTail)) { on_body(*e, kVortex[k]); e->handle = t.vortex[k] = next_handle_++; }
            se2(359);                                              // also when the spawn was refused
        }
        if (c == 80)
            if (Boss08Effect* e = inline_fx(482, Boss08Stat::kTail)) e->pos = {ppos_.x, 0.0f, ppos_.z};   // world, not kept
        if (c == 290) {
            bind(k, 24);                                           // 25_PullFromTheGround
            kill(t.vortex[k]);                                     // the engine has no null test here [CE K8]: guarded
        }
        if (c == 360) { release(k); t.stab[k] = 0; }
        else t.stab[k] = c + 1;
    }
    // (4) Touch flag and sparks
    const int c = sec_[kT];
    if (c != 8 && c != 11 && c != 14 && c != 19 && c != 25) { touch(kT, false); t.spark_t = 0; }
    else {
        touch(kT, true);
        const int lo = c == 25 ? 2 : 10, hi = c == 25 ? 9 : 49;
        const std::uint32_t n = t.spark_t;
        if (n % 120 == 0) se2(346);
        if (n % 28 == 0)
            for (int i = lo; i < hi; ++i)
                for (int j = 0; j < 3; ++j) {
                    Boss08Effect* e = inline_fx(464, Boss08Stat::kTail);
                    if (!e) continue;
                    const Vec3 v = in_->t_local[std::size_t(i)];
                    e->parent = Boss08Parent::kTailNode; e->parent_index = i; e->at_mode = 1; e->flag201 = true;
                    e->at_off = {(v.x * 0.333333f) * float(j), (v.y * 0.333333f) * float(j), (v.z * 0.333333f) * float(j)};   // 0x6bd29c
                }
        t.spark_t = n + 1;
    }
    // (5) Eye laser
    const std::uint32_t a = t.laser;
    if (a == 0) return;
    if (a == 2) {
        if (Boss08Effect* e = inline_fx(465, Boss08Stat::kTail)) {
            e->parent = Boss08Parent::kTailNode; e->parent_index = kTNodeEye; e->at_mode = 1;
            e->at_rot = {0.0f, kPi, 0.0f};                         // no +0x201 write
        }
        se2(363);
    }
    t.laser = a == 180 ? 0 : a + 1;
}

// FUN_0055cb70 [CE §3.3]: OnBack only.
void Boss08::tail_decide() {
    Boss08BodyBlock& b = bb_;
    Boss08TailBlock& t = tb_;
    if (1.0f > mesh_[kW].hp) return;
    if (t.form != kOnBack) return;
    if (b.window != 0) {                                           // barrier DOWN: the open window counts here
        --b.window;
        if (b.window == 0) {                                       // window over -> barrier back
            touch(kB, true); mesh_[kB].no_jump = true;
            set_clip(kT, 20);                                      // SetClipName(T, "21_Guard_Start")
            return;
        }
        if (sec_[kT] == 20 || sec_[kT] == 21 || sec_[kT] == 22) return;
        if (t.swing_cd != 0) { --t.swing_cd; return; }
        if (!b.settled) return;
        set_clip(kT, 25);                                          // SetClipName(T, "26_SwingTail")
        t.swing_cd = R(360) + kSwingCd[diff_] + (easy_ ? 60u : 0u);
        return;
    }
    // barrier UP
    if (sec_[kT] == 18) set_clip(kT, 20);
    if (wb_.h_barrier == 0) {                                      // the barrier effect, every tick without a handle
        Boss08Effect& e = fx(144, Boss08Call::kFx2);
        e.src = 1; e.parent = Boss08Parent::kCoreOra; e.at_off = {0.0f, -0.5f, 0.0f}; e.at_scale = {2.5f, 2.5f, 2.5f};
        e.at_mode = 1; e.handle = wb_.h_barrier = next_handle_++;
    }
    if (sec_[kT] != 21) return;
    if (t.tentacle_cd != 0) { --t.tentacle_cd; return; }
    if (!b.settled) return;
    const std::uint32_t w1 = W(), w2 = W();
    const int k = int((w2 >> 17) & 7);
    if (t.stab[k] != 0 || t.beat[k] != 0) return;                  // busy: 2 more draws next tick
    const int tr = tentacle_track(k);
    const int section = (w1 & 0x20000u) ? 19 : 23;                 // 20_Beat / 24_StabInTheGround
    if (w1 & 0x20000u) t.beat[k] = 1; else t.stab[k] = 1;
    track_bind(trk_[std::size_t(tr)], k, section);
    out_->tracks.push_back({true, tr, kTentacleNode[k], section});
    cnt_[kT] = 0;
    t.tentacle_cd = R(60) + kAttackCd[diff_] + (easy_ ? 60u : 0u);
}

// FUN_0055ddc0 kind 2 [A §A.1.5] with FUN_0055a0d0 [A §A.7] and FUN_00559780 [A §A.8].
void Boss08::core_shell() {
    Boss08BodyBlock& b = bb_;
    Boss08CoreBlock& w = wb_;
    Boss08Mesh& Wm = mesh_[kW];
    const int f = b.form;
    // the clip rule: no completion test
    const int s = sec_[kW];
    if (f == kOnBack) { if (s != 5) set_clip(kW, 5); }
    else if (s == 0 || s == 2) {}
    else if (s == 3) set_clip(kW, 0);
    else set_clip(kW, 2);
    // the aura
    if (f != kOnBack && w.fc % 59 == 0 && w.alpha == 1.0f)
        if (Boss08Effect* e = inline_fx(454, Boss08Stat::kCore)) { e->parent = Boss08Parent::kCoreOra; e->at_mode = 1; }
    if (f == kOnBack) {
        w.weak = 0;
        Wm.pos = in_->t_eye;                                       // W+0x20..0x5f = the world matrix of T's Bone_eye
    } else {
        const int n = kWeakNode[std::clamp(w.weak, 0, 2)];
        Vec3 tgt = node(n);
        auto mid = [&](int foot) { const Vec3 o = node(foot); tgt.x += (o.x - tgt.x) * 0.5f; tgt.y += (o.y - tgt.y) * 0.5f; tgt.z += (o.z - tgt.z) * 0.5f; };
        if (n == kLeftLeg) mid(kLeftFoot);
        if (n == kRightLeg) mid(kRightFoot);
        if (Wm.move_speed == 0.0f) {                               // no MoveTo running
            switch (w.seq) {
                case 0: if (!w.dropped) Wm.pos = tgt; break;       // glued to the weak point
                case 1: {
                    Vec3 up{Wm.pos.x, Wm.pos.y + 7.5f, Wm.pos.z}; float v = 0.125f;
                    if (f == 0) { up.y = Wm.pos.y + 10.0f; v = 0.17f; }
                    if (f == 1) { up.y = Wm.pos.y + 7.75f; v = 0.14f; }
                    if (f == 2) { up.y = Wm.pos.y + 14.0f; v = 0.2f; }
                    move_to(kW, up, v, v); w.seq = 2; queue_motion(kW, "04_Wait>>Swim");
                    break;
                }
                case 2:
                    if (++w.seq_wait > 30) {
                        float v = 0.06f;
                        if (f == 0 || f == 2) v = 0.08f;
                        if (f == 1) v = 0.1f;
                        Vec3 p = node(kSpine2); if (f == 1) p.y += 0.5f;
                        move_to(kW, p, v, v); queue_motion(kW, "02_Swim>>Wait");
                        w.seq = 3; w.seq_wait = 0;
                    }
                    break;
                case 3: w.seq = 0; break;
                default: break;
            }
        }
        if (w.exposure != 0) {                                     // EXPOSED
            ++w.exposure;
            if (w.exposure == 2) {
                Boss08Effect& e = fx(514); e.src = 1; e.stat = Boss08Stat::kCore;
                e.parent = Boss08Parent::kCoreOra; e.at_mode = 1;
                w.dropped = true;
                move_to(kW, {Wm.pos.x, 1.5f, Wm.pos.z}, 0.03f, 0.03f);     // the core sinks to y 1.5
            }
            if (w.exposure >= kExposure + (easy_ ? 60u : 0u)) {
                w.dropped = false; w.exposure = 0; w.alpha_t = 0.0f;
                b.leg_r_t = b.leg_l_t = b.chest_t = b.hand_r_t = b.hand_l_t = 1.0f;
                w.seq = 1;
            }
            Wm.no_hit = false;                                     // (also on the closing frame)
        } else Wm.no_hit = true;
    }
    step(w.alpha, w.alpha_t, fb(0x3bb43958u), 0.02f);              // ALPHA: -0.0055 / +0.02
}

// FUN_0055ddc0 kind 3 [A §A.1.5].
void Boss08::arms_shell() {
    Boss08Mesh& A = mesh_[kA];
    step(ab_.alpha, ab_.alpha_t, 0.025f, 0.025f);                  // FUN_0055bed0
    A.alpha = ab_.alpha;
    if (ab_.alpha == 0.0f) A.pos.y = -999.0f;
}

// FUN_0055f800 [CE §4.7]. [approx] the five angle constants are the spec's six printed digits (it gives the
// addresses, not the bit patterns).
bool Boss08::camera_hook(int k, Boss08CamRecord& rec, const Vec3& P) {
    const Boss08BodyBlock& b = bb_;
    const Vec3& Bp = mesh_[kB].pos;
    const float dx = P.x - Bp.x, dy = P.y - Bp.y, dz = P.z - Bp.z;
    const float d = std::fabs(std::sqrt((dy * dy + dx * dx) + dz * dz));
    const float k3Deg = 0.0523599f;                                // (3 deg)
    switch (k) {
        case 1:                                                    // Normal
            if (!st_.fov1) st_.fov1 = rec.fov - k3Deg;             // f32 0x6bd148
            if (!st_.pit1) st_.pit1 = rec.eye_pitch - 0.0349066f;  // f32 0x6bd110 (2 deg)
            if (!st_.h1) st_.h1 = rec.offset.y;
            rec.fov = d * kDeg * 0.25f + *st_.fov1;
            rec.eye_pitch = d * 0.015f + *st_.pit1;
            rec.offset.y = *st_.h1 + 0.75f;
            break;
        case 2:                                                    // Low
            if (!st_.fov2) st_.fov2 = rec.fov - 0.122173f;         // f32 0x6bd1b4 (7 deg)
            if (!st_.pit2) st_.pit2 = rec.eye_pitch - k3Deg;
            rec.fov = d * kDeg * 0.35f + *st_.fov2;
            rec.eye_pitch = d * 0.01f + *st_.pit2;
            break;
        case 3:                                                    // High: the block pushout() fills
            rec.eye_fixed = b.cam_eye; rec.target_fixed = b.cam_target; rec.eye_dist = b.cam_dist; rec.fov = b.cam_fov;
            rec.offset = b.cam_off; rec.eye_mode = 0; rec.target_mode = 0;
            break;
        case 4:                                                    // OnBack
            if (!st_.fov4) st_.fov4 = rec.fov - 0.174533f;         // f32 0x6bd1f0 (10 deg)
            if (!st_.pit4) st_.pit4 = rec.eye_pitch - 0.20944f;    // f32 0x6bd21c (12 deg)
            if (!st_.h4) st_.h4 = rec.offset.y;
            rec.fov = d * kDeg * 0.5f + *st_.fov4;
            rec.eye_pitch = d * 0.01f + *st_.pit4;
            rec.offset.y = *st_.h4 - 1.5f;
            break;
        case 5: rec.target_fixed = {0.0f, 490.0f, 0.0f}; break;    // the fall
        default: break;
    }
    return true;                                                   // 1 for every k while the block exists
}

}  // namespace cleria
