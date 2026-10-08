// The GoXx bullet: build/ai_spec/BULLETS.md. See bullet.hpp.
#include "cleria/bullet.hpp"
#include "cleria/cos_table.hpp"

#include <cmath>
#include <cstring>

namespace cleria::bullet {

namespace {

float vdot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// FUN_0042cc50: v / |v| (the sqrt through double, FUN_0041f570); zero stays zero.
Vec3 vnorm(const Vec3& v) {
    const float l = float(std::sqrt(double(v.x * v.x + v.y * v.y + v.z * v.z)));
    if (l == 0.0f) return {};
    const float k = 1.0f / l;
    return {v.x * k, v.y * k, v.z * k};
}

// FUN_00458d40: the xz part, normalised, y = 0 (zero stays zero).
Vec3 flat(const Vec3& v) {
    const float q = v.x * v.x + v.z * v.z;
    if (q == 0.0f) return {};
    const float k = 1.0f / float(std::sqrt(double(q)));
    return {v.x * k, 0.0f, v.z * k};
}

// The axis-angle matrix both FUN_00458970 and FUN_004585c0 build inline (row-major, D3D row
// vectors): the Rodrigues matrix laid out so v * M turns v by -angle about `a`.
ef2sim::M4 axis_matrix(const Vec3& a, float c, float s) {
    const float t = 1.0f - c;
    ef2sim::M4 m;
    m.m[0][0] = a.x * a.x * t + c;       m.m[0][1] = a.x * a.y * t - a.z * s; m.m[0][2] = a.x * a.z * t + a.y * s;
    m.m[1][0] = a.x * a.y * t + a.z * s; m.m[1][1] = a.y * a.y * t + c;       m.m[1][2] = a.y * a.z * t - a.x * s;
    m.m[2][0] = a.x * a.z * t - a.y * s; m.m[2][1] = a.y * a.z * t + a.x * s; m.m[2][2] = a.z * a.z * t + c;
    return m;
}

float acos_clamped(float x) { return float(std::acos(double(x < -1.0f ? -1.0f : x > 1.0f ? 1.0f : x))); }

// INTEGRATE (§3): v = dir*speed - (0, g, 0); speed = |v|; dir = v / |v| when non-zero.
Vec3 integrate(Bullet& b) {
    Spawn& s = b.s;
    const Vec3 v{s.dir.x * s.speed, s.dir.y * s.speed - s.gravity, s.dir.z * s.speed};
    const float l = float(std::sqrt(double(v.y * v.y + v.x * v.x + v.z * v.z)));
    s.speed = l;
    if (l != 0.0f) {
        const float k = 1.0f / l;
        s.dir = {v.x * k, v.y * k, v.z * k};
    }
    return v;
}

// The kinds 0x1e / 0x1f step (0x59d53c / 0x594cf5): INTEGRATE without gravity, except that a
// zero |v| zeroes dir (INTEGRATE keeps it). Returns v; the caller adds it to pos.
Vec3 straight(Bullet& b) {
    Spawn& s = b.s;
    const Vec3 v{s.dir.x * s.speed, s.dir.y * s.speed, s.dir.z * s.speed};
    const float l = float(std::sqrt(double(v.y * v.y + v.x * v.x + v.z * v.z)));
    s.speed = l;
    if (l == 0.0f) {
        s.dir = {};
    } else {
        const float k = 1.0f / l;
        s.dir = {v.x * k, v.y * k, v.z * k};
    }
    return v;
}

void reset_matrix(Bullet& b) { b.mat = ef2sim::translation(b.s.pos.x, b.s.pos.y, b.s.pos.z); }

}  // namespace

Spawn block_spawn(int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f) {
    Spawn s;
    s.kind = kind; s.effect = effect; s.pos = pos; s.dir = dir;
    s.speed = f[0]; s.accel = f[1]; s.gravity = f[2]; s.power = f[3]; s.knock = f[4];
    std::memcpy(&s.f158, &f[5], 4);
    std::memcpy(&s.f15c, &f[6], 4);
    std::memcpy(&s.f160, &f[7], 4);
    s.f164 = f[8]; s.f168 = f[9]; s.f16c = f[10]; s.f170 = f[11]; s.f174 = f[12];
    return s;
}

ef2sim::M4 look_rotation(const Vec3& d) {
    // F = (0, 0, -1) at 0x7461b8; axis = vnorm(d x F), angle = acos(F . vnorm(d)).
    const Vec3 a = vnorm({-d.y, d.x, 0.0f});
    const Vec3 n = vnorm(d);
    const double th = std::acos(double(-n.z));
    const float ang = float(th);
    return axis_matrix(a, float(std::cos(double(ang))), float(std::sin(double(ang))));
}

Vec3 home(const Vec3& pos, const Vec3& dir, const Vec3& target, float max_turn) {
    const Vec3 t{target.x - pos.x, target.y - pos.y, target.z - pos.z};
    const Vec3 ax = vnorm({t.y * dir.z - t.z * dir.y, t.z * dir.x - t.x * dir.z, t.x * dir.y - t.y * dir.x});
    const Vec3 tn = vnorm(t);
    float ang = acos_clamped(vdot(dir, tn));
    if (max_turn < ang) ang = max_turn;                            // minss
    const ef2sim::M4 R = axis_matrix(ax, float(std::cos(double(ang))), float(std::sin(double(ang))));
    const Vec3 o{dir.x * R.m[0][0] + dir.y * R.m[1][0] + dir.z * R.m[2][0],
                 dir.x * R.m[0][1] + dir.y * R.m[1][1] + dir.z * R.m[2][1],
                 dir.x * R.m[0][2] + dir.y * R.m[1][2] + dir.z * R.m[2][2]};
    return vnorm(o);
}

ef2sim::Effect* Bullets::spawn_fx(int id, const Vec3& pos, const Vec3& rot, int side, float atk,
                                  float power, float knock, int rtype, int status, int cls, int owner,
                                  const Vec3& scale) {
    const Ef2* ef = ef2_of_ ? ef2_of_(id) : nullptr;
    if (!ef) return nullptr;
    ef2sim::SpawnParams sp;
    sp.pos = pos; sp.rot = rot; sp.scale = scale;
    sp.power = power; sp.knock = knock; sp.atk = atk;
    sp.rtype = rtype; sp.status = status; sp.cls = cls; sp.side = side; sp.owner = owner;
    return fx_->spawn(id, *ef, sp);
}

Bomb* Bullets::spawn_bomb(const Spawn& s) {
    // FUN_00593550 kind 0xf [BYTES 0x593593..0x593653] -> FUN_00584c90 (BOMB.md §0, M290.md §3.7).
    auto b = std::make_unique<Bomb>();
    b->pos = b->prev = s.pos;
    const float yaw = float(std::atan2(double(s.dir.z), double(s.dir.x)));   // FUN_00427410(z, x)
    const int i = int(yaw * 40.7437f);                             // 0x6bda38, cvttss2si
    b->vel = {cos_table(i) * s.speed, s.dir.y * s.speed, cos_table((i - 0x40) & 0xff) * s.speed};
    b->spin_rate = 0.1875f;
    if (rng_ && (rng_() & 0x80000000u)) b->spin_rate = -b->spin_rate;   // one draw: the spin sign
    b->owner = s.owner; b->atk = s.owner_atk;
    b->mat = ef2sim::translation(s.pos.x, s.pos.y + 0.25f, s.pos.z);
    // FUN_00450410(effect, pos, rot 0, scale 1, side 1, the owner's stats, power 0, knock 1, rtype 0,
    // status 0, cls 0xff), attached to the carrier matrix; its first particle is disarmed.
    b->fx = spawn_fx(s.effect, s.pos, {0, 0, 0}, 1, b->atk, 0.0f, 1.0f, 0, 0, -1, s.owner);
    if (b->fx) {
        b->fx->held = true;
        b->fx->local = ef2sim::M4{};
        b->fx->attach_parent = &b->mat;
        b->fx->attach_mode = 1;
        if (ef2sim::Particle* p = fx_->first_particle(b->fx)) p->hit.armed = false;
    }
    bombs_.push_back(std::move(b));
    return bombs_.back().get();
}

void Bullets::step_bomb(Bomb& b, const Env& env, std::vector<Event>* events) {
    // FUN_00584f90 (BOMB.md §1) with FUN_005856c0 / FUN_00585cb0 (§2); out of water only.
    auto kill = [&] {
        if (b.fx) { b.fx->held = false; b.fx->attach_parent = nullptr; b.fx->kill = true; b.fx = nullptr; }
        b.gone = true;
    };
    const Vec3 at{b.mat.m[3][0], b.mat.m[3][1], b.mat.m[3][2]};
    b.age += 1.0f;
    if (b.age >= b.life) {                                         // 180: the explosion, no motion
        if (b.fx && !b.fx->kill) {
            spawn_fx(267, at, {0, 0, 0}, 1, b.atk, 0.0f, 1.0f, 0, 0, -1, b.owner);   // enemy\m_bom01.eft
            if (events) events->push_back({Event::kSound, 437, at, 0, 0});        // zako\z_bomb
            b.fx->kill = true;
        }
        kill();
        return;
    }
    if (!b.rest) {
        const Vec3 mv{b.pos.x - b.prev.x, b.pos.y - b.prev.y, b.pos.z - b.prev.z};   // last tick's move
        const RayHit r = env.ray && (mv.x != 0 || mv.y != 0 || mv.z != 0) ? env.ray(b.prev, mv, 2) : RayHit{};
        if (r.hit) {                                               // FUN_00585cb0: the bounce
            const Vec3& n = r.normal;
            const float vn = b.vel.x * n.x + b.vel.y * n.y + b.vel.z * n.z;
            if (vn < 0.0f) {
                b.pos = {b.prev.x + (r.point.x - b.prev.x) * 0.995f, b.prev.y + (r.point.y - b.prev.y) * 0.995f,
                         b.prev.z + (r.point.z - b.prev.z) * 0.995f};
                b.vel = {b.vel.x + n.x * (vn * -2.0f), b.vel.y + n.y * (vn * -2.0f), b.vel.z + n.z * (vn * -2.0f)};
                std::uint32_t w = rng_ ? rng_() : 0u;
                std::uint32_t bits = (w & 0x7fffffu) | 0x3f800000u;
                float u; std::memcpy(&u, &bits, 4);
                const float rr = (u - 1.0f) * 0.2f + 0.55f;        // [0.55, 0.75)
                float hf, vf, sf;
                if (n.y >= 0.5f) { hf = rr; vf = rr; sf = 0.5f; } else { hf = 0.8f; vf = rr; sf = 0.8f; }
                b.vel.y *= vf; b.vel.x *= hf; b.vel.z *= hf;
                if (n.y >= 0.75f && b.vel.y < 0.035f) b.rest = true;   // [approx] every collider static
                if (n.y < 0.75f) {
                    w = rng_ ? rng_() : 0u;
                    if (w & 0x80000000u) sf = -sf;
                }
                b.spin_rate *= sf;
            }
        }
        b.prev = b.pos;
    }
    if (!b.rest) {
        b.pos = {b.pos.x + b.vel.x, b.pos.y + b.vel.y, b.pos.z + b.vel.z};
        b.spin += b.spin_rate;
        b.vel.y -= 0.0075f;                                        // 0x6bd064
    }
    const float c = float(std::cos(double(b.spin))), sn = float(std::sin(double(b.spin)));
    ef2sim::M4 rz;                                                 // RotZ(spin), translation pos + (0, 0.25, 0)
    rz.m[0][0] = c; rz.m[0][1] = sn; rz.m[1][0] = -sn; rz.m[1][1] = c;
    rz.m[3][0] = b.pos.x; rz.m[3][1] = b.pos.y + 0.25f; rz.m[3][2] = b.pos.z;
    b.mat = rz;
    if (b.fx) {
        if (b.age >= 10.0f)                                        // re-armed every tick from age 10
            if (ef2sim::Particle* p = fx_->first_particle(b.fx)) p->hit.armed = true;
        if (b.fx->kill) {                                          // a contact detonated it (impact 267)
            if (events) events->push_back({Event::kSound, 437, at, 0, 0});
            kill();
        }
    }
}

Bullet* Bullets::spawn(const Spawn& in) {
    if (in.kind == 0xf) { spawn_bomb(in); return nullptr; }      // 0x59358a: not a bullet
    auto b = std::make_unique<Bullet>();
    b->s = in;
    Spawn& s = b->s;
    // Kinds 0x10 / 0x23: the owner slot becomes a proxy victim (+0x60 = 0x88, ATK 0 from the
    // base ctor) before the effect spawns, so its particles are side 1 with ATK 0 (§1.4).
    bool monster = s.owner_monster;
    b->atk = s.owner_atk;
    if (s.kind == 0x10 || s.kind == 0x23) { monster = true; b->atk = 0.0f; }
    int rtype = 0;
    // Hugo's shot (effect 0x82): rtype 3. [not modelled] its x1.35 speed under DAT_0074c0d8 (Extra mode).
    if (s.effect == 0x82) rtype = 3;
    const int status = s.effect == 0x36 ? 8 : 0;
    if (s.knock < 0.0f) {                                          // an rtype code
        switch (int(s.knock)) {                                    // cvttss2si
            case -1: rtype = 0; break;
            case -2: rtype = 1; break;
            case -3: rtype = 2; break;
            case -4: rtype = 3; break;
            case -5: rtype = 10; break;
            default: break;
        }
        s.knock = 1.0f;
    }
    b->side = 1;
    int cls = -1;
    if (!monster) { b->y190 = s.pos.y; b->side = 0; cls = s.cls; }
    b->fx = spawn_fx(s.effect, s.pos, {0, 0, 0}, b->side, b->atk, s.power + 0.0f, s.knock, rtype, status,
                     cls, s.owner);
    if (!b->fx) return nullptr;                                    // FUN_00594190
    b->fx->held = true;
    // FUN_00451c20(fx, &bullet+0x20, ofs 0, rot 0, scale 1, mode 1): the effect follows the bullet.
    b->fx->local = ef2sim::M4{};
    b->fx->attach_parent = &b->mat;
    b->fx->attach_mode = 1;
    b->mat = ef2sim::mul(look_rotation(s.dir), ef2sim::translation(s.pos.x, s.pos.y, s.pos.z));
    b->owner_pos = s.pos;
    b->serial = ++serial_;
    list_.push_back(std::move(b));
    return list_.back().get();
}

void Bullets::clear() {
    for (auto& b : list_)
        if (b->fx) { b->fx->held = false; b->fx->attach_parent = nullptr; }
    list_.clear();
    probes_.clear();
    for (auto& b : bombs_)
        if (b->fx) { b->fx->held = false; b->fx->attach_parent = nullptr; }
    bombs_.clear();
}

void Bullets::update(const Env& env, std::vector<Event>* events) {
    const std::size_t n = list_.size();
    for (std::size_t i = 0; i < n; ++i) step(*list_[i], env, events);
    for (std::size_t i = 0; i < list_.size();) {
        if (list_[i]->destroyed) list_.erase(list_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
    const std::size_t nb = bombs_.size();                          // the kind-0xf bombs (BOMB.md)
    for (std::size_t i = 0; i < nb; ++i) if (!bombs_[i]->gone) step_bomb(*bombs_[i], env, events);
    for (std::size_t i = 0; i < bombs_.size();) {
        if (bombs_[i]->gone) bombs_.erase(bombs_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
    const std::size_t m = probes_.size();
    for (std::size_t i = 0; i < m; ++i)
        if (!probes_[i]->gone) step_probe(*probes_[i], env, events);
    for (std::size_t i = 0; i < probes_.size();) {
        if (probes_[i]->gone) probes_.erase(probes_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
}

WaterProbe* Bullets::spawn_water_probe(const Vec3& pos, float radius, float step) {
    // FUN_0058f9a0 [BYTES 0x58f9a0..0x58f9f6]: +0x12 = 0x16; the 0x14-byte block -> +0x120..+0x130;
    // +0x134 is 0 from the ctor. Born dead (+0x11 |= 2) when step == 0 or radius == 0 (a NaN
    // compares unequal and lives).
    auto w = std::make_unique<WaterProbe>();
    w->pos = pos; w->radius = radius; w->step = step;
    if (step == 0.0f || radius == 0.0f) w->gone = true;
    probes_.push_back(std::move(w));
    return probes_.back().get();
}

void Bullets::step_probe(WaterProbe& w, const Env& env, std::vector<Event>* events) {
    // FUN_0058fa00 [BYTES 0x58fa00..0x58fca8], the live-step branch (the pause gates are the host's).
    const Vec3& P = env.player;                                    // 0x74c09c +0x50
    const float dy = P.y - w.pos.y, dx = P.x - w.pos.x, dz = P.z - w.pos.z;
    const float d = float(std::sqrt(double(dy * dy + dx * dx + dz * dz)));   // FUN_0041f570
    if (w.radius * w.radius > d * d) {                             // comiss; jbe 0x58fc2a
        // SE 0x266 (614 env/v_fall_in) at the player (FUN_0043a080), then, if the effect pool has
        // room, effect 0x44 (68 common/z_mizu_sibuki_jyanpu, no hit emitter) inlined at the
        // player's origin: side 0 (+0x1d7), power 0, knock 1, rtype 0, status 0, cls 0xff, no
        // stat copy (ATK 0), rot 0, scale 1. [not modelled] the pool-full gate.
        if (events) events->push_back({Event::kSound, 0x266, P, 0, 0});
        spawn_fx(0x44, P, {0, 0, 0}, 0, 0.0f, 0.0f, 1.0f, 0, 0, -1, 0);
        w.gone = true;
        return;
    }
    // Out of reach: the terrain ray (set 2) from pos along (0, -step, 0). A hit ends the probe;
    // else it falls one step, and the 601st fall ends it (inc; cmp 0x258; jbe).
    const RayHit r = env.ray ? env.ray(w.pos, {0.0f, -w.step, 0.0f}, 2) : RayHit{};
    if (r.hit) { w.gone = true; return; }
    w.falls += 1;
    w.pos.y -= w.step;
    if (w.falls > 0x258u) w.gone = true;
}

void Bullets::step(Bullet& b, const Env& env, std::vector<Event>* events) {
    if (b.destroyed) return;
    if (!b.fx || b.fx->live == 0) {                                // the effect finished: gone
        if (b.fx) { b.fx->held = false; b.fx->attach_parent = nullptr; }
        b.fx = nullptr;
        b.destroyed = true;
        return;
    }
    if (b.gone) return;                                            // fading after a handler kill
    Spawn& s = b.s;
    auto sound = [&](int id) { if (events) events->push_back({Event::kSound, id, s.pos, 0, 0}); };
    auto ray = [&](const Vec3& from, const Vec3& d) { return env.ray ? env.ray(from, d, 2) : RayHit{}; };
    if (s.owner != 0 && env.actor) {
        Vec3 op; float hp = 0;
        if (env.actor(s.owner, &op, &hp)) b.owner_pos = op;
    }

    // ---- the handler (jump table 0x596950)
    auto mover1 = [&] {                                            // FUN_00597290
        const Vec3 v = integrate(b);
        if (s.f16c != 0.0f) {                                      // the first live particle's rtype
            if (ef2sim::Particle* p = fx_->first_particle(b.fx)) p->hit.rtype = int(s.f16c);
        }
        s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
        reset_matrix(b);
    };
    switch (s.kind) {
        case 0:
            reset_matrix(b);
            break;
        case 2:
        case 4: {                                                  // the homing window (§3.2)
            if (s.f160 != 0) b.u184 = s.f160;
            const float a = float(b.age);
            s.f160 = s.f16c > a ? 0u : b.u184;
            if (a > s.f170) { s.f160 = 0; b.u184 = 0; }
            mover1();
            break;
        }
        case 0x29: {                                               // FUN_00598020: the lob
            const Vec3 v = integrate(b);
            s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
            reset_matrix(b);
            break;
        }
        case 0xb:
        case 0xe: {                                                // FUN_0059a6a0: the heal shot
            const Vec3 v = integrate(b);
            if (s.f168 != 0.0f) {
                const RayHit r = ray(s.pos, v);
                if (r.hit && vdot(v, r.normal) < 0.0f) b.fx->kill = true;
            }
            s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
            reset_matrix(b);
            auto d2 = [&](const Vec3& q) {
                const float dx = q.x - s.pos.x, dy = q.y - s.pos.y, dz = q.z - s.pos.z;
                return dx * dx + dy * dy + dz * dz;
            };
            bool kill = false;
            Vec3 op = b.owner_pos; float ohp = 0;
            const bool alive = s.owner != 0 && env.actor && env.actor(s.owner, &op, &ohp);
            if (s.owner != 0 && d2(op) < 0.25f) {
                kill = true;
                if (alive && ohp >= 1.0f && s.kind == 0xb) {
                    sound(0xad);
                    if (events) events->push_back({Event::kHeal, 0, s.pos, s.owner, s.f16c});
                }
            } else if (d2(env.player) < 1.0f) {
                kill = true;
                if (env.player_hp >= 1.0f && s.kind == 0xb) {
                    sound(0xad);
                    if (events) events->push_back({Event::kHeal, 0, env.player, 0, s.f170});
                }
            }
            if (kill) { b.fx->kill = true; b.gone = true; return; }
            break;
        }
        case 0x10: {                                               // FUN_00599a30: the drip
            if (s.speed != 0.0f) {
                Vec3 v = integrate(b);
                const RayHit r = ray(s.pos, v);
                const float d = r.hit ? vdot(v, r.normal) : 0.0f;
                if (r.hit && d < 0.0f) {
                    s.pos = {s.pos.x + (r.point.x - s.pos.x) * 0.995f, s.pos.y + (r.point.y - s.pos.y) * 0.995f,
                             s.pos.z + (r.point.z - s.pos.z) * 0.995f};
                    s.dir = vnorm({v.x - 2.0f * d * r.normal.x, v.y - 2.0f * d * r.normal.y,
                                       v.z - 2.0f * d * r.normal.z});
                    s.speed *= s.effect == 0x85 ? 0.85f : (b.u184 ? 0.2f : 0.6f);
                    v = {s.dir.x * s.speed, s.dir.y * s.speed, s.dir.z * s.speed};
                    if (s.speed < 0.1f) {
                        s.speed = 0.0f;
                        const Vec3 h = flat(s.dir);
                        const Vec3 q{s.pos.x + h.x * 0.3f, s.pos.y + h.y * 0.3f, s.pos.z + h.z * 0.3f};
                        if (!ray(q, {0.0f, -0.3f, 0.0f}).hit) {         // slide off a ledge
                            s.dir = h;
                            s.speed = 0.0333333f;
                        }
                    }
                }
                s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
                reset_matrix(b);
            }
            break;
        }
        case 0x1e: {                                               // FUN_0059d2e0: the bubble
            auto draw = [&] { return env.rng ? env.rng() : 0u; };
            b.u194 += 1;
            const std::uint32_t c = b.u194 > 60u ? 60u : b.u194;   // cmova
            const float k = float(double(int(c + 20))) / 80.0f * s.f16c;   // 0x6bdac8
            b.fx->scale_x = b.fx->scale_y = b.fx->scale_z = k;     // fx+0x1ac..: drawn size, hit radius
            if (b.u194 == 1) {                                     // the root's clock: 320..359 ticks
                // (the engine writes through a null pointer when the scan misses; skipped here)
                ef2sim::Particle* p = fx_->first_particle(b.fx);
                const int rem = int((draw() >> 17) % 40u) + 0x140;
                const int life = int((draw() >> 17) % 40u) + 0x140;
                if (p) { p->remaining = rem; p->life = life; }     // p+0x1c4 / p+0x1c0
                if (env.character == 2) {                          // Hugo: 420..479
                    const int rem2 = int((draw() >> 17) % 60u) + 0x1a4;
                    const int life2 = int((draw() >> 17) % 60u) + 0x1a4;
                    if (p) { p->remaining = rem2; p->life = life2; }
                }
            }
            const std::size_t np = fx_->particles().size();        // DAT_0073012c before the burst
            if (env.character == 2) {                              // the speed floor
                if (0.004f > s.speed) s.speed = 0.004f;            // 0x6bd03c
            } else if (0.04f > s.speed) {
                s.speed = 0.04f;                                   // 0x6bd12c
            }
            const Vec3 v = straight(b);                            // no gravity: +0x14c is not read
            s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
            reset_matrix(b);
            // ---- popped (effect 0x143, not on Nightmare, once): the effect as a victim sphere
            //      (fx+0x60 = 0x80, +0x6c = pos, +0x7c = 0), radius per test
            if (s.effect == 0x143 && env.difficulty != 2 && !b.fx->kill) {
                HitShape me{1.25f, s.pos, 0.0f, 0};                // 0x3fa00000
                const Vec3 P = env.player;
                auto burst = [&](float sc) {                       // FUN_00450880: bs_01_awa_hit
                    // [H] stat = the bullet's own +0x94 block (ATK 0); 325 has no hit emitter.
                    spawn_fx(0x145, s.pos, {0, 0, 0}, 1, 0.0f, 0.0f, 1.0f, 0, 0, -1, s.owner, {sc, sc, sc});
                };
                if (env.swing && env.swing(me)) {                  // the player's live swing
                    b.fx->kill = true;
                    burst(1.2f);                                   // 0x3f99999a
                    const Vec3 d = vnorm({P.x - s.pos.x, P.y - s.pos.y, P.z - s.pos.z});
                    if (env.swing_rtype == 0) {                    // P+0x108 == 0
                        if (env.character >= 0 && env.character <= 3) {   // jump table 0x59dc08
                            const float m = env.character == 3 ? 0.75f : 1.0f;   // 0x6bd390 (Toal)
                            if (events) {
                                Event e{Event::kPush, 0, s.pos, 0, 0};
                                e.push = {d.x * m, 0.0f, d.z * m};
                                events->push_back(e);
                            }
                        }
                        sound(0xc5);
                    }
                }
                // Player effects in the particle pool, by the striking particle's effect id
                // (fx+0x1d8). [H] only armed particles: an unarmed one's +0xd0 record is stale.
                const auto& parts = fx_->particles();
                for (std::size_t i = 0; i < np && i < parts.size(); ++i) {
                    ef2sim::Particle& p = *parts[i];
                    const int id = p.fx ? p.fx->id : -1;
                    const HitShape rec{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                    if (id == 0x66 || id == 0x75 || id == 0x67 || id == 0xa0 || id == 0xc1 || id == 0xe ||
                        id == 0x17) {                              // sword / spin effects: pop
                        me.radius = 0.75f;                         // 0x3f400000
                        if (p.hit.armed && overlap(rec, me)) {
                            b.fx->kill = true;
                            burst(0.9f);                           // 0x3f666666
                            sound(0xc5);
                        }
                    } else if (id == 0x82 || id == 0x83 || id == 0x96) {   // shots: absorbed
                        me.radius = 0.1f;                          // 0x3dcccccd
                        if (p.hit.armed && overlap(rec, me)) {
                            p.fx->kill = true;
                            fx_->strike(p);                        // FUN_00564480(p, 1)
                        }
                    } else if (id == 0x86 || id == 0x87) {         // the player's body, r 1.0
                        me.radius = 1.0f;
                        const HitShape body{1.0f, P, 0.0f, 0};
                        if (overlap(body, me) && !b.fx->kill) {
                            b.fx->kill = true;
                            burst(0.9f);
                            sound(0xc5);
                        }
                    }
                }
            }
            // ---- a slow turn toward the player, level (not Hugo, Hard and up, first 150 ticks)
            if (env.character != 2 && b.u194 < 0x96u && env.difficulty >= 1)
                s.dir = home(s.pos, s.dir, {env.player.x, s.pos.y, env.player.z}, 0.005f);   // 0x3ba3d70a
            break;
        }
        case 0x1f: {                                               // inline 0x594c77: the drool drop
            if (b.u194 == 0) {                                     // the root's clock: 15 ticks
                b.u194 = 1;
                if (ef2sim::Particle* p = fx_->first_particle(b.fx)) { p->remaining = 15; p->life = 15; }
            }
            b.fx->scale_x = b.fx->scale_y = b.fx->scale_z = s.f16c;
            const Vec3 v = straight(b);
            s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
            reset_matrix(b);                                       // jmp 0x5942c8
            break;
        }
        case 0x1b: {                                               // inline 0x594bd3: Hugo's volley
            // On the second update (+0x194 == 1) every particle in the pool whose effect has this
            // bullet's effect id (fx+0x1d8, ANY such effect) and whose life (sub+0x20 = p+0x1c0)
            // is 43 is re-clocked: remaining (p+0x1c4) = 79, life = 80. Then FUN_00597290.
            if (b.u194 == 1) {
                for (const auto& pp : fx_->particles()) {
                    ef2sim::Particle& p = *pp;
                    if (p.fx && p.fx->id == s.effect && p.life == 0x2b) { p.remaining = 0x4f; p.life = 0x50; }
                }
            }
            b.u194 += 1;
            mover1();
            break;
        }
        case 0x16: {                                               // inline 0x594b51: the aimed effect
            // While age < 0x2e - {16, 8, 0}[difficulty] (0x6a2ba4; unsigned jae): dir re-aims at
            // the player's origin in one step (max turn 2*pi, 0x40c90fdb); on Normal the target's
            // y is 1.0 (absolute). No motion: jmp 0x5942c8, kind 0's matrix reset.
            static const std::uint32_t kLead[3] = {16, 8, 0};
            const int dif = env.difficulty;
            const std::uint32_t win = 0x2eu - kLead[dif >= 0 && dif <= 2 ? dif : 0];   // [H] 0..2 only
            if (b.age < win) {
                Vec3 t = env.player;
                if (dif == 0) t.y = 1.0f;                          // 0x594b88
                s.dir = home(s.pos, s.dir, t, 6.2831855f);
            }
            reset_matrix(b);
            break;
        }
        case 0x2a: {                                               // FUN_005985e0: the boomerang (M640.md §7)
            const Vec3 v{s.dir.x * s.speed, s.dir.y * s.speed, s.dir.z * s.speed};
            s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
            reset_matrix(b);
            if (b.u184 == 0) {                                     // outbound
                if (!(0.0f < s.speed)) {
                    s.accel = s.accel * -1.0f; b.u184 = 1; s.speed = s.speed * -1.0f;
                    s.f16c = float(double(b.age)) + s.f16c;
                }
                if (ray(s.pos, v).hit) {                           // the one-step look-ahead (set 2)
                    s.speed = s.speed * -0.25f; b.u184 = 1; s.accel = s.accel * -1.0f;
                    s.f16c = float(double(b.age)) + s.f16c;
                }
                break;
            }
            if (b.u184 != 1) break;
            if (s.accel > s.f168) s.accel = s.f168;                // the max accel
            const float a = float(double(b.age));
            float k = s.f16c;
            Vec3 op{}; float ohp = 0.0f;
            const bool owner = s.owner != 0 && env.actor && env.actor(s.owner, &op, &ohp);
            if (a >= k) {
                if (!owner || 1.0f > ohp) { k = 999999.0f; s.f16c = 999999.0f; }
                const float t = ((s.f170 * a) * k) * 0.001f;       // 0x6bcff0
                s.dir = home(s.pos, s.dir, owner ? op : b.owner_pos, t);
                k = s.f16c;
            }
            if (999999.0f > k && owner) {                          // 0x6bdf84
                const float dx = op.x - s.pos.x, dz = op.z - s.pos.z;
                if (0.25f > (dx * dx + 0.0f) + dz * dz) fx_->kill_effect(b.fx);   // caught: FUN_0047a7c0
            }
            break;
        }
        case 5: {                                                  // FUN_005988e0: the ricochet (KHONSCLARD.md §B.1.6)
            if (b.u184 == 2) { s.speed = 0.0f; s.dir = {0.0f, 0.0f, -1.0f}; reset_matrix(b); break; }   // parked
            Vec3 v = integrate(b);
            const RayHit r = ray(s.pos, v);
            const float d = r.hit ? vdot(v, r.normal) : 0.0f;
            if (r.hit && d < 0.0f) {
                v = {v.x - 2.0f * d * r.normal.x, v.y - 2.0f * d * r.normal.y, v.z - 2.0f * d * r.normal.z};   // -2.0 0x6be098
                s.pos = {s.pos.x + (r.point.x - s.pos.x) * 0.995f, s.pos.y + (r.point.y - s.pos.y) * 0.995f,
                         s.pos.z + (r.point.z - s.pos.z) * 0.995f};                          // 0x6bd424
                s.dir = vnorm(v);
                s.speed *= 0.6f;                                   // 0x6bd334 (0.4 needs +0x184 != 0: never)
                v = {s.dir.x * s.speed, s.dir.y * s.speed, s.dir.z * s.speed};
                if (s.speed < 0.0667f && r.normal.y > 0.866f) {    // 0x6bd160 / 0x6bd3c0: rests on a floor
                    b.u184 = 2; s.speed = 0.0f; s.dir = {0.0f, 0.0f, -1.0f};
                    reset_matrix(b);
                    break;
                }
            }
            s.pos = {s.pos.x + v.x, s.pos.y + v.y, s.pos.z + v.z};
            reset_matrix(b);
            break;
        }
        case 6: {                                                  // 0x59439f..0x5944e6: the snaking mover (§B.1.5)
            // psi = f10 * sin(phi), phi = ((int)f9 * age + 90) mod 360, through the 256-entry cosine table;
            // v = dir turned by psi about y (D3D row vector); dir itself changes only by homing.
            const std::uint32_t phi = (std::uint32_t(int(s.f168)) * b.age + 90u) % 360u;
            // [H] the sign: the spec's reading is f10 * sin(phi) (= 0.7 cos(5 deg age), +40 deg at age 0).
            const int k = int(float(phi) * 0.017453292f * 40.7437f);
            const float psi = s.f16c * float(std::cos(double((k - 64) & 255) * (6.28318531 / 256.0)));
            const float c = float(std::cos(double(psi))), sn = float(std::sin(double(psi)));
            const Vec3 d{s.dir.x * c - s.dir.z * sn, s.dir.y, s.dir.x * sn + s.dir.z * c};
            s.pos = {s.pos.x + d.x * s.speed, s.pos.y + d.y * s.speed, s.pos.z + d.z * s.speed};
            reset_matrix(b);
            break;
        }
        case 7: {                                                  // FUN_00598ef0: the weave (BULLETS.md §3.7, M740.md §3.6)
            // base (+0x12c) += dir * speed; the matrix sits at base + side * f10 * sin(((int)f9 * age) mod 360 deg),
            // side = normalize(UP x dir) = normalize(dir.z, 0, -dir.x); then INTEGRATE.
            s.pos = {s.pos.x + s.dir.x * s.speed, s.pos.y + s.dir.y * s.speed, s.pos.z + s.dir.z * s.speed};
            Vec3 side{s.dir.z, 0.0f, -s.dir.x};
            const float l = float(std::sqrt(double(side.x * side.x + side.z * side.z)));
            side = l != 0.0f ? Vec3{side.x / l, 0.0f, side.z / l} : Vec3{};
            const std::uint32_t phi = std::uint32_t(int(s.f168) * int(b.age)) % 360u;
            const float off = float(std::sin(double(float(phi) * 3.1415927f / 180.0f))) * s.f16c;
            b.mat = ef2sim::translation(s.pos.x + side.x * off, s.pos.y, s.pos.z + side.z * off);
            (void)integrate(b);
            break;
        }
        case 0x14: case 0x19: case 0x2b:                           // none: the tail only
            break;
        default:
            if (s.kind > 0x2b) break;
            mover1();                                              // [STAND-IN] an undecoded kind
            break;
    }

    // ---- the tail (0x5953e6)
    if (s.f160 != 0 && b.age % s.f160 == 0) {                      // homing
        Vec3 target = env.player;
        if (s.kind == 4 || s.kind == 0x26 || s.kind == 0x27) target.y = s.pos.y;
        else if (s.kind == 0xd) target.y = 0.0f;
        else if (s.kind == 0xb || s.kind == 0xe) target = b.owner_pos;
        s.dir = home(s.pos, s.dir, target, s.f164);
    }
    s.speed += s.accel;
    if (s.speed < 0.0f) s.speed = 0.0f;
    b.age += 1;
    const bool parked = b.fx->kill;
    if (parked) s.speed = 0.0f;
    if (!parked && s.kind != 0x29 && s.f15c != 0 && b.age % s.f15c == 0) {   // the trail puff
        const float yaw = -1.5707964f - float(std::atan2(double(s.dir.z), double(s.dir.x)));
        if (s.f158 == 0x18c) sound(0x145);
        const Vec3 at{b.mat.m[3][0], b.mat.m[3][1], b.mat.m[3][2]};
        spawn_fx(int(s.f158), at, {0.0f, yaw, 0.0f}, b.side, b.atk, 0.0f, 0.0f, 0, 0, -1, s.owner);
    }
    if (s.kind != 0x10 && s.kind != 0x23 && s.kind != 0xd)        // the render orientation
        b.mat = ef2sim::mul(look_rotation(s.dir), b.mat);
}

}  // namespace cleria::bullet
