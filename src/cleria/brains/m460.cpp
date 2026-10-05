// M460 — the bat-swarm familiar boss ("Vagullion" by game knowledge; the data names it
// nothing). Handler FUN_004c9880, brain FUN_004c7020 (AI index 39). Spec:
// build/ai_spec/M460.md. Only ever spawned by event scripts (S_1080, S_2080).
//
// The cycle: hold 90-30d ticks; dissolve (section 8) into 32 Vagully in a formation turned
// to its sprite direction, fade out and fly invisibly between wall-side waypoints, now and
// then agitating the flock; after 400 ticks recall the flock (each bat docks and reports
// +0x7c0 = 2), then re-scatter (at most twice) or reform (section 21) and fade in; wait
// 240-30d+R%120 ticks (or until hit more than lim(d) times) and attack once — a clawing
// rush (300), a rush with shots (301) or a gas breath (7) — then dissolve again. Below
// half HP it may split into two bodies of 16 bats sharing one HP pool.
// The handler's extras live here too: the partner HP link, the hit counter, the bats'
// crowding fade, the unconditional arena clamp, Durable after lim(d) hits, and (via the
// death hook) killing the flock when it dies.
// [not modelled] the keep-out box restore (FUN_0042dc90); FUN_004202e0's stat recompute at
// INIT (the spawn already computed this cast's stats); effect attachment (FUN_00451c20 —
// the claw slashes are placed, not parented); the player-dead helper's forced section 2;
// the R() draw FUN_00459410 may make inside a child spawn.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr int kSelf = 0x32, kBat = 0xb7;
constexpr float kDeg = 0.017453292f;

std::uint32_t lim(int d) { return std::uint32_t(12 - (d >= 0 ? d : 2 * d)); }

// The crowding fade (handler #10): bats closer than 1 u to each other dim, down to 0.3.
void separation_pass(Actor& A, AiWorld& w) {
    std::vector<Actor*> L;
    w.for_each([&](Actor& b) {
        if (b.cast_id == kBat && b.hp >= 1.0f && b.ai.i(0x854) == A.instance_id) L.push_back(&b);
    });
    std::vector<float> acc(L.size(), 0.0f);
    for (std::size_t i = 0; i < L.size(); ++i)
        for (std::size_t j = i + 1; j < L.size(); ++j) {
            const float dz = L[i]->pos.z - L[j]->pos.z, dx = L[i]->pos.x - L[j]->pos.x;
            const float d2 = dz * dz + dx * dx;
            if (d2 > 1.0f) continue;
            const float dd = std::sqrt(d2);
            if (dd >= 1.0f) continue;
            const float wgt = (1.0f - dd) * 0.25f;
            acc[i] += wgt; acc[j] += wgt;
        }
    for (std::size_t i = 0; i < L.size(); ++i) {
        const float v = std::min(acc[i], 0.7f);
        L[i]->tint_alpha = float(std::uint8_t(int((1.0f - v) * 255.0f))) / 255.0f;
    }
}

}  // namespace

void m460_dead(Actor& A, AiWorld& w) {                             // handler #5
    const int partner = A.ai.i(0x854);
    w.for_each([&](Actor& b) {
        if (b.cast_id == kBat && b.hp >= 1.0f &&
            (b.ai.i(0x854) == A.instance_id || (partner != 0 && b.ai.i(0x854) == partner)))
            b.hp = 0.0f;
    });
}

