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
}
