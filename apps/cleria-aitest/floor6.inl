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
}