AiOut m460(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* Ap = e.actor;
    AiWorld* w = e.world;
    if (!Ap || !w) return out;
    Actor& A = *Ap;
    const int d = difficulty(e);
    const float fd = float(d);
    const int ch = e.character;

    // ---- handler extras that precede the brain ----
    if (Actor* q = w->find(a.i(0x854))) {                          // #3: one HP pool
        const float h = std::min(q->hp, A.hp);
        A.hp = h; q->hp = h;
    }
    if (e.was_hit) a.i(0x7bc) += 1;                                // #11: hits taken
    {   float r[4]; w->arena(r);                                   // #12: unconditional clamp
        if (A.pos.x < r[0]) A.pos.x = r[0]; else if (A.pos.x > r[2]) A.pos.x = r[2];
        if (A.pos.z < r[1]) A.pos.z = r[1]; else if (A.pos.z > r[3]) A.pos.z = r[3]; }
    separation_pass(A, *w);                                        // #10
    if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }   // #9: FUN_0047aa80

    if (e.launched) return out;                                    // +0x4d0
    const float ratio = a.f(0x7d0) != 0 ? A.scale / a.f(0x7d0) : 1.0f;
    const Vec3 pos = e.self;                                       // the origin, +0x50
    auto steer_to = [&](int mode) { const int s = a.steer(); a.steer() = mode; ai::steer(a, e); a.steer() = s; };
    bool durable = e.durable;
    auto tail_lite = [&] {
        if (std::uint32_t(a.i(0x7bc)) > lim(d)) { out.set_durable = 1; durable = true; }
        A.def = durable ? a.f(0x7d8) * (ch == 2 ? 1.6f : 1.5f) : a.f(0x7d8);
        return out;
    };

    if (a.phase() == 0) {                                          // ---- INIT
        if (ch == 2) {
            A.def *= 1.05f; A.hp *= 1.05f; A.atk += 5.0f;
            if (d > 0) { A.atk *= 0.98f; A.def *= 0.965f; A.hp *= 0.965f; }
            A.max_hp = A.hp;
        }
        ai::init(a, p, e);
        A.flyer = true;
        a.i(0x70c) = 0; a.i(0x728) = 0; a.i(0x72c) = 32;
        a.i(0x730) = int(r15(e) % 360u);
        a.i(0x79c) = int(r15(e) & 1u);
        a.i(0x7a0) = 0; a.i(0x7a4) = 1; a.i(0x7a8) = 0;
        a.i(0x7bc) = 0; a.i(0x7c0) = 0; a.i(0x7c4) = 0; a.i(0x7c8) = 0;
        a.f(0x7ac) = A.radius; a.f(0x7b0) = 0.0f; a.f(0x7b4) = A.height; a.f(0x7b8) = 0.0f;
        a.f(0x7d0) = A.scale; a.f(0x7d4) = 0.0f; a.f(0x7d8) = A.def;
        a.i(0x854) = 0;
        ai::acquire(a);
        a.cool() = 90 - std::min(30 * d, 90);
        w->for_each([&](Actor& o) {                                // partner search
            if (a.i(0x854) != 0 || &o == &A) return;
            if (o.cast_id == kSelf && o.ai.i(0x854) == A.instance_id) {
                A.scale *= 0.75f; a.i(0x72c) = 16; a.i(0x854) = o.instance_id; a.i(0x728) = 0x28;
                a.alpha = 0.0f; a.i(0x7a4) = 0; a.i(0x7a8) = 3;
            }
        });
        return tail_lite();
    }
    if (a.phase() == 1) { ai::idle(a, p, e); return tail_lite(); }
    if (a.phase() != 2 && a.phase() != 4) return tail_lite();

    // ---- MAIN ----
    a.phase() = 4;
    int& sub = a.i(0x728);
    auto start_summon = [&] { sub = 1; out.section = 8; out.set_marker_block = 1; };
    auto each_bat = [&](bool need_alive, const std::function<void(Actor&)>& f) {
        w->for_each([&](Actor& b) {
            if (b.cast_id == kBat && (!need_alive || b.hp >= 1.0f) && b.ai.i(0x854) == A.instance_id) f(b);
        });
    };
    auto reseed_bat = [&](Actor& b) {                              // speeds/turn/hover overrides
        b.ai.f(0x818) = swarm_speed(d, ch == 2);
        b.ai.f(0x814) = 0.15f; b.ai.f(0x840) = 0.2f; b.ai.f(0x83c) = 0.1f; b.ai.f(0x838) = 0.1f;
    };
    switch (sub) {
        case 0:                                                    // initial hold
            a.action() = 2; a.steer() = 0;
            if (a.cool() == 0) start_summon();
            break;
        case 1: {                                                  // dissolve into bats
            a.action() = 1; a.steer() = 0;
            if (e.section != 8) { out.section = 8; out.set_marker_block = 1; }
            int m = e.aso_marker;
            if (m == 1) {
                const float s = A.scale;
                out.effects.push_back({269, {pos.x, pos.y - s, pos.z}, 3.0f * s, false});   // z_warp
                out.set_marker = 2; out.set_marker_block = 3;
                const float base = float(e.sprite_dir + 2) * 45.0f * kDeg;
                const float* raw87 = w->enemy_raw(kBat);
                for (int i = 0; i < a.i(0x72c); ++i) {
                    const int q = i % 16;
                    float ax, ay, t;
                    if (a.i(0x72c) == 32) {
                        ax = float(q % 4) - 1.5f; ay = float(q / 4) - 1.5f;
                        t = (unit(e.rng()) - 0.5f) + float(i / 16) - 0.5f;
                    } else {
                        const int k = i % 8;
                        if (k < 6) { ax = float(k % 3) - 0.5f; ay = float(k / 3) - 0.5f; }
                        else       { ax = float(7 - k) - 0.25f; ay = -0.5f; }
                        t = (unit(e.rng()) - 0.5f) + float(i / 8) - 0.3f;
                    }
                    const float len = float(std::sqrt(double(t * t + ax * ax)));
                    const float ang = float(std::atan2(double(t), double(ax))) + base;
                    const Vec3 off{std::cos(ang) * len, ay, std::sin(ang) * len};
                    Actor* b = w->spawn_child(A, kBat, off, A.level, false);
                    if (!b) continue;
                    if (i == 0) b->aso_local15 = 1;
                    EnemyAi& B = b->ai;
                    ai::init_block(B, {b->pos.x, b->pos.y + b->height, b->pos.z}, raw87);
                    B.hittable = false; B.alpha = 0.0f; b->def = 999.0f;
                    if (ch == 2) b->atk = b->atk - (fd * (d < 0 ? 1.25f : 8.5f) - 3.5f);
                    else b->atk = b->atk - ((d < 0 ? fd * 1.25f : fd - float(d * d) * 0.5f) + 2.0f);
                    b->no_min_damage = true;
                    if (d < 0) b->atk = (ch == 2 ? fd * 5.0f + 10.0f : fd * 6.0f + 16.0f) + b->atk;
                    reseed_bat(*b);
                    B.phase() = 4; B.i(0x728) = 0;
                    B.i(0x7bc) = int(r15(e) % 15u);
                    B.f(0x7ac) = B.f(0x834);
                    B.set_v(0x808, pos);
                    B.i(0x70c) = 0;
                    const float yb = ((pos.y - A.height) - A.floor_y) + ay + 1.8f;
                    B.f(0x7f0) = ax * 0.75f; B.f(0x7f4) = yb; B.f(0x7f8) = t * 0.75f;
                    B.f(0x834) = yb;
                    b->floor_y = A.floor_y;
                    b->pos.y = yb + A.floor_y - b->height;         // origin = yb + floor
                    b->flyer = true;
                }
                m = 2;
            }
            if (m < 2) break;
            a.hittable = false;                                    // fade the body out
            if (a.alpha > 0.0f) { a.alpha = std::max(0.0f, a.alpha - 0.025f); break; }
            a.i(0x7a0) = 0;
            a.i(0x79c) = int(r15(e) & 1u);
            sub = 10; a.action() = 0x400; a.steer() = 4;
            a.set_v(0x894, pos); a.i(0x87c) = 0; a.set_v(0x7e4, pos);
            out.section = 2; out.section_force = true;             // FUN_00446620(2)
            break;
        }
        case 10: {                                                 // pick a wander waypoint
            int s = int(r15(e) % 15u) + 5;
            if (a.i(0x79c) != 0) s = -s;
            const std::uint32_t ang_deg = std::uint32_t(a.i(0x730) + s) % 360u;   // unsigned wrap
            a.i(0x730) = int(ang_deg);
            const float ang = float(ang_deg) * kDeg;
            const Vec3 O{pos.x, A.floor_y + 0.5f, pos.z};
            AiWorld::Ray Q = w->ray(O, {std::cos(ang) * 100.0f, 0.0f, std::sin(ang) * 100.0f});
            if (!Q.hit) break;
            float rect[4]; w->arena(rect);
            bool gave_up = false;
            for (;;) {
                Vec3 H = Q.point;
                const float mrg = A.radius + 0.1f;                 // FUN_0047ab70
                bool clamped = false;
                if (H.x < rect[0]) { H.x = rect[0] + mrg; clamped = true; }
                if (rect[2] < H.x) { H.x = rect[2] - mrg; clamped = true; }
                if (H.z < rect[1]) { H.z = rect[1] + mrg; clamped = true; }
                if (rect[3] < H.z) { H.z = rect[3] - mrg; clamped = true; }
                if (!clamped) break;
                Q = w->ray(O, {H.x - pos.x, 0.0f, H.z - pos.z});
                if (!Q.hit) { gave_up = true; break; }
            }
            if (gave_up) break;
            const Vec3 H = Q.point;
            const float nx = Q.normal.x, nz = Q.normal.z;
            if (0.1f > std::fabs(nx) + std::fabs(nz)) break;       // floor/ceiling: retry
            const float nl = std::sqrt(nx * nx + nz * nz);
            Vec3 G{H.x + nx / nl * (A.radius + 0.1f), H.y, H.z + nz / nl * (A.radius + 0.1f)};
            int k = 0;
            for (; k < 3; ++k) {
                const Vec3 S{G.x, A.floor_y + 0.1f + A.radius, G.z};
                if (!w->sphere_hits(S, A.radius)) break;
                G = {pos.x + (S.x - pos.x) * 0.9f, S.y, pos.z + (S.z - pos.z) * 0.9f};
            }
            if (k == 3) break;
            a.set_v(0x894, G);
            sub = 0xb; a.steer() = 4; a.action() = 0x800;
            a.set_v(0x7fc, pos);
            steer_to(4);
            ai::turn(a, a.f(0x814) * 100.0f);
            break;
        }
        case 0xb: {                                                // fly to the waypoint
            Actor* q = w->find(a.i(0x854));
            float dx = 10.0f, dz = 10.0f;
            if (q) { dx = q->pos.x - A.pos.x; dz = q->pos.z - A.pos.z; }
            const std::uint32_t t = std::uint32_t(a.i(0x87c));
            bool keep = false;
            if (t > 400u && (!q || dz * dz + dx * dx >= 9.0f)) {   // recall the flock
                steer_to(0);
                ai::turn(a, a.f(0x814) * 100.0f);
                a.steer() = 1; a.action() = 1; sub = 0x14;
                keep = true;
            }
            if (!keep) {
                if (t % 30u == 0 && int(r15(e) % 1000u) < (d + 3) * 50)
                    w->for_each([&](Actor& b) {
                        if (b.cast_id == kBat && b.ai.i(0x854) == A.instance_id) b.ai.i(0x7c4) = 2 * d + 12;
                    });
                const float ex = a.f(0x7fc) - pos.x, ez = a.f(0x804) - pos.z;
                const float wx = a.f(0x894) - pos.x, wz = a.f(0x89c) - pos.z;
                if (std::sqrt(wx * wx + wz * wz) >= 1.0f && std::sqrt(ex * ex + ez * ez) >= 0.01f &&
                    (wx * a.f(0x748) + 0.0f + wz * a.f(0x750)) >= 0.0f) {
                    keep = true;                                   // still en route
                }
            }
            if (!keep) {                                           // arrived, stuck or overshot
                const float hx = a.f(0x7e4) - pos.x, hz = a.f(0x7ec) - pos.z;
                if (std::sqrt(hx * hx + hz * hz) < 3.0f || t < 150u || (e.rng() & 0x20000u) != 0) sub = 10;
                else a.i(0x87c) = 401;
                bool split = false;
                if (A.hp < A.max_hp * 0.5f && a.i(0x72c) == 32 &&
                    int(r15(e) % 1000u) < (d + 15) * 50) {          // SPLIT
                    a.i(0x72c) = 16;
                    A.scale = a.f(0x7d0) * 0.75f;
                    if (Actor* np = w->spawn_child(A, kSelf, {0, 0, 0}, A.level, true)) {
                        a.i(0x854) = np->instance_id;
                        np->ai.i(0x854) = A.instance_id;           // FUN_00476d70 step 4
                        a.i(0x7a8) = 3;
                        int n = 0;
                        w->for_each([&](Actor& b) {                // ANY live Vagully
                            if (b.cast_id == kBat && b.hp >= 1.0f && ++n >= 16) b.ai.i(0x854) = np->instance_id;
                        });
                    }
                    split = true;
                }
                if (!split && A.hp < A.max_hp * 0.5f && a.i(0x72c) == 16) {
                    Actor* qq = w->find(a.i(0x854));
                    if (int(r15(e) % 1000u) < (4 - d) * 100 && a.i(0x7a8) == 0 && qq &&
                        qq->ai.i(0x7a8) == 0 && (qq->ai.i(0x728) == 10 || qq->ai.i(0x728) == 0xb)) {
                        Actor* o;                                  // MERGE (believed unreachable)
                        if (a.i(0x7a4) != 0) {
                            w->remove(*qq);
                            a.i(0x854) = 0; a.i(0x72c) = 32; a.i(0x7a8) = 3; A.scale = a.f(0x7d0); o = &A;
                        } else {
                            w->remove(A);
                            qq->ai.i(0x854) = 0; qq->ai.i(0x72c) = 32; qq->ai.i(0x7a8) = 3;
                            qq->scale = a.f(0x7d0); o = qq;
                        }
                        const int oid = o->instance_id;
                        w->for_each([&](Actor& b) { if (b.cast_id == kBat) b.ai.i(0x854) = oid; });
                    }
                }
            }
            a.set_v(0x7fc, pos);                                   // KEEP
            break;
        }
        case 0x14: {                                               // recall the bats
            int total = 0, ready = 0;
            each_bat(true, [&](Actor& b) {
                ++total;
                if (b.ai.i(0x7c0) == 0) { b.ai.i(0x7c0) = 1; b.ai.i(0x87c) = 0; }
                else if (b.ai.i(0x7c0) == 2) ++ready;
            });
            if (ready != total) break;
            if (std::uint32_t(a.i(0x7a0)) < 2u) {
                const int thr = A.hp < A.max_hp * 0.5f ? d * 50 + 125 : (d * d + 4) * 25;
                if (int(r15(e) % 1000u) < thr) {                   // RE-SCATTER
                    each_bat(true, [&](Actor& b) {
                        EnemyAi& B = b.ai;
                        B.hittable = true;                         // +0x60 = 0x88
                        b.def = 999.0f; b.no_min_damage = true;
                        reseed_bat(b);
                        B.phase() = 4; B.i(0x7bc) = 0; B.i(0x7c0) = 0; B.i(0x70c) = 0;
                        B.steer() = 0; B.action() = 1; B.i(0x728) = 1; B.f(0x7cc) = 9999.0f;
                    });
                    a.i(0x87c) = 0; sub = 10; a.i(0x7a0) += 1;
                    break;
                }
            }
            sub = 0x15;                                            // REFORM
            out.section = 0x15;
            a.i(0x7bc) = 0;
            out.effects.push_back({269, {pos.x, A.height + A.floor_y, pos.z}, 3.0f * A.scale, false});
            each_bat(true, [&](Actor& b) { b.ai.i(0x7c0) = 3; });
            break;
        }
        case 0x15:                                                 // fade in
            a.hittable = false;
            if (a.alpha < 1.0f) { a.alpha = std::min(a.alpha + 0.04f, 1.0f); break; }
            sub = 0x1e; a.i(0x87c) = 0;
            a.cool() = int(r15(e) % 120u) - 30 * d + 240;
            break;
        case 0x1e: {                                               // visible: choose an attack
            a.steer() = 0; a.action() = 2;
            if (a.cool() != 0 && std::uint32_t(a.i(0x7bc)) <= lim(d)) break;
            steer_to(0);
            ai::turn(a, a.f(0x814) * 10.0f);
            const std::uint32_t r = r15(e);
            int pick;                                              // 0 gas/none, 1 rush A, 2 rush B
            if (ch == 2) pick = (r % 3u == 0) ? 2 : 0;
            else pick = ((r & 7u) == 0 || a.dist() < 3.0f) ? 1 : 0;
            if (pick == 0) {                                       // ALT
                if ((r15(e) & 3u) == 1 && (r15(e) & 3u) != 0 && a.dist() < 8.0f) pick = 2;
            }
            if (pick == 0) {
                if (r15(e) % std::uint32_t(2 * d + 6) == 0) {       // no attack: dissolve again
                    sub = 1; out.section = 8; out.set_marker_block = 1; a.i(0x87c) = 0;
                    break;
                }
                sub = 0x46; out.section = 7; out.section_force = true; a.i(0x7c0) = 0;
            } else if (pick == 1) {
                sub = 0x32; out.section = 300; out.section_force = true;
            } else {
                sub = 0x3c; out.section = 301; out.section_force = true;
            }
            out.attack = true;
            out.set_durable = 1; durable = true; a.i(0x87c) = 0;
            break;
        }
        case 0x28:                                                 // the split partner's first tick
            a.i(0x87c) = 0; sub = 10; a.i(0x7a0) += 1;
            break;
        case 0x32: case 0x3c: {                                    // the rushes (300 / 301)
            if (e.section != 300 && e.section != 301) {
                if (std::uint32_t(a.i(0x87c)) >= 10u) { start_summon(); break; }
                out.section = sub == 0x32 ? 300 : 301; out.section_force = true;
                break;
            }
            a.action() = 1; a.steer() = 0;
            const std::uint32_t f = std::uint32_t(e.aso_frame);
            if (f > 3u) {
                a.action() = 0x800; a.steer() = 1;                 // dash along the heading
                if (f == 6) a.action() = 0x20; else if (f == 7) a.action() = 1;
                float a1 = 0, ang = 0, vo = 0;
                if (f == 4)      { a1 = ratio * 1.25f; ang = -100.0f; vo = ratio * -0.5f; }
                else if (f == 5) { a1 = ratio * 1.75f; ang = -65.0f;  vo = ratio * -1.5f; }
                else if (f == 6) { a1 = ratio * 2.25f; ang = -15.0f;  vo = ratio * -1.5f; }
                AiHitbox h;
                h.radius = a1; h.fwd = 1.75f; h.angle_deg = ang; h.voff = vo;   // [1] r, [2] fwd
                h.life = 6; h.rehit = 32; h.rtype = 2; h.power = 12.0f; h.knock = 5.0f;
                out.hitboxes.push_back(h);
            } else if (f < 2u) {
                a.action() = 2;                                    // frames 0,1 turn
            }
            if (ch == 2 && e.aso_frame == 7) {
                a.action() = 2; a.steer() = 0;
                steer_to(0);
                ai::turn(a, a.f(0x814) * 2.0f);
            }
            if (const int m = e.aso_marker; m != 0) {              // Marker(298): the claw slashes
                out.set_marker = 0;
                float lx = 0;
                for (int k = 0; k < 3; ++k) {
                    out.effects.push_back({m, {pos.x + std::cos(a.heading) * lx, pos.y, pos.z + std::sin(a.heading) * lx},
                                           ratio, false});
                    lx -= ratio * 0.8f;
                }
            }
            break;
        }
        case 0x46: {                                               // the gas breath (section 7)
            a.action() = 1; a.steer() = 1;
            if (e.section != 7) {
                if (std::uint32_t(a.i(0x87c)) >= 10u) { start_summon(); break; }
                out.section = 7; out.section_force = true;
                break;
            }
            if (e.aso_marker != 0) { a.i(0x87c) = 0; a.i(0x7c0) = e.aso_marker; out.set_marker = 0; }
            const int g = a.i(0x7c0);
            const int t = a.i(0x87c);
            if (g == 0) { a.i(0x87c) = 0; break; }
            if (g == 999) {                                        // Hugo: re-aim between sweeps
                a.steer() = 0; a.action() = 2;
                steer_to(0);
                ai::turn(a, (fd * 0.2f + 0.4f) * a.f(0x814));
                a.action() = 1; a.steer() = 1;
                break;
            }
            if ((t & 1) == 0) break;
            auto puff = [&](float deg_per_tick, float spd_base, float accel, int trail) {
                const float spd = ratio * 2.25f;
                const float ang = a.heading + ((float(t) - 20.0f) * deg_per_tick) * kDeg;
                const float c = std::cos(ang), s = std::sin(ang);
                AiBullet bl;
                bl.effect = g;
                bl.pos = {pos.x + c * spd, pos.y + 0.0f, pos.z + s * spd};
                bl.dir = {c, -0.6f, s};                            // normalised in xz, then y = -0.6
                bl.speed = (d > 0 ? fd * 0.025f : fd * 0.01f) + spd_base;
                bl.accel = accel;
                bl.knock = 5.0f; bl.f168 = 1.0f;                  // a11 0 / a12 5.0
                bl.trail_effect = trail > 0 ? g : 0; bl.trail_period = trail;
                bl.gas = true;
                out.bullets.push_back(bl);
                return bl.pos;
            };
            const int trail = d == 3 ? 15 : d == 2 ? 27 : d == 1 ? 45 : 0;
            const Vec3 p1 = puff(2.0f, 0.15f, -0.0008f, trail);
            a.f(0x7c8) = p1.y - ratio * 1.3f;                      // the gas floor
            if (ch == 2 && (t & 3) == 1) puff(3.0f, 0.3f, -0.0014f, 0);
            break;
        }
        default:
            break;
    }

    // ---- TRAILER ----
    if (sub != 1 && sub != 0x15) a.hittable = a.alpha >= 0.5f;
    const float origin_y = A.pos.y + A.height;                     // keep +0x54 while +0x324 changes
    if (a.alpha == 0.0f) {
        const float r = ratio * (a.f(0x7ac) * 1.5f);
        A.radius = r;
        const float dl = r - a.f(0x7ac);
        A.height = a.f(0x7d4) + dl;
    } else {
        const float r = ratio * a.f(0x7ac);
        A.radius = r;
        const float dl = r - a.f(0x7ac);
        const float h = a.f(0x7b4) + dl;
        A.height = h;
        const float bob = std::sin(a.f(0x848) * kDeg) * a.f(0x840);
        float y = origin_y;
        if ((y - h) - A.floor_y > bob) y = h + A.floor_y + bob;   // altitude cap
        A.pos.y = y - A.height;
    }
    if (a.alpha == 0.0f) A.pos.y = origin_y - A.height;
    if (a.f(0x880) != A.hp) { a.f(0x880) = A.hp; out.gauge_hp = A.hp; }
    a.dur() = -1;
    ai::steer(a, e);
    ai::turn(a, a.f(0x814) * (sub == 0xb ? 100.0f : 1.0f));
    if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
    ai::act(a, p, e);
    return tail_lite();
}

}  // namespace cleria::brains
