// M280 Chups — brain FUN_004a34b0 (handler FUN_004a5300 = M100's minus the arena clamp, plus the
// lifesteal; AI index 21). Spec: build/ai_spec/M280.md. The lifesteal (a landed hit heals it by
// max(1, floor(maxHP / 20 | 14 | 7.5))) is the host's, where the enemy volume lands (view_game.inl).
//
// A hovering insect (hover 3.0). Free, it runs at a point 1 u behind the target (or behind a damaged
// ally it picked, 15% per re-roll, up to 5 random pool actors within 8 u) and 25% a tick starts a
// 70-tick swoop down to 1.5 and back. Within 1.25 of the target, off cooldown, from behind (+-45 deg)
// and with fewer than 3 already latched, it latches (section 7): glued to one of three slots behind
// the target, unhittable, a sphere (r 0.4, power -5) every 8 ticks and EF2 33 on the target. It lets
// go after 20 / 30 / 40 turns of more than 40 deg with movement input, when the latch time (200 /
// 400 / 9999) runs out, or at once in the player's sections 0x10d / 0x110: a 5-u shove behind the
// target on knock channel 2, a forced section 1, a 300..399 cooldown. On an ally it heals 25 / 40 /
// 55 / 70 every 24 ticks (EF2 325 every 32) until a 200 / 300 / 400 budget is spent.
// [not modelled] the keep-out box restore (FUN_0042dc90); Hugo's +0x7c4 release; the
// green heal popups (FUN_004fae00); the effects' attachment to the target (spawned at its origin);
// FUN_00451730 kills every live effect of the id (here only this Chups's own); the pool sampling's
// order and count (the non-player actors in list order behind a player slot 0) [H].
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

float fcos(float x) { return float(std::cos(double(x))); }
float fsin(float x) { return float(std::sin(double(x))); }
Vec3 origin_of(const Actor& c) { return {c.pos.x, c.pos.y + c.height, c.pos.z}; }
Vec3 hurt_centre(const Actor& c) { return {c.pos.x, c.pos.y + c.height + c.hurt_off, c.pos.z}; }   // +0x6c..

constexpr int kFx = 0x88c;   // the latch effect's tag

}  // namespace

