// Floor 6 (S_60) brains (build/ai_spec/M960.md, ...). Included by main.cpp after floor5.inl.

static void floor6_tests() {
    // ---- M960 Nooze (Purple): the Nooze routine with no difficulty term (build/ai_spec/M960.md) ----
    {
        const EnemyBrain m960 = enemy_brain_for(89);
        check(m960 != nullptr, "Nooze (Purple) brain registered (AI index 89)");
        EnemyAiParams np = params(0.06f, 0.06f, 1.0f, 3.0f, 0.0f);   // EI[86]
        np.raw[6] = 0.1f; np.raw[7] = 0.15f;
        bool split_ok = false, capped = true;
        for (int s = 0; s < 400 && !split_ok; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {7.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            enemy_ai_tick(ai, np, r.env, m960);
            r.env.hp = 80; r.env.section = 4; r.env.aso_marker = 9; r.env.pool_count = 35;
            const AiOut o = enemy_ai_tick(ai, np, r.env, m960);
            // chunk = max(0.31*100, 80 - 0.625*80 = 30) = 31 -> both 49; the child is 0xb6
            if (!o.spawns.empty()) split_ok = o.set_hp == 49.0f && o.spawns[0].hp == 49.0f && o.spawns[0].cast == 0xb6;
        }
        for (int s = 0; s < 200; ++s) {                               // pool 36: never
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {7.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            enemy_ai_tick(ai, np, r.env, m960);
            r.env.hp = 80; r.env.section = 4; r.env.aso_marker = 9; r.env.pool_count = 36;
            capped = capped && enemy_ai_tick(ai, np, r.env, m960).spawns.empty();
        }
        check(split_ok && capped, "M960: splits into 0xb6 below the pool cap 36, chunk max(0.31 max, 0.375 h): 80 -> 49 each");
        int lunge = 0, far_lunge = 0;
        for (int s = 0; s < 60; ++s) {
            for (float dist : {5.8f, 6.3f}) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {dist, 1.075f, 0}; r.env.section = 2; r.env.hp = 50; r.env.max_hp = 50;
                bool l = false;
                for (int t = 0; t < 200 && !l; ++t) l = enemy_ai_tick(ai, np, r.env, m960).section == 7;
                (dist < 6.0f ? lunge : far_lunge) += l;
            }
        }
        check(lunge > 30 && far_lunge == 0, "M960 lunges inside 6.0 u (M950: 5.5), never beyond");
        // The cooldown ignores the difficulty: 60..119 on Easy and on Nightmare.
        bool cool_ok = true;
        int lo = 1000, hi = -1;
        for (int diff : {-2, 3}) {
            for (int s = 0; s < 80; ++s) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {3.0f, 1.075f, 0}; r.env.section = 2; r.env.hp = 50; r.env.max_hp = 50;
                r.env.difficulty = diff;
                for (int t = 0; t < 300; ++t)
                    if (enemy_ai_tick(ai, np, r.env, m960).section == 7) {
                        const int c = ai.cool();
                        lo = std::min(lo, c); hi = std::max(hi, c);
                        cool_ok = cool_ok && c >= 59 && c <= 119;
                        break;
                    }
            }
        }
        std::printf("[aitest]   M960 lunge cooldown range %d..%d\n", lo, hi);
        check(cool_ok && hi > lo, "M960 lunge cooldown R%60 + 60 on every difficulty (no d term)");
    }
    // ---- M670 Bazeraena: M430 + cast-0x1e hands 8.5 u out + the dash (build/ai_spec/M670.md) ----
    {
        using floor4::Pool;
        const EnemyBrain m670 = enemy_brain_for(60);
        check(m670 != nullptr && revive::row(60), "Bazeraena registered (AI 60); it revives");
        EnemyAiParams zp = params(0.13f, 0.0666f, 1.0f, 2.0f, 1.5f);   // EI[57]
        zp.raw[9] = 0.03f; zp.raw[10] = 0.03f; zp.raw[11] = 0.15f; zp.raw[12] = 3.0f;
        Pool w; Actor& Z = (w.actors.emplace_back(), w.actors.back());
        Z.instance_id = 71; Z.cast_id = 0x47; Z.ai_index = 60; Z.flyer = true; Z.height = 1.5751f;
        Z.hp = Z.max_hp = 300; Z.level = 41;
        Rig r; r.env.actor = &Z; r.env.world = &w; r.env.target = {7.0f, 1.075f, 0}; r.env.target_grounded = true;
        int s7 = -1;
        for (int t = 0; t < 400 && s7 < 0; ++t) if (enemy_ai_tick(Z.ai, zp, r.env, m670).section == 7) s7 = t;
        const bool backup = Z.ai.f(0x7b4) == 0.0666f;
        r.env.section = 7; r.env.aso_marker = 2;
        enemy_ai_tick(Z.ai, zp, r.env, m670);
        std::vector<Actor*> kids;
        for (Actor& c : w.actors) if (c.cast_id == 0x1e) kids.push_back(&c);
        const float th0 = Z.ai.heading - (0.0f + 0.3f) * 3.14159f;
        const bool ok = kids.size() == 2 &&
                        std::fabs(kids[0]->pos.x - (Z.pos.x + float(std::cos(double(th0))) * 8.5f)) < 1e-4f &&
                        kids[0]->ai.i(0x7a0) == 0 && kids[1]->ai.i(0x7a0) == 1 && kids[0]->ai.i(0x7a4) == 0x6e &&
                        kids[0]->ai.f(0x7b0) == 7.0f;
        check(s7 >= 0 && backup && ok, "M670 summon (Normal): 1 pair of cast 0x1e 8.5 u out, +0x7a4 = 0x6e, +0x7b0 = 7.0");
        // The guard hold with both sides alive: inside 5 + 1.5d while section 300 shows -> the dash.
        r.env.aso_marker = 0; r.env.section = 300; r.env.target = {4.0f, 1.075f, 0};
        Z.ai.i(0x72c) = 0x67; Z.ai.dur() = 0; Z.ai.action() = 2;
        enemy_ai_tick(Z.ai, zp, r.env, m670);                          // the action player's +0x734 catches up
        enemy_ai_tick(Z.ai, zp, r.env, m670);                          // 0x67 -> 0x78
        const bool to78 = Z.ai.i(0x72c) == 0x78 && Z.ai.i(0x79c) == 0x67;
        const AiOut o78 = enemy_ai_tick(Z.ai, zp, r.env, m670);        // 0x78: gated 302, 166 ticks, H 0.75
        const bool req = o78.section == 0x12e && !o78.section_force && Z.ai.dur() == 165 &&   // 166, one spent by the action player
                         std::fabs(Z.ai.f(0x834) - 0.75f) < 1e-6f;
        std::printf("[aitest]   M670 0x78: section 0x%x force %d dur %d H %.3f\n", o78.section, int(o78.section_force),
                    Z.ai.dur(), Z.ai.f(0x834));
        r.env.section = 0x12e; r.env.aso_marker = 1;
        enemy_ai_tick(Z.ai, zp, r.env, m670);
        const bool charge = Z.ai.action() == 0x800 && std::fabs(Z.ai.f(0x818) - 3.0f * 0.0666f) < 1e-6f &&
                            std::fabs(Z.ai.f(0x814) - 0.13f * 0.01f) < 1e-7f;
        r.env.aso_marker = 2;
        const float k0 = Z.mover.k1.x;
        enemy_ai_tick(Z.ai, zp, r.env, m670);
        const float k1 = Z.mover.k1.x;
        enemy_ai_tick(Z.ai, zp, r.env, m670);
        const bool push = k1 != k0 && Z.mover.k1.x == k1 && Z.ai.action() == 1 && Z.ai.f(0x818) == 0.0666f;
        r.env.section = 300;                                            // the section ended: EXIT
        enemy_ai_tick(Z.ai, zp, r.env, m670);
        const bool exitok = Z.ai.i(0x72c) == 0x7a && Z.ai.dur() >= 59 && Z.ai.dur() <= 89 && Z.ai.f(0x834) == 1.5f;
        std::printf("[aitest]   M670 dash: to78 %d req %d charge %d push %d exit %d (sub 0x%x dur %d)\n", to78, req,
                    charge, push, exitok, Z.ai.i(0x72c), Z.ai.dur());
        check(to78 && req && charge && push && exitok,
              "M670 dash: 302 gated, sink to 0.75, charge x3 base speed at Marker 1, one push at Marker 2, rest 60 + R%30");
    }
    // ---- M260 Hand: the far branch 0x6e -> 0x6f -> 0x70 (build/ai_spec/M260.md) ----
    {
        using floor4::Pool;
        const EnemyBrain m260 = enemy_brain_for(19);
        check(m260 != nullptr, "Hand registered (AI 19)");
        Pool w; Actor& Z = (w.actors.emplace_back(), w.actors.back());
        Z.instance_id = 71; Z.cast_id = 0x47; Z.hp = Z.max_hp = 300;
        Actor* H = w.spawn_child(Z, 0x1e, {8.5f, 0, 0}, 41, true);
        H->ai.i(0x854) = 71; H->ai.i(0x7a0) = 0; H->ai.f(0x7ac) = 0.0f; H->ai.i(0x7a4) = 0x6e; H->ai.f(0x7b0) = 7.0f;
        H->height = 0.5251f;
        Rig r; r.env.actor = H; r.env.world = &w; r.env.target = {0.0f, 1.075f, 0}; r.env.target_heading = 0.0f;
        r.env.target_floor = 0.0f;
        int t8 = -1, timer = 0; bool def999 = false;
        for (int t = 0; t < 200 && t8 < 0; ++t) {
            const AiOut o = enemy_ai_tick(H->ai, params(0.16f, 0.0525f, 1.0f, 2.0f, 0.0f), r.env, m260);
            if (t == 0) def999 = H->def == 999.0f;
            if (o.section == 8) { t8 = t; timer = H->ai.i(0x71c); }
        }
        const float dx = H->pos.x - 0.0f, dz = H->pos.z - 0.0f;
        const float dist = std::sqrt(dx * dx + dz * dz);
        const float ang = std::atan2(dz, dx);
        std::printf("[aitest]   M260: section 8 at t %d, %.3f u at %.3f rad, timer %d, sub 0x%x\n", t8, dist, ang, timer,
                    H->ai.i(0x72c));
        check(def999 && t8 >= 15 && std::fabs(dist - 6.5f) < 1e-4f && std::fabs(ang - 0.4f) < 1e-4f && timer >= 174 &&
              timer <= 194 && H->ai.i(0x72c) == 0x70,
              "M260: DEF 999; after 15..59 ticks it appears 6.5 u out at heading + 0.4, section 8, life R%20 + 175 - 10d");
    }
    // ---- M710 Astyle: the rooted suction (build/ai_spec/M710.md §8 tests) ----
    {
        struct PW : floor4::Pool {
            Actor P;
            Actor* player() override { return &P; }
        };
        const EnemyBrain m710 = enemy_brain_for(64);
        check(m710 != nullptr && enemy_handler_far_freeze(64), "Astyle registered (AI 64); far freeze");
        EnemyAiParams ap = params(0.08f, 0.03f, 1.25f, 2.0f, 0.0f);    // EI[61]
        int forced = -1;
        for (int s = 0; s < 200 && forced < 0; ++s) {                 // (a) engaged, 10 u: a forced 7 at a decision
            PW w; Actor A; A.cast_id = 75; A.ai_index = 64;
            Rig r; r.env.actor = &A; r.env.world = &w; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {10.0f, 1.075f, 0}; r.env.section = 2; r.env.character = 1; r.env.target_grounded = true;
            enemy_ai_tick(A.ai, ap, r.env, m710);
            A.ai.phase() = 2;
            for (int t = 0; t < 60 && forced < 0; ++t) {
                const AiOut o = enemy_ai_tick(A.ai, ap, r.env, m710);
                if (o.section == 7 && o.section_force) forced = t;
            }
        }
        check(forced >= 0, "M710: a forced section 7 at a decision (35 %, inside d + 20)");
        auto pulled = [&](int psec, int frame) {                       // (b) / (c): one suction tick at 5 u
            PW w; Actor A; A.cast_id = 75; A.ai_index = 64;
            Rig r; r.env.actor = &A; r.env.world = &w;
            r.env.target = {5.0f, 1.075f, 0}; r.env.character = 1; r.env.target_grounded = true;
            r.env.target_section = psec;
            enemy_ai_tick(A.ai, ap, r.env, m710);                      // INIT
            A.ai.phase() = 2; A.ai.cool() = 50; A.ai.dur() = 50;       // no decision this tick
            A.ai.set_v(0x734, {5.0f, 0.0f, 0.0f});
            r.env.section = 7; r.env.aso_frame = frame; r.env.aso_marker = 0;
            enemy_ai_tick(A.ai, ap, r.env, m710);
            return w.P.mover.k2.x;
        };
        const float k = pulled(0, 6), k_burst = pulled(0x139, 6), k_frame = pulled(0, 2);
        std::printf("[aitest]   M710 pull: %.5f (burst %.5f, frame 2 %.5f)\n", k, k_burst, k_frame);
        check(std::fabs(k + 0.085f) < 1e-6f && k_burst == 0.0f && k_frame == 0.0f,
              "M710: the pull toward it on P+0x344, 0.085 for Yunica at 5 u; none in a burst section or off frames 5..8");
    }
    // ---- M750 Liliborad: the hand-flown orb (build/ai_spec/M750.md §7 test) ----
    {
        struct FW : floor4::Pool {
            bool orb = true;
            void for_each_effect(const std::function<void(const Fx&)>& f) override {
                if (!orb) return;
                Fx x; x.id = 319; x.owner = 79; x.tag = 1; f(x);
            }
        };
        const EnemyBrain m750 = enemy_brain_for(68);
        check(m750 != nullptr, "Liliborad registered (AI 68)");
        EnemyAiParams lp = params(0.1685f, 0.0225f, 11.0f, 6.0f, 0.0f);   // EI[65]
        FW w; Actor L; L.instance_id = 79; L.cast_id = 79; L.ai_index = 68;
        Rig r; r.env.actor = &L; r.env.world = &w; r.env.target = {0.0f, 1.075f, 20.0f};
        enemy_ai_tick(L.ai, lp, r.env, m750);                          // INIT
        L.ai.phase() = 2; L.ai.heading = 0.0f;
        r.env.section = 7; r.env.aso_marker = 319;                     // the throw at Marker(319)
        const AiOut t0 = enemy_ai_tick(L.ai, lp, r.env, m750);
        const bool thrown = !t0.effects.empty() && t0.effects[0].id == 319 && t0.effects[0].knock == 1.5f &&
                            t0.effects[0].rtype == 1 && L.ai.i(0x79c) == 1 &&
                            std::fabs(L.ai.f(0x7b0) - 8.5f / 33.0f) < 1e-6f && L.ai.f(0x7fc) == 8.5f;
        r.env.aso_marker = 0;
        bool straight = true;
        for (int t = 1; t < 33; ++t) { enemy_ai_tick(L.ai, lp, r.env, m750); straight = straight && L.ai.f(0x7ec) == 0.0f; }
        enemy_ai_tick(L.ai, lp, r.env, m750);                          // c = 33: the homing leg toward T (0, 20)
        const Vec3 E1 = L.ai.v(0x7fc);
        const bool homing = E1.z > 7.5f && E1.x < 6.0f;                // 8.5 from (8.24, 0) toward T (0, 20)
        int caught = -1; AiOut last;
        for (int t = 34; t < 400 && caught < 0; ++t) {
            last = enemy_ai_tick(L.ai, lp, r.env, m750);
            if (last.section == 20) caught = t;
        }
        std::printf("[aitest]   M750: thrown %d straight %d homing %d (end %.2f,%.2f) caught at c %d cool %d\n", thrown,
                    straight, homing, E1.x, E1.z, caught, L.ai.cool());
        check(thrown && straight && homing && caught > 66 && L.ai.i(0x79c) == 2 && L.ai.cool() >= 29 && L.ai.cool() <= 59,
              "M750 (Normal, N 33): 8.5 u straight, a leg at the target, back, caught -> section 20, cooldown 30..59");
        const AiOut end = enemy_ai_tick(L.ai, lp, r.env, m750);
        check(!end.effects.empty() && end.effects[0].remain_tag == 1 && end.effects[0].remaining == 15 &&
              L.ai.i(0x79c) == 0, "M750 END: the orb's particles get 15 ticks, state 0");
    }
    // ---- M680 Hekat-Baocar: M650 + the hover-height roll + long-homing shots (build/ai_spec/M680.md) ----
    {
        const EnemyBrain m680 = enemy_brain_for(61);
        check(m680 != nullptr && enemy_handler_far_freeze(61) && enemy_handler_far_freeze(67) &&
              !enemy_handler_far_freeze(58), "Hekat-Baocar registered (AI 61); the far freeze on AI 61 / 67 only");
        EnemyAiParams hp = params(0.12f, 0.0555f, 1.0f, 2.0f, 3.5f);   // EI[58]
        int heights[3] = {0, 0, 0}, forced = 0;
        bool shot_ok = false, level_ok = false;
        for (int s = 0; s < 300; ++s) {
            Actor H; H.cast_id = 72; H.ai_index = 61; H.flyer = true;
            Rig r; r.env.actor = &H; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {6.0f, 1.075f, 0}; r.env.section = 2;
            for (int t = 0; t < 200; ++t) {
                const AiOut o = enemy_ai_tick(H.ai, hp, r.env, m680);
                if (o.section == 7 && o.section_force) { ++forced; break; }
            }
            const float h = H.ai.f(0x834);
            heights[h == 1.5f ? 0 : h == 3.5f ? 1 : h == 5.5f ? 2 : 1] += 1;
            if (H.ai.i(0x72c) == 10) {                                   // a volley at Marker(310)
                r.env.section = 7; r.env.aso_marker = 310; r.env.aso_frame = 2;
                const AiOut v = enemy_ai_tick(H.ai, hp, r.env, m680);
                if (v.bullets.size() == 2) {
                    const AiBullet& b = v.bullets[0];
                    shot_ok = shot_ok || (b.kind == 2 && b.effect == 310 && b.speed == 0.15f && b.accel == 0.0015f &&
                                          b.f16c == 10.0f && b.f170 == 180.0f);
                    if (H.ai.f(0x834) == 1.5f) level_ok = level_ok || b.dir.y == 0.0f;
                    if (H.ai.f(0x834) != 1.5f && b.dir.y < -0.28f && b.dir.y > -0.29f) level_ok = level_ok || false;
                }
            }
        }
        std::printf("[aitest]   M680: heights 1.5 x%d 3.5 x%d 5.5 x%d, forced 7 x%d\n", heights[0], heights[1], heights[2], forced);
        check(heights[0] > 0 && heights[2] > 0 && forced > 100 && shot_ok,
              "M680: H re-rolled to 1.5 / 5.5 at decisions; section 7 forced; shots 0.15 + 0.0015/tick, homing 10..180");
        (void)level_ok;
    }
    // ---- M740 Drefflum: stationary, a 30-tick decision gate, needles inside 7 u, the bubble fan (M740.md) ----
    {
        const EnemyBrain m740 = enemy_brain_for(67);
        check(m740 != nullptr, "Drefflum registered (AI 67)");
        EnemyAiParams dp = params(0.16f, 0.09f, 1.0f, 2.0f, 0.0f);     // EI[64]
        int s7 = 0, s8 = 0, offgate = 0, fan5 = 0, moved = 0;
        for (int s = 0; s < 120; ++s) {
            Actor D; D.cast_id = 78; D.ai_index = 67;
            Rig r; r.env.actor = &D; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {s % 2 ? 5.0f : 10.0f, 1.075f, 0}; r.env.section = 2;
            enemy_ai_tick(D.ai, dp, r.env, m740);                          // INIT: the random facing
            const float h0 = D.ai.heading;
            D.ai.phase() = 2; D.ai.turn_hold() = 1;                        // engaged (phase 1's stop(1) hold)
            for (int t = 0; t < 200; ++t) {
                const AiOut o = enemy_ai_tick(D.ai, dp, r.env, m740);
                moved += D.ai.heading != h0;
                if (o.section == 7 || o.section == 8) {
                    (o.section == 7 ? s7 : s8) += 1;
                    offgate += std::uint32_t(D.ai.i(0x87c) - 1) % 30u != 0u;   // the counter ticked after the brain
                    if (o.section == 8) {                                  // Marker 1 -> the fan
                        r.env.section = 8; r.env.aso_marker = 1;
                        const AiOut f = enemy_ai_tick(D.ai, dp, r.env, m740);
                        fan5 += f.bullets.size() == 5 && f.bullets[0].kind == 7 && f.bullets[0].effect == 0x1eb &&
                                std::fabs(f.bullets[1].dir.z - float(std::sin(-0.4))) < 1e-4f;
                    }
                    break;
                }
            }
        }
        std::printf("[aitest]   M740: s7 %d s8 %d off-gate %d fans %d turned %d\n", s7, s8, offgate, fan5, moved);
        check(s7 > 0 && s8 > 0 && offgate == 0 && fan5 == s8 && moved == 0,
              "M740: forced 7 / 8 only on the 30-tick gate, a 5-bubble kind-7 fan at Marker 1, never turns");
    }
}
