// Floor 5 (S_50) brains (build/ai_spec/M430.md, M440.md, M950.md, ...). Included by main.cpp after
// floor4.inl (it reuses floor4::Pool).

static void floor5_tests() {
    using floor4::Pool;
    // ---- M430 Zeruena: M570's template, the barrier inside 7 u, 2/4/6 cast-0x30 hands ----
    {
        const EnemyBrain m430 = enemy_brain_for(36), m440 = enemy_brain_for(37);
        check(m430 && m440 && revive::row(36), "Zeruena / its hand registered (AI 36 / 37); Zeruena revives");
        EnemyAiParams zp = params(0.1f, 0.036f, 1.0f, 1.75f, 1.0f);     // EI[33]
        zp.raw[9] = 0.03f; zp.raw[10] = 0.03f; zp.raw[11] = 0.15f; zp.raw[12] = 3.0f;
        {   // 5 u, not high, the target grounded: the barrier (section 6, 120 ticks, cooldown 165 + R%30).
            Pool w; Actor& Z = (w.actors.emplace_back(), w.actors.back());
            Z.instance_id = 47; Z.cast_id = 0x2f; Z.ai_index = 36; Z.flyer = true; Z.height = 1.5751f;
            Z.hp = Z.max_hp = 300; Z.level = 37;
            Rig r; r.env.actor = &Z; r.env.world = &w; r.env.target = {5.0f, 1.075f, 0}; r.env.target_grounded = true;
            int t6 = -1, cool = 0;
            float rate = 0;
            for (int t = 0; t < 300 && t6 < 0; ++t)
                if (enemy_ai_tick(Z.ai, zp, r.env, m430).section == 6) { t6 = t; cool = Z.ai.cool(); rate = Z.ai.f(0x814); }
            check(t6 >= 0 && Z.ai.i(0x72c) == 0x70 && Z.ai.dur() == 119 && cool >= 164 && cool <= 193 &&
                  std::fabs(rate - 0.03f) < 1e-6f,
                  "M430 near (5 u < 7): barrier — section 6, stand 120, cooldown 165 - 15d + R%30, turn x0.3");
            const int c0 = cool;
            int lock = -1;
            for (int t = 0; t < 400 && Z.ai.i(0x72c) == 0x70; ++t) {
                enemy_ai_tick(Z.ai, zp, r.env, m430);
                if (lock < 0 && Z.ai.action() == 1) lock = t;
            }
            std::printf("[ai]   M430 barrier: lock at %d, sub 0x%x, turn %.3f\n", lock, Z.ai.i(0x72c), Z.ai.f(0x814));
            check(lock >= 118 && lock <= 120 && Z.ai.i(0x72c) == 0 && std::fabs(Z.ai.f(0x814) - 0.1f) < 1e-6f,
                  "M430 barrier: heading lock after 120, sub 0 when the cooldown runs out, turn rate restored");
            (void)c0;
        }
        {   // 8 u (mid): the summon at Marker 2 — on Normal 2 pairs of cast 0x30, a pair at one point.
            Pool w; Actor& Z = (w.actors.emplace_back(), w.actors.back());
            Z.instance_id = 47; Z.cast_id = 0x2f; Z.ai_index = 36; Z.flyer = true; Z.height = 1.5751f;
            Z.hp = Z.max_hp = 300; Z.level = 37;
            Rig r; r.env.actor = &Z; r.env.world = &w; r.env.target = {8.0f, 1.075f, 0}; r.env.target_grounded = true;
            int s7 = -1;
            for (int t = 0; t < 400 && s7 < 0; ++t) if (enemy_ai_tick(Z.ai, zp, r.env, m430).section == 7) s7 = t;
            r.env.section = 7; r.env.aso_marker = 2;
            enemy_ai_tick(Z.ai, zp, r.env, m430);
            std::vector<Actor*> kids;
            for (Actor& c : w.actors) if (c.cast_id == 0x30) kids.push_back(&c);
            const float h = Z.ai.heading;
            const float th0 = h - (0.0f + 0.3f) * 3.14159f;                // pair 0: heading - 54 deg
            bool pair0 = kids.size() == 4 &&
                         std::fabs(kids[0]->pos.x - (Z.pos.x + float(std::cos(double(th0))) * 2.5f)) < 1e-4f &&
                         kids[0]->pos.x == kids[1]->pos.x && kids[0]->pos.z == kids[1]->pos.z &&
                         kids[0]->ai.i(0x7a0) == 0 && kids[1]->ai.i(0x7a0) == 1 && kids[2]->ai.f(0x7ac) > 3.0f;
            check(s7 >= 0 && pair0, "M430 summon: n = (d+2)/2 + 1 = 2 pairs of cast 0x30 at heading - (a + 0.3)pi, a pair at one point");
            // M440: M410's brain without DEF 999.
            Actor* H = kids.empty() ? nullptr : kids[0];
            if (H) {
                H->def = 50.0f;
                Rig hr; hr.env.actor = H; hr.env.world = &w; hr.env.target = {8.0f, 1.075f, 0};
                enemy_ai_tick(H->ai, params(0.1f, 0.045f, 1.0f, 2.0f, 0.0f), hr.env, m440);
            }
            check(H && H->def == 50.0f && H->ai.phase() >= 1 && !H->ai.hittable && H->ai.alpha == 0.0f,
                  "M440 init: invisible and unhittable, but its DEF is its own (no 999, unlike M410)");
        }
    }
    // ---- M400 Tolpi: the spray — a kind-1 pollen bullet every 2nd tick while Marker 1 ----
    {
        const EnemyBrain m400 = enemy_brain_for(33);
        check(m400 != nullptr, "Tolpi brain registered (AI 33)");
        EnemyAiParams tp = params(0.1f, 0.0075f, 1.0f, 2.0f, 0.0f);
        Actor T; T.cast_id = 44; T.ai_index = 33;
        Rig r; r.env.actor = &T; r.env.target = {6.0f, 1.075f, 0};
        int s7 = -1;
        for (int t = 0; t < 400 && s7 < 0; ++t) if (enemy_ai_tick(T.ai, tp, r.env, m400).section == 7) s7 = t;
        const int cool = T.ai.cool();
        r.env.section = 7; r.env.aso_marker = 1;
        int shots = 0; bool ok = true;
        for (int t = 0; t < 20; ++t) {
            const AiOut o = enemy_ai_tick(T.ai, tp, r.env, m400);
            for (const AiBullet& b : o.bullets) {
                ++shots;
                ok &= b.kind == 1 && b.effect == 303 && std::fabs(b.speed - 0.16f) < 1e-6f &&
                      std::fabs(b.accel + 0.0016f) < 1e-6f && b.f168 == 1.0f;
            }
        }
        check(s7 >= 0 && cool >= 208 && cool <= 237 && shots == 10 && ok,
              "M400: spray at 6 u, cooldown 209 + R%30; one pollen shot (EF2 303, 0.16, -1%/tick) every 2nd tick");
    }
    // ---- M660 Urobass: the strikes on Normal — 4 (the centre + a ring of 3 at 3 u) ----
    {
        const EnemyBrain m660 = enemy_brain_for(59);
        check(m660 != nullptr, "Urobass brain registered (AI 59)");
        EnemyAiParams up = params(0.08f, 0.02f, 1.0f, 2.0f, 0.0f);
        Pool w; Actor& U = (w.actors.emplace_back(), w.actors.back());
        U.instance_id = 70; U.cast_id = 70; U.ai_index = 59; U.height = 1.3251f;
        Rig r; r.env.actor = &U; r.env.world = &w; r.env.target = {5.0f, 1.075f, 0};
        enemy_ai_tick(U.ai, up, r.env, m660);
        enemy_ai_tick(U.ai, up, r.env, m660);
        r.env.section = 7; r.env.aso_marker = 1;
        const AiOut o = enemy_ai_tick(U.ai, up, r.env, m660);
        bool ring = o.bullets.size() == 4 && o.section == 8 && o.section_force;
        for (std::size_t i = 1; ring && i < o.bullets.size(); ++i) {
            const Vec3 q = o.bullets[i].pos, c = o.bullets[0].pos;
            ring = std::fabs(std::hypot(q.x - c.x, q.z - c.z) - 3.0f) < 1e-4f && o.bullets[i].effect == 494 &&
                   o.bullets[i].trail_effect == 495 && o.bullets[i].trail_period == 45;
        }
        check(ring && U.ai.i(0x79c) == 45, "M660 cast at Marker 1: force 8, 4 strikes (EF2 494, trail 495 at 45) in a 3-u ring; SE in 45");
    }
    // ---- M950 Nooze (Green): M910 with other constants (build/ai_spec/M950.md) ----
    {
        const EnemyBrain m950 = enemy_brain_for(88);
        check(m950 != nullptr, "Nooze (Green) brain registered (AI index 88)");
        EnemyAiParams np = params(0.05f, 0.025f, 1.0f, 3.2f, 0.0f);
        np.raw[6] = 0.1f; np.raw[7] = 0.15f;
        bool split_ok = false;
        for (int s = 0; s < 400 && !split_ok; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {6.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            enemy_ai_tick(ai, np, r.env, m950);
            r.env.hp = 80; r.env.section = 4; r.env.aso_marker = 9; r.env.pool_count = 29;
            const AiOut o = enemy_ai_tick(ai, np, r.env, m950);
            // chunk = max(0.31*100, 80 - 0.6*80 = 32) = 32 -> both 48; the child is 0xb5
            if (!o.spawns.empty()) split_ok = o.set_hp == 48.0f && o.spawns[0].hp == 48.0f && o.spawns[0].cast == 0xb5;
        }
        check(split_ok, "M950: splits into 0xb5 under the pool cap 30, chunk max(0.31 max, 0.4 h): 80 -> 48 each");
        int lunge = 0, far_lunge = 0;
        for (int s = 0; s < 60; ++s) {
            for (float dist : {5.3f, 5.8f}) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {dist, 1.075f, 0}; r.env.section = 2; r.env.hp = 50; r.env.max_hp = 50;
                bool l = false;
                for (int t = 0; t < 200 && !l; ++t) l = enemy_ai_tick(ai, np, r.env, m950).section == 7;
                (dist < 5.5f ? lunge : far_lunge) += l;
            }
        }
        check(lunge > 30 && far_lunge == 0, "M950 lunges inside 5.5 u (M910: 4.0), never beyond");
    }
}