AiOut m280(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4a35d8
    Actor* A = e.actor;
    const Vec3 Tpos = e.target;                                    // the snapshot (origin)
    const int d = difficulty(e);
    const int bb5c = e.diff_index;                                 // DAT_0076bb5c, read raw
    if (r15(e) % 1000u < 5u) {                                     // 0.5%: re-roll the turn rate (RAW)
        a.f(0x814) = (unit(e.rng()) * 0.3f + 0.85f) * a.f(0x7ac);  // 0x6bd278 / 0x6bd3bc
    }
    if (a.i(0x7c0) != 0) a.i(0x7c0) -= 1;
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4a5256)
            ai::init(a, p, e);
            a.i(0x7a8) = 0; a.i(0x7c0) = 0;
            a.f(0x7ac) = a.f(0x814);
            const int k = std::min(d - 3, 0);
            a.f(0x820) = float(k) * a.f(0x820) * 0.1f + a.f(0x820);
            return out;
        }
        case 1:
            ai::idle(a, p, e);
            a.i(0x858) = 0;
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);                        // the tick continues
    const int lim = bb5c <= 0 ? 20 : bb5c == 1 ? 30 : 40;
    const int ldur = bb5c <= -1 ? 200 : bb5c == 0 ? 400 : 9999;
    Actor* H = (e.world && a.i(0x858) != 0) ? e.world->find(a.i(0x858)) : nullptr;
    auto set_pos = [&](const Vec3& o) { if (A) A->pos = {o.x, o.y - A->height, o.z}; };
    auto release_fx = [&] {
        if (a.i(kFx) != 0) { AiEffect k; k.kill_tag = kFx; out.effects.push_back(k); a.i(kFx) = 0; }
    };
    auto fx_at = [&](int id, const Vec3& at) {
        AiEffect fx; fx.id = id; fx.pos = at; fx.knock = 0.0f; fx.tag = kFx;   // +0x1c8 = 0 here
        out.effects.push_back(fx);
        a.i(kFx) = 1;
    };

    switch (a.i(0x728)) {
        case 0: {                                                  // FREE (0x4a459b)
            float gap = 99999.0f;
            const int oldH = a.i(0x858);
            float aH = 0.0f;
            if (a.i(0x7c0) == 0 && e.world && A) {                 // A. the ally search
                if (r15(e) % 1000u < 150u) {
                    a.i(0x858) = 0; H = nullptr;
                    std::vector<Actor*> pool{nullptr};             // [H] slot 0 = the player
                    e.world->for_each([&](Actor& c) { pool.push_back(&c); });
                    const std::uint32_t count = std::uint32_t(pool.size());
                    const int n = int(std::min(5u, count));
                    for (int i = 0; i < n; ++i) {
                        Actor* c = pool[r15(e) % count];
                        if (!c || c == A) continue;
                        if (!(c->max_hp > c->hp) || c->hp < 1.0f) continue;
                        if (c->cast_id == A->cast_id && c->ai.i(0x858) == A->instance_id) continue;
                        const Vec3 dv = origin_of(*c) - origin_of(*A);
                        if (8.0f > std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z)) {
                            a.i(0x858) = c->instance_id; H = c; break;
                        }
                    }
                }
            }
            Vec3 goal = Tpos;                                      // B. keep or drop H
            if (H) {
                bool drop = H->hp < 1.0f;
                if (!drop && a.i(0x7c0) == 0 && r15(e) % 1000u < 50u) drop = true;
                if (!drop) {
                    const Vec3 C = hurt_centre(*H);
                    const Vec3 dv = C - e.self;
                    gap = std::max(0.0f, std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z) - H->hurt_radius);
                    aH = float(std::atan2(double(dv.x), double(dv.z)));   // 0x4a48f6: swapped operands, as read
                    goal = C;
                } else {
                    a.i(0x858) = 0; H = nullptr;
                }
            }
            if (oldH != a.i(0x858)) a.i(0x7c0) = int(r15(e) % 90u);
            goal.x += fcos(e.target_heading + 3.14159f);           // 1 u behind the target (0x6bd6ac)
            goal.z += fsin(e.target_heading + 3.14159f);
            a.set_v(0x894, goal);
            a.steer() = 4; a.action() = 0x400;
            a.hittable = true;
            if (a.i(0x7a4) != 0) a.i(0x7a4) -= 1;
            if (a.i(0x72c) == 0 && a.i(0x7c4) == 0 && r15(e) % 1000u < 250u) {   // C. the swoop
                a.i(0x72c) = 1; a.i(0x7a0) = 70;
            }
            if (a.i(0x72c) == 1) {
                const std::uint32_t c = std::uint32_t(--a.i(0x7a0));
                if (c > 40u) a.f(0x834) = float(int(c) - 40) / 20.0f + 1.5f;
                else if (c > 30u) a.f(0x834) = 1.5f;
                else a.f(0x834) = float(30 - int(c)) / 20.0f + 1.5f;
                if (c == 0) { a.i(0x72c) = 0; a.i(0x7a4) = 30; }
            }
            if (e.section == 7) return out;                        // D. the latch tests
            if (!H) { if (!(1.25f > a.dist())) return out; }
            else if (!(1.25f > gap)) return out;
            if (a.cool() != 0) return out;
            if (!H) {                                              // on the target (0x4a4b77)
                const Vec3 to = a.v(0x734);
                const float an = float(std::atan2(double(to.z), double(to.x)));
                if (!(0.785398f > float(std::fabs(ai::angle_diff(e.target_heading, an))))) return out;
                int latched = 0;
                if (e.world) e.world->for_each([&](Actor& c) { latched += c.cast_id == 0x20 && c.ai.i(0x728) == 1; });
                a.i(0x7bc) = latched;
                if (latched > 2) return out;
                out.section = 7;                                   // gated; result ignored
                a.action() = 2; a.dur() = ldur;
                const int r = int(r15(e) % 100u);
                a.i(0x728) = 1;
                a.cool() = a.dur() + 300 + r;
                a.i(0x79c) = 0;
                a.f(0x7b4) = e.target_heading;
                fx_at(33, Tpos);                                   // pc\adol\adol_dash_max.eft
            } else {                                               // on the ally (0x4a4ec3)
                if (!(0.785398f > float(std::fabs(ai::angle_diff(H->ai.heading, aH))))) return out;
                a.i(0x7bc) = 0;
                bool taken = false;
                if (e.world) e.world->for_each([&](Actor& c) {
                    taken |= c.cast_id == 0x20 && c.ai.i(0x728) == 2 && c.ai.i(0x858) == H->instance_id; });
                if (taken) { a.i(0x7bc) = 1; return out; }
                out.section = 7;                                   // gated; result ignored
                a.action() = 2; a.dur() = ldur;
                const int r = int(r15(e) % 100u);
                a.i(0x728) = 2;
                a.cool() = a.dur() + 300 + r;
                a.i(0x79c) = 0;
                a.f(0x7b4) = e.target_heading;
                fx_at(325, origin_of(*H));                         // bos\bs_01_awa_hit.eft
            }
            return out;
        }
        case 1: {                                                  // ON THE TARGET (0x4a3f35)
            a.steer() = 0;
            const Vec3 P0 = e.self;
            const float h = e.target_heading;
            Vec3 o{};
            switch (a.i(0x7bc)) {
                case 0: o = {0.5f * fcos(h + 3.14159f), 0.5f, 0.5f * fsin(h + 3.83972f)}; break;   // mixed, as read
                case 1: o = {0.5f * fcos(h + 2.44346f), 0.4f, 0.5f * fsin(h + 2.44346f)}; break;
                case 2: o = {0.5f * fcos(h + 3.83972f), 0.4f, 0.5f * fsin(h + 3.83972f)}; break;
                default: break;
            }
            a.hittable = false;                                    // +0x60 = 0xffffff7f
            set_pos({(Tpos.x + o.x + 2.0f * P0.x) * 0.333333f, (Tpos.y + o.y + 2.0f * P0.y) * 0.333333f,
                     (Tpos.z + o.z + 2.0f * P0.z) * 0.333333f});
            out.heavy_hold = true;                                 // DAT_0076c15c |= 8; DAT_0076c164 = 1 (0x4a40bb)
            if ((a.dur() & 7) == 0) {                              // every 8th tick
                fx_at(33, Tpos);
                AiHitbox hb;                                       // FUN_0047a730 -> FUN_004494e0
                hb.shape = 0; hb.radius = 0.4f; hb.fwd = 0.2f; hb.voff = -0.2f;
                hb.life = 8; hb.rehit = 96 - 20 * d; hb.one_shot = 1; hb.power = -5.0f; hb.knock = 0.0f;
                out.hitboxes.push_back(hb);
            }
            if (e.target_moving && float(std::fabs(ai::angle_diff(e.target_heading, a.f(0x7b4)))) > 0.698132f) {
                a.i(0x79c) += 1; a.f(0x7b4) = e.target_heading;    // a shake-off turn
            }
            const bool forced = e.target_section == 0x10d || e.target_section == 0x110;
            if (std::uint32_t(a.i(0x79c)) <= std::uint32_t(lim) && a.dur() != 0 && !forced) return out;
            a.action() = 0x400;                                    // RELEASE (0x4a43da)
            const int r = int(r15(e) % 100u);
            a.i(0x728) = 0; a.cool() = 300 + r;
            const float an = e.target_heading + 3.14159f;
            if (A) { A->mover.k2.x += fcos(an) * 5.0f; A->mover.k2.z += fsin(an) * 5.0f; }
            out.section = 1; out.section_force = true;             // inlined FUN_00446620(1)
            release_fx();
            a.i(0x7c0) = int(r15(e) % 90u);
            return out;
        }
        case 2: {                                                  // ON THE ALLY (0x4a37bc)
            const int lim10 = 10 * lim;
            if (H) {
                a.steer() = 4; a.set_v(0x894, origin_of(*H));
                const Vec3 P0 = e.self;
                const Vec3 C = hurt_centre(*H);
                const float yo = (H->hurt_shape == 1 ? H->hurt_half_h : H->hurt_radius) - 0.1f;
                const float rr = (A ? A->hurt_radius : 0.6f) - 0.1f;
                const float h = H->ai.heading;
                Vec3 o{};
                switch (a.i(0x7bc)) {
                    case 0: o = {rr * fcos(h + 3.14159f), yo, rr * fsin(h + 3.83972f)}; break;
                    case 1: o = {rr * fcos(h + 2.44346f), yo, rr * fsin(h + 2.44346f)}; break;
                    case 2: o = {rr * fcos(h + 3.83972f), yo, rr * fsin(h + 3.83972f)}; break;
                    default: break;
                }
                a.hittable = false;
                set_pos({(C.x + o.x + 2.0f * P0.x) * 0.333333f, (C.y + o.y + 2.0f * P0.y) * 0.333333f,
                         (C.z + o.z + 2.0f * P0.z) * 0.333333f});
                if ((a.dur() & 0x1f) == 0) fx_at(325, origin_of(*H));
                if (std::uint32_t(a.dur()) % 24u == 0) {
                    const int amt = d < 0 ? 15 : 15 * d + 25;
                    if (H->max_hp > H->hp && H->hp >= 1.0f) {
                        H->hp = std::min(H->max_hp, H->hp + float(amt));
                        out.popups.push_back({amt, 0x00ff00, origin_of(*H)});   // FUN_004fae00 (0xff00ff00) at H+0x50
                    } else a.i(0x79c) += lim10;                    // full or dead: forces the release
                    a.i(0x79c) += std::max(amt, 24);
                }
            }
            if (std::uint32_t(a.i(0x79c)) <= std::uint32_t(lim10) && a.dur() != 0 && H &&
                e.self.y <= (A ? A->floor_y : 0.0f) + 3.5f)
                return out;
            a.action() = 0x400;                                    // RELEASE (0x4a3ccd)
            const int r = int(r15(e) % 100u);
            a.i(0x728) = 0; a.cool() = 300 + r;
            const float an = H ? float(std::atan2(double(H->pos.z - (A ? A->pos.z : 0.0f)),
                                                  double(H->pos.x - (A ? A->pos.x : 0.0f))))
                               : float(r15(e) % 360u) * 0.0174533f;
            if (A) { A->mover.k2.x += fcos(an) * 5.0f; A->mover.k2.z += fsin(an) * 5.0f; }
            out.section = 1; out.section_force = true;
            release_fx();
            a.i(0x858) = 0;
            a.i(0x7c0) = int(r15(e) % 90u);
            return out;
        }
        default: return out;
    }
}

}  // namespace cleria::brains
