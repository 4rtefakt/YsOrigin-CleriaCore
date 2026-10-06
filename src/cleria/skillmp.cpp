#include "cleria/skillmp.hpp"

#include <algorithm>

namespace cleria::skillmp {

namespace {
const short kCost[4][3] = {{1000, 600, 300}, {700, 700, 400}, {2000, 600, 900}, {300, 700, 600}};
const short kCostNm[4][3] = {{0, 0, 0}, {300, 300, 300}, {0, 300, 300}, {300, 300, 300}};
const short kCostAdolAlt[3] = {1500, 1800, 1200};
const short kRegen[4] = {2, 4, 6, 8};
// 0x69bc14[0x100 .. 0x12a]
const unsigned char kKind[0x2b] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 7, 2, 5, 5, 5, 5, 5, 5, 4, 1, 1, 1,
                                   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
}  // namespace

int bracelet_slot(int magic) {
    return magic == 0x74 ? 0 : magic == 0x75 ? 1 : magic == 0x76 ? 2 : 3;
}

int skill_section(int slot, bool charged) {
    static const int kNormal[3] = {0x10d, 0x10c, 0x10e}, kCharged[3] = {0x110, 0x10f, 0x111};
    if (slot < 0 || slot > 2) return 0;
    return charged ? kCharged[slot] : kNormal[slot];
}

int damage_class(int magic) {
    return magic == 0x74 ? 0 : magic == 0x75 ? 2 : magic == 0x76 ? 1 : 3;
}

int attack_kind(int section) {
    if (section < 0x100 || section >= 0x100 + int(sizeof kKind)) return 0;
    return kKind[section - 0x100];
}

float cost_f(const CostIn& in) {
    if (in.slot < 0 || in.slot > 2) return 0.0f;
    const int c = in.chr & 3;
    int base = (c == 0 && in.adol_alt) ? kCostAdolAlt[in.slot] : kCost[c][in.slot];
    if (in.difficulty == 2) base += kCostNm[c][in.slot];
    float mult = 1.0f;
    if ((in.bc80 & 0x80000) || in.acc_mp) mult = 0.80f;
    else if (in.bc80 & 0x40000) mult = 0.88f;
    else if (in.bc80 & 0x40) mult = 0.94f;
    if (c == 1 && in.boosting) mult *= 0.5f;
    return float(base) * mult;
}

int cost(const CostIn& in) { return int(cost_f(in)); }   // ftol truncates

void Mp::tick(int level, float buff) {
    const float add = float(kRegen[std::clamp(level, 0, 3)]);
    if (lag == mp) {                       // exact float compare
        if (mp < max) mp = std::min(max, mp + (add + 5.0f) * buff);   // the stat pass clamps
    } else if (mp <= lag) {
        lag -= add + 8.0f;                 // a spend: drain the lag first
    } else {
        lag = mp;                          // resync
    }
}

bool Charge::add(float a, bool held, int bracelet_level, std::uint32_t bc80) {
    // The threshold table by blessing; slot [0] is 1 when the button is held, 400 auto-charging.
    const short t = (bc80 & 0x4000) ? 2000 : (bc80 & 0x800) ? 2400 : (bc80 & 0x400) ? 2700 : 3000;
    const short thr[2] = {short(held ? 1 : 400), t};
    if (level == 1 && bracelet_level == 0) return false;   // Lv1 bracelets stop at the plain cast
    amount += a;
    if (a < 0.0f) level = 0;
    if (amount < 0.0f) { amount = 0.0f; return false; }
    const float lim = float(thr[std::min(level, 1)] + 1);
    if (amount < lim) return false;
    amount = lim;                                       // clamped at the threshold
    if (level > 1) return false;
    ++level;
    return level == 2;
}

SkillOut skill_tick(const SkillIn& in, const Mp& mp, Charge& ch) {
    SkillOut out;
    CostIn ci = in.cost;
    ci.slot = bracelet_slot(in.magic);
    if (!in.skill_held && !in.attack_held) {           // (+0x510 & 5) == 0: the release test
        if (ch.level > 0 && in.section != 9 && in.section != 5 && !in.busy && !in.fix && ci.slot != 3) {
            out.cast = true;
            out.section = skill_section(ci.slot, ch.level == 2);
            out.cost = cost(ci);
            out.damage_class = damage_class(in.magic);
            out.level = ch.level;
            ch.amount = 0.0f; ch.level = 0;            // FUN_0046ea00 clears it (L255-257)
        }
        // Then FUN_0046cf20(-9999) on every path. Note its early return: a level-0 bracelet at
        // c138 == 1 is NOT cleared, so a release refused in section 9/5 stays armed.
        // FUN_0046cf20 itself returns at once under a fix, so nothing clears then.
        if (!in.fix) ch.add(kChargeClear, false, in.bracelet_level, in.bc80);
        return out;
    }
    // Held: charge while affordable (0x466a64..0x466ae7).
    if (ci.slot == 3) return out;
    if (mp.mp - cost_f(ci) < 0.0f || in.busy) return out;
    if (in.fix) return out;      // 0x40b2c0 -> FUN_0046cf20(-9999), which returns under the fix
    if (in.kind == 5 || in.kind == 3 || in.kind == 2) return out;
    if (in.skill_held) out.charged_ready = ch.add(kChargeStep, true, in.bracelet_level, in.bc80);
    else if (in.auto_charge) out.charged_ready = ch.add(kChargeStep, false, in.bracelet_level, in.bc80);
    return out;
}

float boost_passive(float hp, float hp_max, int difficulty, std::uint32_t bc80) {
    const float b = difficulty == 2 ? 30.0f : 25.0f;
    const float m = (bc80 & 0x10000) ? 2.0f : (bc80 & 0x8000) ? 1.66f : (bc80 & 0x20) ? 1.33f : 1.0f;
    const float f = hp_max > 0 ? hp / hp_max : 1.0f;
    return ((1.0f - f) * b + 1.0f) * m;
}

float boost_drain(int difficulty, bool bda4, bool acc_3b) {
    const float d = difficulty == 2 ? (bda4 ? 400.0f : 90.0f) : 30.0f;
    return d * (acc_3b ? 0.6f : 1.0f);
}

bool boost_can_add(bool enabled187, bool busy161, bool fix, bool boosting, float gauge) {
    return enabled187 && !busy161 && !fix && !boosting && gauge < kBoostFull;
}

}  // namespace cleria::skillmp
