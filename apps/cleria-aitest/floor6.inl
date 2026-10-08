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
    // ---- M700 Fliogan: the hops, the dash effect, the restarting slash (build/ai_spec/M700.md §7 test) ----
    {
        const EnemyBrain m700 = enemy_brain_for(63);
        check(m700 != nullptr, "Fliogan registered (AI 63)");
        EnemyAiParams fp = params(0.12f, 0.03f, 1.8f, 2.0f, 0.0f);   // EI[60]
        auto engaged = [&](Rig& r, Actor& F, float pd) {
            F.instance_id = 74; F.cast_id = 74; F.ai_index = 63;
            r.env.actor = &F; r.env.target = {pd, 1.075f, 0.0f};
            m700(F.ai, fp, r.env);                                   // INIT
            F.ai.phase() = 2; F.ai.heading = 0.0f;
            F.ai.f(0x734) = pd; F.ai.f(0x73c) = 0.0f; F.ai.dist() = pd;
        };
        int n8 = 0, n7 = 0; bool ok8 = true, ok7 = true;
        for (int s = 0; s < 200; ++s) {
            Rig r; Actor F; for (int i = 0; i < s; ++i) r.rng();
            engaged(r, F, 3.0f); r.env.section = 2; r.env.difficulty = 1;
            F.ai.i(0x87c) = 21;                                      // off the hop gate
            const AiOut o = m700(F.ai, fp, r.env);
            const int cool = F.ai.cool();
            if (o.section == 8) {
                ++n8;                                                // the lockout was set, then the TAIL took 1
                ok8 = ok8 && o.section_force && F.ai.dur() == 90 && cool >= 240 && cool <= 299 &&
                      F.ai.i(0x7a0) == cool / 3 - 1 && F.ai.action() == 1 && F.ai.i(0x72c) == 10;
            } else if (o.section == 7) {
                ++n7;
                ok7 = ok7 && o.section_force && F.ai.dur() == 170 && cool >= 385 && cool <= 444 &&
                      F.ai.i(0x7a0) == cool / 3 - 1 && F.ai.i(0x72c) == 20 && F.ai.i(0x79c) == 0;
            } else ok8 = ok8 && F.ai.action() == 2 && F.ai.dur() >= 15 && F.ai.dur() <= 44;
        }
        std::printf("[aitest]   M700 at 3 u (Hard): section 8 x%d, section 7 x%d of 200\n", n8, n7);
        check(ok8 && n8 > 80 && n8 < 140, "M700 inside 4.5 u: 55 % force(8), action 90, cooldown 270 - 30d + R%60, lockout a third");
        check(ok7 && n7 > 15 && n7 < 55, "M700: else 35 % force(7), action 150 + 20d, cooldown 410 - 25d + R%60");
        {   // COMBO7: the restart at Pattern frame 5, d + 2 times
            Rig r; Actor F; engaged(r, F, 3.0f);
            F.ai.i(0x72c) = 20; r.env.section = 7; r.env.aso_frame = 5;
            const AiOut a1 = m700(F.ai, fp, r.env);
            const bool restart = a1.section == 7 && a1.section_force && F.ai.i(0x79c) == 1;
            r.env.aso_frame = 4;
            const bool early = m700(F.ai, fp, r.env).section < 0;
            r.env.aso_frame = 5; F.ai.i(0x79c) = 2;
            const bool spent = m700(F.ai, fp, r.env).section < 0 && F.ai.i(0x79c) == 2;
            r.env.section = 2; F.ai.cool() = 300; F.ai.i(0x7a0) = 0;
            m700(F.ai, fp, r.env);
            check(restart && early && spent && F.ai.i(0x72c) == 0 && F.ai.i(0x7a0) == 99,
                  "M700 COMBO7 (Normal): force(7) at frame 5 while the count < d + 2; the section's end re-arms the lockout");
        }
        {   // COMBO8: Marker(496) spawns the attached effect, Marker(1) releases it
            Rig r; Actor F; engaged(r, F, 3.0f);
            F.ai.i(0x72c) = 10; r.env.section = 8; r.env.aso_marker = 496;
            const AiOut s1 = m700(F.ai, fp, r.env);
            const bool spawn = s1.effects.size() == 1 && s1.effects[0].id == 496 && s1.effects[0].attach &&
                               s1.effects[0].attach_full && s1.effects[0].power == 5.0f && s1.effects[0].knock == 10.0f &&
                               s1.effects[0].rtype == 2 && s1.effects[0].status == 0x10 && s1.effects[0].tag == 1 &&
                               s1.set_marker == 0 && F.ai.i(0x88c) != 0;
            r.env.aso_marker = 0;
            const bool held = m700(F.ai, fp, r.env).effects.empty();
            r.env.aso_marker = 1;
            const AiOut s2 = m700(F.ai, fp, r.env);
            const bool rel = s2.effects.size() == 1 && s2.effects[0].release_tag == 1 && s2.set_marker == 0 &&
                             F.ai.i(0x88c) == 0;
            F.ai.i(0x88c) = 1; r.env.aso_marker = 0; r.env.section = 4;   // knocked out of the dash
            const AiOut s3 = m700(F.ai, fp, r.env);
            check(spawn && held && rel && s3.effects.size() == 1 && s3.effects[0].release_tag == 1,
                  "M700 COMBO8: Marker(496) -> EF2 496 attached (mode 1, power 5, rtype 2, status 0x10); released at Marker(1) / outside section 8");
        }
        {   // the hops: forward beyond 6 u (0.6 on channel 1), back inside 3.5 u (-0.2)
            int fwd = 0, back = 0; bool fok = true, bok = true;
            for (int s = 0; s < 300; ++s) {
                for (float pd : {8.0f, 3.0f}) {
                    Rig r; Actor F; for (int i = 0; i < s; ++i) r.rng();
                    engaged(r, F, pd); r.env.section = 1;
                    F.ai.i(0x87c) = 20; F.ai.dur() = 5; F.vert.grounded = true;
                    const float y0 = F.pos.y;
                    m700(F.ai, fp, r.env);
                    const float k = std::sqrt(F.mover.k1.x * F.mover.k1.x + F.mover.k1.z * F.mover.k1.z);
                    if (k == 0.0f) continue;
                    const bool up = !F.vert.grounded && std::fabs(F.pos.y - y0 - 0.2f) < 1e-6f;
                    if (pd > 6.0f) { ++fwd; fok = fok && up && std::fabs(k - 0.6f) < 1e-5f && F.mover.k1.x > 0.35f; }
                    else { ++back; bok = bok && up && std::fabs(k - 0.2f) < 1e-5f && F.mover.k1.x < -0.16f; }
                }
            }
            std::printf("[aitest]   M700 hops: forward %d, back %d of 300\n", fwd, back);
            check(fok && bok && fwd > 50 && fwd < 100 && back > 15 && back < 50,
                  "M700 hops every 10th tick: 25 % forward beyond 6 u (0.6, +-54 deg), 10 % back inside 3.5 u (-0.2, +-36 deg)");
        }
    }
    // ---- M470 Levenueba: the glob fan, the grab and its hold, the bite and spit (build/ai_spec/M470.md §8 test) ----
    {
        const EnemyBrain m470 = enemy_brain_for(40);
        check(m470 != nullptr, "Levenueba registered (AI 40)");
        EnemyAiParams vp = params(0.08f, 0.05f, 1.0f, 2.25f, 0.0f);   // EI[37]
        auto engaged = [&](Rig& r, Actor& L, const Vec3& target) {
            L.instance_id = 51; L.cast_id = 0x33; L.ai_index = 40; L.def = 30.0f;
            r.env.actor = &L; r.env.target = target;
            m470(L.ai, vp, r.env);                                   // INIT
            L.ai.phase() = 2; L.ai.heading = 0.0f; r.env.facing = 0.0f;
            L.ai.f(0x734) = target.x; L.ai.f(0x73c) = target.z; L.ai.dist() = std::sqrt(target.x * target.x + target.z * target.z);
        };
        {   // the fan: 3 / 7 / 11 globs, slots 3..6 twice on d >= 2 with the second always kind 1
            bool ok = true; int lobs = 0, shots = 0;
            for (int diff : {-1, 0, 2}) {
                Rig r; Actor L; engaged(r, L, {4.0f, 1.075f, 0.0f});
                r.env.difficulty = diff; r.env.section = 7; r.env.aso_marker = 315;
                const AiOut o = m470(L.ai, vp, r.env);
                const std::size_t want = diff < 0 ? 3u : diff > 1 ? 11u : 7u;
                ok = ok && o.bullets.size() == want && o.set_marker == 0 && L.ai.action() == 2 &&
                     L.ai.cool() >= 30 && L.ai.cool() <= 59;
                static const int slot11[11] = {0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 6};
                static const float sx[7] = {0.4f, 0.4f, 0.5f, 0.8f, 0.8f, 0.7f, 0.7f};   // heading 0: x = the slot's z
                for (std::size_t i = 0; i < o.bullets.size() && ok; ++i) {
                    const AiBullet& b = o.bullets[i];
                    const int slot = diff > 1 ? slot11[i] : int(i);
                    const bool second = diff > 1 && i >= 4 && ((i - 3) % 2 == 1);
                    ok = ok && b.effect == 315 && b.gravity == 0.004f && b.knock == 1.0f && std::fabs(b.pos.x - sx[slot]) < 1e-6f &&
                         (b.kind == 1 || b.kind == 0x29) && !(second && b.kind != 1) &&
                         std::fabs(b.dir.x * b.dir.x + b.dir.y * b.dir.y + b.dir.z * b.dir.z - 1.0f) < 1e-4f;
                    if (b.kind == 0x29) { ++lobs; ok = ok && b.f168 == 65636.0f && b.f16c == 1.2f && b.f170 == 2.5f && b.f174 == 4.0f; }
                    else { ++shots; ok = ok && b.home_period == 1 && b.home_rate == (diff > 1 ? 0.0174533f : 0.0f) && b.f170 == 360.0f; }
                }
            }
            std::printf("[aitest]   M470 fans: %d lobs (kind 0x29), %d shots (kind 1)\n", lobs, shots);
            check(ok && lobs > 3 && shots > 7, "M470 section 7 at Marker(315): 3 / 7 / 11 globs from the slot table, a coin flip between "
                                               "the kind-0x29 lob and the kind-1 shot (1 deg homing on d >= 2); cooldown R%30 + 30");
        }
        {   // DECIDE: 30 % section 7 inside 5 u; else 15 % section 8 inside 7.5 u unless another Levenueba grabs
            struct W : floor4::Pool { Actor* other = nullptr; void for_each(const std::function<void(Actor&)>& f) override { if (other) f(*other); } };
            int n7 = 0, n8 = 0, n8far = 0, n8busy = 0;
            for (int s = 0; s < 400; ++s) {
                for (int mode = 0; mode < 3; ++mode) {
                    Rig r; Actor L, O; W w; for (int i = 0; i < s; ++i) r.rng();
                    engaged(r, L, {mode == 0 ? 4.0f : 6.5f, 1.075f, 0.0f}); r.env.section = 1; r.env.world = &w;
                    if (mode == 2) { O.cast_id = 0x33; O.hp = 50; O.live_section = 8; w.other = &O; }
                    const AiOut o = m470(L.ai, vp, r.env);
                    if (mode == 0) { n7 += o.section == 7; n8 += o.section == 8; }
                    else if (mode == 1) n8far += o.section == 8;
                    else n8busy += o.section == 8;
                }
            }
            std::printf("[aitest]   M470 decisions of 400: at 4 u section 7 x%d / 8 x%d; at 6.5 u section 8 x%d, x%d while another grabs\n",
                        n7, n8, n8far, n8busy);
            check(n7 > 90 && n7 < 150 && n8 > 20 && n8 < 65 && n8far > 35 && n8far < 85 && n8busy == 0,
                  "M470 DECIDE: 30 % request(7) inside 5 u, else 15 % request(8) inside 7.5 u, never while another live Levenueba is in section 8");
        }
        {   // the grab: hold at marker 1..13, the bite at marker 2 fast-forwards to 14, then the release and the spit
            Rig r; Actor L; engaged(r, L, {1.75f, 0.575f, 0.0f});       // 1 u ahead of the mouth (0.75, y - 0.5)
            r.env.section = 8; r.env.aso_marker = 1; r.env.target_section = 3;
            const AiOut h1 = m470(L.ai, vp, r.env);
            const bool hold = h1.hold_target && std::fabs(h1.hold_origin.x - 0.75f) < 1e-6f && std::fabs(h1.hold_origin.y - 1.075f) < 1e-6f &&
                              h1.hold_origin.z == 0.0f && L.ai.action() == 1 && L.def == 55.0f && (L.flags12c & 0x40u) != 0 &&
                              L.ai.i(0x79c) == 1 && h1.effects.empty() && h1.fast_forward < 0;
            r.env.aso_marker = 2;
            const std::uint32_t before = XorShift(r.rng)();
            const AiOut h2 = m470(L.ai, vp, r.env);
            const bool bite = h2.hold_target && h2.effects.size() == 1 && h2.effects[0].id == 0xff && h2.effects[0].scale == 1.3f &&
                              std::fabs(h2.effects[0].pos.x - 0.75f) < 1e-6f && h2.fast_forward == 14 && XorShift(r.rng)() == before;
            r.env.aso_marker = 14;
            const AiOut h3 = m470(L.ai, vp, r.env);
            const bool spit = !h3.hold_target && h3.effects.size() == 4 && h3.fast_forward == 14 && L.def == 30.0f &&
                              (L.flags12c & 0x40u) == 0 && L.ai.i(0x79c) == 0 && h3.effects[1].scale >= 1.0f && h3.effects[1].scale < 1.5f;
            check(hold && bite && spit, "M470 section 8: the target within 1.4 + 0.15d of the mouth is held there (DEF + 25, channel 2 only); "
                                        "marker 2 -> EF2 255 and the skip to Marker(14); there the release and 3 more a tick");
            // the escape meter: +3 a held tick outside sections 3 / 0xb, tripped past (d + 5) x 10
            Rig r2; Actor L2; engaged(r2, L2, {1.75f, 0.575f, 0.0f});
            r2.env.section = 8; r2.env.aso_marker = 1; r2.env.target_section = 2;
            int held = 0;
            for (int t = 0; t < 40; ++t) held += m470(L2.ai, vp, r2.env).hold_target;
            Rig r3; Actor L3; engaged(r3, L3, {1.75f, 0.575f, 0.0f});
            r3.env.section = 8; r3.env.aso_marker = 1; r3.env.character = 1; r3.env.target_section = 0x139;
            check(held == 17 && !m470(L3.ai, vp, r3.env).hold_target,
                  "M470 hold: a struggling target breaks free after 17 held ticks (Normal); Yunica in section 0x139 is never held");
        }
        {   // a hit taken: consumed before the action player, a splash between the two
            Rig r; Actor L; engaged(r, L, {4.0f, 1.075f, 0.0f}); r.env.section = 1; L.ai.dur() = 5; r.env.was_hit = true;
            const AiOut o = m470(L.ai, vp, r.env);
            check(o.consume_hit && o.effects.size() == 1 && o.effects[0].id == 0xff && std::fabs(o.effects[0].pos.x - 2.0f) <= 0.5f,
                  "M470 hit: the was-hit bit is consumed, EF2 255 at the midpoint +-0.5");
        }
    }
    // ---- The Piache pair: M450 Maka-Piache (the ring, the summon) and M420 Sinn-Piache (the orb batter)
    //      (build/ai_spec/M450.md §8, M420.md §8 tests) ----
    {
        const EnemyBrain m450 = enemy_brain_for(38), m420 = enemy_brain_for(35);
        check(m450 != nullptr && m420 != nullptr && enemy_handler_far_freeze(38) && enemy_handler_far_freeze(35),
              "Maka-Piache (AI 38) and Sinn-Piache (AI 35) registered, both in the 25-u far freeze");
        EnemyAiParams mp = params(1.5f, 0.0425f, 1.0f, 2.0f, 0.0f);   // EI[35]
        EnemyAiParams sp = params(1.5f, 0.0425f, 1.0f, 8.0f, 0.0f);   // EI[32]
        struct Orb { int handle, kind, owner; Vec3 pos, dir; float speed, f168, f16c, f170; };
        struct W : floor4::Pool {
            Actor P; std::vector<Orb> orbs; bool wall = false;
            Actor* player() override { return &P; }
            Ray ray(const Vec3&, const Vec3& dl) override { Ray r; r.hit = wall || dl.y < 0.0f; return r; }   // a floor below, open around
            void for_each_bullet(const std::function<void(const BulletRef&)>& f) override {
                for (Orb& o : orbs) {
                    BulletRef r; r.handle = o.handle; r.kind = o.kind; r.owner = &o.owner; r.pos = o.pos; r.dir = &o.dir;
                    r.speed = &o.speed; r.f168 = &o.f168; r.f16c = &o.f16c; r.f170 = &o.f170;
                    f(r);
                }
            }
        };
        auto setup = [&](Rig& r, W& w, Actor& X, EnemyBrain br, const EnemyAiParams& pp, int cast, float pdist) {
            X.instance_id = 300 + cast; X.cast_id = cast; X.ai_index = cast == 0x31 ? 38 : 35; X.level = 43;
            w.P.cast_id = 13; w.P.pos = {pdist, 0.0f, 0.0f}; w.P.height = 1.025f; w.P.hp = w.P.max_hp = 100;
            r.env.actor = &X; r.env.world = &w; r.env.self = {0, 1.025f, 0}; r.env.target = {pdist, 1.025f, 0.0f};
            br(X.ai, pp, r.env);                                     // INIT
            X.ai.phase() = 2; X.ai.heading = 0.0f; X.ai.dist() = pdist; X.ai.f(0x734) = pdist; X.ai.f(0x73c) = 0.0f;
        };
        {   // the visibility cycle (Normal): 240 shown, 210 hidden, 45 shown; alpha +-0.01; the mask shows it always
            Rig r; W w; Actor M; r.env.actor = &M; r.env.world = &w;
            int shown0 = 0, hidden = 0, shown1 = 0; float amin = 1.0f, aend = 0.0f; bool phases = true;
            for (int t = 1; t <= 495; ++t) {
                m450(M.ai, mp, r.env);
                if (t == 1) M.ai.phase() = 3;                        // INIT ran; from here only the tail
                const bool s = M.ai.i(0x79c) != 0;
                if (t <= 240) { shown0 += s; phases = phases && s; }
                else if (t <= 450) { hidden += !s; amin = std::min(amin, M.ai.alpha); }
                else { shown1 += s; aend = M.ai.alpha; }
            }
            Rig r2; W w2; Actor M2; r2.env.actor = &M2; r2.env.world = &w2; r2.env.mask_of_eyes = true;
            bool masked = true;
            for (int t = 1; t <= 495; ++t) { m450(M2.ai, mp, r2.env); if (t == 1) M2.ai.phase() = 3; masked = masked && M2.ai.i(0x79c) == 1 && M2.ai.alpha == 1.0f; }
            Rig r3; W w3; Actor M3; r3.env.actor = &M3; r3.env.world = &w3; M3.ai.i(0x854) = 77;   // summoned
            m450(M3.ai, mp, r3.env);
            std::printf("[aitest]   Piache visibility: shown %d, hidden %d (alpha min %.2f), shown %d (alpha %.2f)\n", shown0, hidden, amin, shown1, aend);
            check(phases && shown0 == 240 && hidden == 210 && shown1 == 45 && amin == 0.0f && std::fabs(aend - 0.45f) < 1e-4f && masked &&
                  M3.ai.phase() == 2 && std::fabs(M3.ai.alpha - 0.01f) < 1e-6f,
                  "Piache visibility: 240 shown after INIT, then 210 + 30d hidden / 15 (3 - d) shown, alpha +-0.01; always shown "
                  "under the Mask of Eyes; a summoned one starts engaged at alpha 0");
        }
        {   // M450 STAND: the ring inside 11 u, else the summon unless crowded; the orphaned request at +0x714 = 0
            int ring = 0, sum = 0, sum_crowd = 0, orphan = 0; bool ok = true;
            for (int s = 0; s < 400; ++s) {
                for (int mode = 0; mode < 3; ++mode) {
                    Rig r; W w; Actor M; for (int i = 0; i < s; ++i) r.rng();
                    setup(r, w, M, m450, mp, 0x31, 5.0f); r.env.section = 2;
                    if (mode == 1) for (int k = 0; k < 2; ++k) { w.actors.emplace_back(); Actor& o = w.actors.back(); o.cast_id = 0x2e; o.hp = o.max_hp = 50; o.pos = {3.0f + k, 0, 0}; }
                    M.ai.dur() = mode == 2 ? 0 : 30;
                    const AiOut o = m450(M.ai, mp, r.env);
                    if (mode == 0) {
                        if (o.section == 8) { ++ring; ok = ok && M.ai.i(0x7a4) == 2 && !o.section_force; }
                        else if (o.section == 0x139) { ++sum; ok = ok && M.ai.i(0x7a4) == 3; }
                        else ok = ok && M.ai.i(0x7a4) == 0 && M.ai.action() == 2;
                    } else if (mode == 1) sum_crowd += o.section == 0x139;
                    else if (o.section >= 0) { ++orphan; ok = ok && M.ai.i(0x7a4) == 1 && M.ai.steer() == 5 && M.ai.dur() >= 15 && M.ai.dur() <= 59; }
                }
            }
            std::printf("[aitest]   M450 STAND of 400 (+0x714 = 30, 5 u): ring x%d, summon x%d (x%d with two Sinn-Piaches near); orphaned at 0: x%d\n",
                        ring, sum, sum_crowd, orphan);
            Rig r; W w; Actor M; setup(r, w, M, m450, mp, 0x31, 5.0f);
            M.ai.i(0x7a4) = 2; r.env.section = 2; r.env.difficulty = 1;
            m450(M.ai, mp, r.env);
            check(ok && ring > 120 && ring < 190 && sum > 20 && sum < 65 && sum_crowd == 0 && orphan > 250 &&
                  M.ai.cool() == 75 && M.ai.i(0x7a4) == 1 && M.ai.steer() == 5,
                  "M450: standing off cooldown every 10th tick under a 15..59 roll — inside 11 u 60 % request(8), else 40 % "
                  "request(0x139) unless 2 Sinn-Piaches stand within 15 u; a request at +0x714 = 0 is orphaned by the walk; "
                  "the ring over -> cooldown 90 - 15d and a walk");
        }
        {   // the summon at Marker(269): the other cast 1..5 u away, alpha 0, parent set, section 2 requested
            Rig r; W w; Actor M; setup(r, w, M, m450, mp, 0x31, 5.0f);
            M.ai.i(0x7a4) = 3; r.env.section = 0x139; r.env.aso_marker = 269;
            const AiOut o = m450(M.ai, mp, r.env);
            const bool one = w.actors.size() == 1 && o.effects.size() == 1 && o.effects[0].id == 269 && o.set_marker == 0;
            bool child = false;
            if (one) {
                const Actor& c = w.actors.back();
                const float rr = std::sqrt(o.effects[0].pos.x * o.effects[0].pos.x + o.effects[0].pos.z * o.effects[0].pos.z);
                child = c.cast_id == 0x2e && c.ai.alpha == 0.0f && c.ai.i(0x854) == M.instance_id && c.ai.hook_section == 2 &&
                        rr >= 1.0f && rr < 5.0f && std::fabs(c.pos.x - o.effects[0].pos.x) < 1e-6f &&
                        std::fabs(c.pos.y + c.height - 1.025f) < 1e-6f && c.ai.phase() == 0;
            }
            Rig r2; W w2; Actor S2; setup(r2, w2, S2, m420, sp, 0x2e, 5.0f);
            S2.ai.i(0x7a4) = 3; r2.env.section = 0x139; r2.env.aso_marker = 269;
            m420(S2.ai, sp, r2.env);
            Rig r3; W w3; Actor M3; setup(r3, w3, M3, m450, mp, 0x31, 5.0f); w3.wall = true;
            M3.ai.i(0x7a4) = 3; r3.env.section = 0x139; r3.env.aso_marker = 269;
            const AiOut o3 = m450(M3.ai, mp, r3.env);
            check(one && child && w2.actors.size() == 1 && w2.actors.back().cast_id == 0x31 && w3.actors.empty() && o3.set_marker != 0,
                  "Piache summon at Marker(269): each spawns the OTHER cast 1..5 u away (alpha 0, +0x854 = the parent, section 2), "
                  "effect 269 there; ten blocked probes keep the marker");
        }
        {   // M420: the claim, its exclusions, the run's arrival, the bat
            auto orb = [](float x, float z, const Vec3& dir) { return Orb{7, 1, 555, {x, 1.025f, z}, dir, 0.05f, 1000.0f, 1000.0f, 1000.0f}; };
            Rig r; W w; Actor S; setup(r, w, S, m420, sp, 0x2e, 9.0f); r.env.section = 2;
            w.orbs.push_back(orb(0.0f, 6.0f, {0, 0, 1}));            // 6 u away, moving perpendicular to the player
            S.ai.dur() = 21; m420(S.ai, sp, r.env);
            const bool no_scan = S.ai.i(0x7bc) == 0;                 // +0x714 % 10 != 0
            S.ai.dur() = 20; S.ai.cool() = 5; m420(S.ai, sp, r.env);
            // waypoint = pos + 2 dir + dir max(L / 3 - 1, 0): (0, 1.025, 8), L = 8 -> k = 1.6667 -> z 9.6667
            const bool claim = S.ai.i(0x7a4) == 2 && S.ai.i(0x7bc) == 7 && w.orbs[0].owner == S.instance_id && w.orbs[0].f168 == 0.0f &&
                               w.orbs[0].f16c == 0.0f && w.orbs[0].f170 == 0.0f && std::fabs(S.ai.f(0x89c) - (8.0f + (8.0f / 3.0f - 1.0f))) < 1e-4f;
            const AiOut run = m420(S.ai, sp, r.env);
            const bool running = S.ai.action() == 0x800 && S.ai.steer() == 4 && run.section < 0 && S.ai.i(0x79c) == 1;
            int excluded = 0;
            for (int c = 0; c < 5; ++c) {
                Rig rx; W wx; Actor Sx; setup(rx, wx, Sx, m420, sp, 0x2e, 9.0f); rx.env.section = 2;
                Orb o = orb(0.0f, 6.0f, {0, 0, 1});
                if (c == 0) o.dir = normalize(Vec3{9.0f, 0, -6.0f});         // heading at the player
                if (c == 1) { o.pos = {8.0f, 1.025f, 1.0f}; }        // within 2 of the player
                if (c == 2) o.pos.y = 2.1f;                          // more than 1 above
                if (c == 3) { wx.actors.emplace_back(); Actor& ot = wx.actors.back(); ot.cast_id = 0x2e; ot.hp = ot.max_hp = 50; ot.ai.i(0x7bc) = 7; }
                if (c == 4) o.pos.z = 12.5f;                         // beyond 12
                wx.orbs.push_back(o);
                Sx.ai.dur() = 20; Sx.ai.cool() = 5; m420(Sx.ai, sp, rx.env);
                excluded += Sx.ai.i(0x7bc) == 0 && wx.orbs[0].f168 == 1000.0f;
            }
            // arrival: within 0.22 of last tick's waypoint -> the stance
            Rig ra; W wa; Actor Sa; setup(ra, wa, Sa, m420, sp, 0x2e, 9.0f); ra.env.section = 1;
            wa.orbs.push_back(orb(0.3f, -2.0f, {0, 0, 1})); wa.orbs[0].owner = Sa.instance_id;
            Sa.ai.i(0x7a4) = 2; Sa.ai.i(0x7bc) = 7; Sa.ai.set_v(0x894, {0.1f, 1.025f, 0.1f});
            const AiOut arr = m420(Sa.ai, sp, ra.env);
            const bool arrived = arr.section == 7 && !arr.section_force && Sa.ai.i(0x7a4) == 5 && Sa.ai.action() == 1 && Sa.ai.steer() == 1;
            // the bat: the orb 0.2 away in section 7
            ra.env.section = 7; wa.orbs[0].pos = {0.0f, 1.025f, 0.2f};
            const AiOut bat = m420(Sa.ai, sp, ra.env);
            const bool hit = bat.section == 0x14 && bat.effects.size() == 1 && bat.effects[0].id == 0x41 &&
                             std::fabs(bat.effects[0].pos.z - 0.1f) < 1e-6f && std::fabs(wa.orbs[0].speed - 0.35f) < 1e-6f &&
                             std::fabs(wa.orbs[0].dir.x - 9.0f / std::sqrt(81.04f)) < 1e-5f && wa.orbs[0].dir.y == 0.0f &&
                             Sa.mover.k2.z > 0.0f && std::fabs(Sa.mover.k2.z - 0.01f) < 1e-6f;
            ra.env.section = 0x14; m420(Sa.ai, sp, ra.env);
            const bool waits = Sa.ai.i(0x7a4) == 5;
            ra.env.section = 2; m420(Sa.ai, sp, ra.env);
            check(no_scan && claim && running && excluded == 5,
                  "M420 orb scan (every 10th tick): the nearest tagged kind-1 orb within 12 u is claimed (owner, tags 0, the lead "
                  "point) -> the run; not one heading at the player, within 2 u of it, 1 u off its height, held by another, or beyond 12 u");
            if (!(arrived && hit && waits)) std::printf("[aitest]   M420 bat: arrived %d hit %d waits %d\n", arrived, hit, waits);
            check(arrived && hit && waits && std::uint32_t(Sa.ai.i(0x7a4)) <= 1u && Sa.ai.i(0x7bc) == 0,   // FLAG's 1-in-5 roll: walk or stand
                  "M420 bat: within 0.22 of the lead point request(7); the orb within 0.32 -> flat at the player at 0.35 + 0.3d, "
                  "effect 65 at the midpoint, request(0x14); the swing over -> back to the loop");
        }
        {   // M420 DECIDE: 75 % the summon alone; with a Maka-Piache within 15 u it follows it instead
            int sum = 0, follow = 0, sum_near = 0;
            for (int s = 0; s < 400; ++s) {
                for (int mode = 0; mode < 2; ++mode) {
                    Rig r; W w; Actor S; for (int i = 0; i < s; ++i) r.rng();
                    setup(r, w, S, m420, sp, 0x2e, 9.0f); r.env.section = 2;
                    if (mode == 1) { w.actors.emplace_back(); Actor& o = w.actors.back(); o.instance_id = 412; o.cast_id = 0x31; o.hp = o.max_hp = 50; o.pos = {0, 0, 10.0f}; }
                    S.ai.dur() = 30;
                    const AiOut o = m420(S.ai, sp, r.env);
                    if (mode == 0) sum += o.section == 0x139 && S.ai.i(0x7a4) == 3;
                    else { follow += S.ai.i(0x888) == 412; sum_near += o.section == 0x139; }
                }
            }
            std::printf("[aitest]   M420 DECIDE of 400 (+0x714 = 30): summon x%d alone; with a Maka-Piache at 10 u follow x%d, summon x%d\n", sum, follow, sum_near);
            check(sum > 155 && sum < 235 && follow == sum && sum_near == 0,
                  "M420: standing, 75 % under the 15..59 roll request(0x139); a Maka-Piache within 15 u -> +0x888 = it, no summon");
        }
    }
    // ---- M720 Heketie: the ally scan, the run and cast, the heal / disguised shot, the flee, the fan (build/ai_spec/M720.md §8 test) ----
    {
        const EnemyBrain m720 = enemy_brain_for(65);
        check(m720 != nullptr && enemy_handler_far_freeze(65), "Heketie registered (AI 65), in the 25-u far freeze");
        EnemyAiParams hp = params(0.12f, 0.06f, 1.0f, 2.0f, 0.0f);   // EI[62]
        struct W : floor4::Pool {
            Actor P; bool wall = false;
            Actor* player() override { return &P; }
            Ray ray(const Vec3&, const Vec3&) override { Ray r; r.hit = wall; return r; }
        };
        auto setup = [&](Rig& r, W& w, Actor& H, float pdist) {
            H.instance_id = 76; H.cast_id = 76; H.ai_index = 65;
            w.P.cast_id = 13; w.P.pos = {pdist, 0.0f, 0.0f}; w.P.height = 1.075f; w.P.hp = w.P.max_hp = 100;
            r.env.actor = &H; r.env.world = &w; r.env.target = {pdist, 1.075f, 0.0f};
            m720(H.ai, hp, r.env);                                   // INIT
            H.ai.phase() = 2; H.ai.heading = 0.0f; r.env.facing = 0.0f;
        };
        auto wounded = [](W& w, int id, float x, float hpv) -> Actor& {
            w.actors.emplace_back();
            Actor& o = w.actors.back();
            o.instance_id = id; o.cast_id = 72; o.kind = 8; o.max_hp = 400; o.hp = hpv; o.pos = {x, 0.0f, 3.0f}; o.height = 1.075f;
            return o;
        };
        {   // the scan picks the nearest wounded monster with a clear line; the run, the cast, the shot
            Rig r; W w; Actor H; setup(r, w, H, 9.0f); r.env.section = 2;
            wounded(w, 301, 8.0f, 200.0f); wounded(w, 302, 2.0f, 400.0f);   // 302 is unhurt
            Actor& near = wounded(w, 303, 0.0f, 399.5f);                    // less than 1 HP missing
            wounded(w, 304, 4.0f, 100.0f);
            (void)near;
            H.ai.dur() = 7;
            m720(H.ai, hp, r.env);
            const bool picked = H.ai.i(0x888) == 304 && H.ai.i(0x79c) == 2 && H.ai.dur() == 0 && H.ai.i(0x7a0) == 0;
            const AiOut g = m720(H.ai, hp, r.env);                   // GOTO_ALLY: 5 u away (3-D) -> not yet (25 > 25 fails)
            const bool run = g.section < 0 && H.ai.action() == 0x800 && H.ai.steer() == 4 && H.ai.f(0x894) == 4.0f && H.ai.f(0x89c) == 3.0f;
            w.actors.back().pos.x = 3.0f;
            const AiOut c = m720(H.ai, hp, r.env);
            const bool cast = c.section == 7 && !c.section_force && H.ai.i(0x79c) == 3;
            r.env.section = 7; r.env.aso_marker = 5080314;
            int heals = 0, attacks = 0; bool shot_ok = true;
            for (int s = 0; s < 300; ++s) {
                Rig r2 = r; for (int i = 0; i < s; ++i) r2.rng();
                r2.env.rng = [&r2] { return r2.rng(); };
                Actor H2 = H; r2.env.actor = &H2;
                const AiOut o = m720(H2.ai, hp, r2.env);
                shot_ok = shot_ok && o.bullets.size() == 1 && o.set_marker == 0;
                if (!shot_ok) break;
                const AiBullet& b = o.bullets[0];
                shot_ok = shot_ok && b.owner == 304 && b.speed == 0.075f && b.accel == 0.0001f && b.home_period == 2 &&
                          b.home_rate == 0.15707964f && std::fabs(b.pos.x - 1.0f) < 1e-6f && std::fabs(b.pos.y - 1.575f) < 1e-6f;
                if (b.kind == 0xb) { ++heals; shot_ok = shot_ok && b.effect == 314 && b.power == 20.0f && b.knock == 5.0f && b.f168 == 1.0f && b.f16c == 150.0f && b.f170 == 30.0f; }
                else { ++attacks; shot_ok = shot_ok && b.kind == 1 && b.effect == 508 && b.power == 5.0f && b.knock == 2.0f; }
            }
            std::printf("[aitest]   M720 shots (Normal) of 300: %d heals (kind 0xb, 314), %d attacks (kind 1, 508)\n", heals, attacks);
            check(picked && run && cast, "M720 scan: the nearest monster missing 1 HP or more within 15 u with a clear line -> +0x888, "
                                         "sub 2 (run, steer 4); within 5 u request(7), sub 3");
            check(shot_ok && attacks > 35 && attacks < 85, "M720 Marker(5080314): one shot owned by the ally, 1 u ahead and 0.5 up — the "
                                                           "kind-0xb heal (150) or, 20 % on Normal, the kind-1 attack (power 5)");
            r.env.section = 2; r.env.aso_marker = 0;
            m720(H.ai, hp, r.env);
            check(H.ai.i(0x79c) == 0 && H.ai.i(0x888) == 0 && H.ai.cool() >= 60 && H.ai.cool() <= 119,
                  "M720 cast over: sub 0, the ally dropped, +0x700 = R%60 + 60");
        }
        {   // a wall hides every candidate: no ally
            Rig r; W w; Actor H; setup(r, w, H, 9.0f); r.env.section = 2; w.wall = true;
            wounded(w, 304, 4.0f, 100.0f);
            H.ai.dur() = 7;
            m720(H.ai, hp, r.env);
            check(H.ai.i(0x888) == 0 && H.ai.i(0x79c) == 0, "M720 scan: no clear line, no ally");
        }
        {   // the flee inside 4 u, the fan inside 12 u and 60 deg
            int flee = 0, fan = 0, fan_far = 0; bool ok = true;
            for (int s = 0; s < 400; ++s) {
                for (float pd : {3.0f, 9.0f, 13.0f}) {
                    Rig r; W w; Actor H; for (int i = 0; i < s; ++i) r.rng();
                    setup(r, w, H, pd); r.env.section = 2;
                    const AiOut o = m720(H.ai, hp, r.env);
                    if (pd < 4.0f) flee += H.ai.i(0x79c) == 1;
                    else if (pd < 12.0f) {
                        if (o.section == 8) { ++fan; ok = ok && H.ai.i(0x79c) == 5 && !o.section_force; }
                        else ok = ok && H.ai.dur() >= 10 && H.ai.dur() <= 19 && (H.ai.i(0x7a0) ? H.ai.action() == 0x20 && H.ai.steer() == 5 : H.ai.action() == 2);
                    } else fan_far += o.section == 8;
                }
            }
            std::printf("[aitest]   M720 of 400: flee at 3 u x%d, the fan at 9 u x%d, at 13 u x%d\n", flee, fan, fan_far);
            Rig r; W w; Actor H; setup(r, w, H, 9.0f);
            H.ai.i(0x79c) = 5; r.env.section = 2; r.env.difficulty = 1;
            m720(H.ai, hp, r.env);
            check(ok && flee > 80 && fan > 150 && fan < 235 && fan_far == 0 && H.ai.i(0x79c) == 0 && H.ai.i(0x7a4) >= 74 && H.ai.i(0x7a4) <= 133,
                  "M720 idle: inside 4 u 40 % a tick flees; inside 12 u and 60 deg 48 % request(8) (sub 5), else a walk on a coin; "
                  "the fan over -> cooldown R%60 - 15d + 90");
        }
    }
    // ---- M690 Glaregrave: the combo and its chain, the guard, the hops, STR / tint (build/ai_spec/M690.md §8 test) ----
    {
        const EnemyBrain m690 = enemy_brain_for(62);
        check(m690 != nullptr && enemy_handler_far_freeze(62), "Glaregrave registered (AI 62), in the 25-u far freeze");
        EnemyAiParams gp = params(0.14f, 0.0777f, 1.0f, 2.0f, 0.0f);   // EI[59]
        auto engaged = [&](Rig& r, Actor& G, float dist) {
            G.instance_id = 73; G.cast_id = 73; G.ai_index = 62; G.atk = 100.0f;
            r.env.actor = &G; r.env.target = {dist, 1.075f, 0.0f}; r.env.hp = 400; r.env.max_hp = 400;
            m690(G.ai, gp, r.env);                                   // INIT (its HP change: +0x7b4 = 2 - 0.075)
            G.ai.phase() = 2; G.ai.heading = 0.0f; G.ai.f(0x7b4) = 0.0f;
            G.ai.f(0x734) = dist; G.ai.f(0x73c) = 0.0f; G.ai.dist() = dist;
            G.ai.i(0x87c) = 21;
        };
        int n7 = 0, hops = 0; bool ok = true;
        for (int s = 0; s < 300; ++s) {
            Rig r; Actor G; for (int i = 0; i < s; ++i) r.rng();
            engaged(r, G, 3.0f); r.env.section = 2;
            const AiOut o = m690(G.ai, gp, r.env);
            if (o.section == 7) { ++n7; ok = ok && o.section_force && G.ai.i(0x72c) == 1 && G.ai.action() == 2; }
            else if (!G.vert.grounded) ++hops;
            else ok = ok && o.section < 0 && G.ai.action() == 2;
        }
        std::printf("[aitest]   M690 at 3 u: force(7) x%d, idle hops x%d of 300\n", n7, hops);
        check(ok && n7 > 65 && n7 < 115 && hops < 10, "M690 inside 4.5 u and 72 deg: 30 % a tick force(7); it stands (action 2)");
        {   // ATTACKING: the lunge at Marker 1, the cooldown each tick, the chain below half HP
            Rig r; Actor G; engaged(r, G, 3.0f);
            r.env.section = 7; r.env.aso_frame = 2; r.env.aso_marker = 1;
            const AiOut o = m690(G.ai, gp, r.env);
            const int c = G.ai.cool();
            const bool lunge = o.set_marker == 0 && std::fabs(G.ai.f(0x7ac) - 0.36f) < 1e-6f &&
                               std::fabs(G.mover.k2.x - 0.4f) < 1e-6f && G.mover.k2.z == 0.0f &&
                               c >= 90 && c <= 149 && G.ai.dur() == (c >> 2) + (c >> 1) && G.ai.action() == 1;
            r.env.aso_marker = 11;
            const bool no_chain = m690(G.ai, gp, r.env).section < 0;         // full HP
            r.env.hp = 199; r.env.target = {0.0f, 1.075f, -3.0f};
            G.ai.f(0x734) = 0.0f; G.ai.f(0x73c) = -3.0f; G.ai.turn_hold() = 1;
            const AiOut ch = m690(G.ai, gp, r.env);
            check(lunge && no_chain && ch.section == 8 && ch.section_force && std::fabs(G.ai.heading + 1.5707964f) < 1e-5f,
                  "M690 section 7: Marker 1 -> +0x7ac 0.4 on channel 2 (x0.9), cooldown R%60 - 20d + 90 with 3/4 locked; "
                  "below half HP Marker(11) snap-turns and forces 8");
        }
        {   // the guard: the damage accumulator starts it, blocked hits refresh and break it
            int guards = 0, back = 0, fwd = 0;
            for (int s = 0; s < 300; ++s) {
                Rig r; Actor G; for (int i = 0; i < s; ++i) r.rng();
                engaged(r, G, 9.0f); r.env.section = 1; G.ai.dur() = 5;
                r.env.hp = 380; m690(G.ai, gp, r.env);                // +2
                r.env.hp = 360;                                       // +2 again: 3.925 > 3
                const AiOut o = m690(G.ai, gp, r.env);
                if (o.section == 6) { ++guards; if (!(G.ai.dur() >= 60 && G.ai.dur() <= 119 && G.ai.f(0x7b4) == 0.0f)) guards = -1000; }
                else if (!G.vert.grounded) (G.ai.f(0x7ac) < 0.0f ? back : fwd) += 1;
            }
            std::printf("[aitest]   M690 after two damaged ticks: guard x%d, backstep x%d, leap x%d of 300\n", guards, back, fwd);
            Rig r; Actor G; engaged(r, G, 3.0f);
            r.env.section = 6; G.ai.dur() = 3; r.env.was_hit = true; r.env.took_damage = false;
            bool counted = true;
            for (int n = 1; n <= 9; ++n) {
                const AiOut o = m690(G.ai, gp, r.env);
                counted = counted && o.section < 0 && G.ai.i(0x878) == n && G.ai.dur() >= 60 && G.ai.dur() <= 119;
            }
            const AiOut br = m690(G.ai, gp, r.env);                   // the 10th tick: +0x878 = 9 > d + 8
            check(guards > 170 && guards < 230 && back > 25 && fwd > 25 && counted && br.section == 2 && br.section_force &&
                  G.ai.i(0x878) == 0 && G.ai.dur() == 0,
                  "M690 guard: accumulator over 3 - d -> 4 in 6 force(6) for 60..119, else a backstep / leap; a blocked hit "
                  "refreshes it, d + 9 blocks -> force(2)");
        }
        {   // STR and the tint from the HP fraction
            Rig r; Actor G; engaged(r, G, 9.0f); r.env.section = 1; G.ai.dur() = 5;
            r.env.hp = 200;                                           // r = 0.5: k1 = 0.75
            m690(G.ai, gp, r.env);
            check(std::fabs(G.atk - 101.875f) < 1e-4f && G.tint == 0xFFBDBDEFu,
                  "M690 every tick: STR = base (1 + 0.025 (1 - r^2)), tint 0xFF(255-k)(255-k)(255-k/4), k = 66 at half HP");
        }
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
