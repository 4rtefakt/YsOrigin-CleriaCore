// World item pickups: build/item_re/PICKUPS.md. See pickup.hpp.
#include "cleria/pickup.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cleria/enemies.hpp"

namespace cleria {

namespace {

constexpr float kPi = 3.14159265f;

// The uniform draw: asfloat((w & 0x7fffff) | 0x3f800000) - 1.0, in [0, 1).
float unit(std::uint32_t w) {
    const std::uint32_t b = (w & 0x7fffffu) | 0x3f800000u;
    float f;
    std::memcpy(&f, &b, 4);
    return f - 1.0f;
}
float lerp(float a, float b, float u) { return (b - a) * u + a; }
// The 256-entry cos table 0x68f350 ([0] = 1, [64] = -0, [128] = -1).
float cos256(int i) { return std::cos(float(i & 255) * (2.0f * kPi / 256.0f)); }
Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// FUN_00584c90: the constructor's own draw and the velocity from yaw + speed.
void construct(ef2sim::Rng& rng, Pickup& p, float yaw, float spd, float vy) {
    const std::uint32_t w = rng.next();
    p.glint_phase = int((w >> 25) & 3);                       // bit 31 would negate the spin (0)
    const int i = int(yaw * 40.7437f);                        // cvttss2si; movzx al masks both
    p.vel = {cos256(i) * spd, vy, cos256(i - 64) * spd};
    p.prev = p.pos;
    p.base = p.pos;
    p.resting = (p.flags & 0x10) != 0;
}

// FUN_00585cb0: reflect off the hit plane, scale, maybe come to rest.
void bounce(ef2sim::Rng& rng, Pickup& p, const Vec3& hit, const Vec3& n, const PickupWorld& w) {
    const float vn = dot(p.vel, n);
    if (!(vn < 0.0f)) return;
    p.pos = add(p.prev, scale(hit - p.prev, 0.995f));
    p.vel = add(p.vel, scale(n, vn * -2.0f));
    const float r = unit(rng.next()) * 0.2f + 0.55f;
    float hf, sf, vf = r;
    if (n.y >= 0.5f) { hf = r; sf = 0.5f; } else { hf = 0.8f; sf = 0.8f; }
    if (p.in_water) { sf *= 0.08125f; vf = 0; hf *= 0.125f; }
    p.vel.y *= vf; p.vel.x *= hf; p.vel.z *= hf;
    if (n.y >= 0.75f && p.vel.y < 0.035f) {
        p.resting = true;                                     // the room mesh is a static collider
        if ((p.flags & 2) && w.hit) {
            Vec3 pt, nn;
            p.ground = w.hit(add(p.pos, {0, 0.05f, 0}), add(p.pos, {0, -510.0f, 0}), &pt, &nn);
            if (p.ground) p.ground_y = pt.y + 0.02f;
        }
    }
    rng.next();                                               // the spin sign draw (spin is 0)
    (void)sf;
}

}  // namespace

float Pickup::alpha() const {
    if (!(flags & 8) && age > life - 35.0f) return std::max(0.0f, (life - age) / 35.0f);
    return 1.0f;
}

int Pickup::glint(std::uint32_t ms) const {
    if (kind == 2 || !(flags & 4) || age <= 50.0f) return 0;
    const int k = 85 - int((std::uint32_t(glint_phase) + (ms >> 4)) % 85u);
    return k <= 10 ? 7 * k : 0;
}

Vec3 Pickup::sprite_centre(const Vec3& cam_up, const Vec3& cam_fwd) const {
    Vec3 c{pos.x, pos.y + 0.1f, pos.z};                        // 0x6bd19c
    if (!in_water) { c.x -= cam_fwd.x * 0.15f; c.y -= cam_fwd.y * 0.15f; c.z -= cam_fwd.z * 0.15f; }   // 0x6bd1d8
    c.x += cam_up.x * 0.075f; c.y += cam_up.y * 0.075f; c.z += cam_up.z * 0.075f;                     // 0x6bd174
    return c;
}

Vec3 Pickup::shadow_centre(const Vec3& cam_fwd) const {
    return {pos.x - cam_fwd.x * 0.15f, ground_y - cam_fwd.y * 0.15f, pos.z - cam_fwd.z * 0.15f};
}

float Pickup::shadow_alpha() const {
    if (!ground || in_water) return 0.0f;
    return std::min(0.37647f, (0.4f - (pos.y - ground_y) * 0.14f) * alpha());
}

void spawn_pickups(ef2sim::Rng& rng, const int* bag, int n, const Vec3& pos, const SpawnArgs& a,
                   bool whole_room_water, std::vector<Pickup>& out) {
    const float base_life = (a.flags & 4) ? 990.0f : 90.0f;   // ([0x6d5d92] >= 2 -> 105: [not modelled])
    std::vector<int> b;
    int idx = 0, delay = 0;
    for (int k = 0; k < a.count; ++k) {
        if (idx == 0 && n != 0) {                              // the "shuffle": n swaps with a[0]
            b.assign(bag, bag + n);
            for (int s = 0; s < n; ++s) {
                const std::uint32_t r = rng.next();
                const std::uint32_t i = r % std::uint32_t(n);
                const std::uint32_t j = (i >> 16) % std::uint32_t(n);
                std::swap(b[i], b[j]);
            }
        }
        Pickup p;
        p.id = b.empty() ? -1 : b[std::size_t(idx)];
        idx = (idx + 1 < n) ? idx + 1 : 0;
        p.life = float((rng.next() >> 1) % std::uint32_t(a.extra_life + 1)) + base_life;
        const float yaw = lerp(a.yaw0, a.yaw1, unit(rng.next()));
        const float spd = lerp(a.h0, a.h1, unit(rng.next()));
        const float vy = lerp(a.v0, a.v1, unit(rng.next()));
        p.pos = pos;
        p.flags = a.flags;
        p.delay = delay;
        p.half = a.size * 0.315f;
        p.in_water = whole_room_water;
        construct(rng, p, yaw, spd, vy);
        out.push_back(p);
        delay += a.delay_step;
    }
}

void roll_enemy_drops(ef2sim::Rng& rng, const Enemy& rec, int cast_id, const Vec3& pos,
                      const DropContext& c, std::vector<Pickup>& out, bool entry_draw) {
    if (entry_draw) rng.next();                               // the entry draw (death effect scale)
    if (c.no_drops) return;
    if (cast_id == 0xb0 || cast_id == 0xaf) return;           // rolls skipped (SP / flag 162 are the host's)
    SpawnArgs a;
    for (const DropSlot& s : rec.drops) {
        if (s.item_id == -1) continue;
        const float luck = c.luck + (c.flag108 ? 2.5f : 0.0f) + (c.bless8 ? 5.0f : 0.0f);
        const int roll = int(rng.next() >> 17) % 100;
        const float ka = std::min(0.25f, float(c.launch) * 0.025f) + 1.0f;
        const float kb = std::min(0.25f, float(c.combo) * 0.01f);
        const float chance = (kb + ka) * ((luck * 0.1f + 1.0f) * s.percent) * c.rate;
        if (chance > float(roll)) spawn_pickups(rng, &s.item_id, 1, pos, a, c.whole_room_water, out);
    }
    if (c.boss_lock) return;
    static constexpr int kLv2[3] = {73, 75, 77}, kLv1[3] = {72, 74, 76};
    static constexpr std::uint32_t kMod[3] = {1200, 2000, 1600};
    for (int k = 0; k < 3; ++k) {
        const int nn = c.launch + c.combo;
        const int r1 = int(rng.next() >> 17) % 100;
        const std::uint32_t r2 = (rng.next() >> 17) % kMod[k];
        const float f = std::min(99.0f, float(nn));
        if (f * f > float(r2) || r1 == 0) {
            if (int(rng.next() >> 17) % 3 == 0) {
                const std::uint32_t w = rng.next();
                const int id = (w & 0x20000u) ? kLv1[k] : kLv2[k];
                spawn_pickups(rng, &id, 1, pos, a, c.whole_room_water, out);
            }
        }
    }
}

Pickup make_chest_item(ef2sim::Rng& rng, int id, const Vec3& mesh_pos, bool flag) {
    Pickup p;
    p.id = id;
    p.flags = flag ? 0x28 : 0;
    p.kind = 2;
    p.pos = add(mesh_pos, {0, 0.3f, 0});
    p.life = 210.0f;
    p.half = 0.9f * 0.315f;
    construct(rng, p, 0, 0, 0);
    return p;
}

bool pickup_in_reach(const Pickup& p, const Vec3& P) {
    const float dx = P.x - p.pos.x, dy = (P.y - p.pos.y) * 0.66667f, dz = P.z - p.pos.z;
    return dx * dx + dy * dy + dz * dz < 3.61f;
}

void step_pickup(ef2sim::Rng& rng, Pickup& p, const PickupWorld& w) {
    if (p.dead) return;
    if (p.delay > 0) { --p.delay; return; }                   // FUN_00584f90
    p.age += 1.0f;
    if (p.age >= p.life && !(p.flags & 8)) { p.dead = true; return; }
    if (p.kind == 2) {                                        // FUN_00586910: the chest item
        p.t += 1;
        if (!p.closing) {
            if (p.t >= 30 && p.rise < 60.0f) p.rise = float(p.t - 30);
            if (p.flags & 0x20) { if (!w.window_open) p.closing = true; }
            else if (p.t > 210) { p.dead = true; return; }
        } else if (p.flags & 0x20) {
            if (p.rise > 0) { p.rise -= 5.0f; if (p.rise <= 0) { p.dead = true; return; } }
        }
        const float h = p.rise < 60.0f ? p.rise / 60.0f * 0.55f : 0.55f;
        float y = p.base.y + h;
        if (p.t > 90) y += std::sin(float(p.t - 90) * 0.1f) * 0.1f;
        p.pos = {p.base.x, y, p.base.z};
        if (w.hit) {
            Vec3 pt, nn;
            p.ground = w.hit(add(p.pos, {0, 0.05f, 0}), add(p.pos, {0, -510.0f, 0}), &pt, &nn);
            if (p.ground) p.ground_y = pt.y + 0.02f;
        }
        p.prev = p.pos;
        if (!p.closing && (p.sparkle++ & 1) == 0) {
            const float a = unit(rng.next()) * 2.0f * kPi - kPi / 2.0f;
            rng.next();                                       // FUN_00584860's rotation draw
            if (w.sparkle) w.sparkle(p, a);
        }
        return;
    }
    // FUN_005856c0
    if (!p.resting) {
        if (!(p.flags & 2) && w.hit) {
            Vec3 pt, nn;
            p.ground = w.hit(add(p.pos, {0, 0.05f, 0}), add(p.pos, {0, -510.0f, 0}), &pt, &nn);
            if (p.ground) p.ground_y = pt.y + 0.02f;
        }
        if (w.hit) {
            Vec3 pt, nn;
            if (w.hit(p.prev, p.pos, &pt, &nn)) bounce(rng, p, pt, nn, w);
        }
        p.prev = p.pos;
        if ((p.flags & 4) && (p.sparkle++ & 1) == 0) {
            const float a = unit(rng.next()) * 2.0f * kPi - kPi / 2.0f;
            rng.next();
            if (w.sparkle) w.sparkle(p, a);
        }
    }
    if (p.age > 50.0f && (p.flags & 4) && pickup_in_reach(p, w.player) && w.touch && w.touch(p)) {
        p.dead = true;
        return;
    }
    if (p.resting) return;
    p.pos = add(p.pos, p.vel);
    if (!p.in_water) p.vel.y -= 0.0075f;
    else if (p.vel.y > -0.03f) p.vel.y -= 0.0025f;
}

}  // namespace cleria
