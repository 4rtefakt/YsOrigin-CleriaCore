// The bosses' parts as the chain lightning's targets (GOXX_17_19.md §3.1 / §3.6); included in render_window after
// the bosses' hosts (view_boss08.inl).
//
// FUN_00591f60's second scan [BYTES 0x5921d7..0x5923d0]: every sub-part (+0x27c, count +0x278) of every GoMesh of
// type 5..0xc (the eight bosses' meshes) whose +0x60 has 0x80 and one of 0x18 (the parts are made with 0x88), that
// no plane of the active camera's frustum has wholly behind it (dist <= -radius, radius = +0x68), and that is
// **12.0 or more** from the scan's centre: `cmp d, 0x3840 ; jl skip` [BYTES 0x59232c..0x592336] — the reverse of
// the monsters' test eight instructions earlier (`jge skip`, 0x59215f). A part nearer than 12.0 is not a target;
// with no target at all the skill is the ray, which scans the actor list only. No HP test at the scan.
// The arrival's hit is FUN_004faf20(chain, part, 1, 0) [BYTES 0x592d00.. per §3.3; EXPORT 004faf20.c:86-112]: flag 1
// = no overlap test; the gates are +0x60 & 0x80 and HP >= 1.0; the part's bank on the chain's token 0 / timer 0 never
// blocks; the attacker is the player (the chain is neither an actor nor class 0x0b); power -15.0, class 2.
// [H] the frustum planes are the view's last view-projection (the camera block's plane writer was not traced).
// [approx] a part is "there" by its boss's part_hittable (Dalles's and Darm's: while the fight flag 156 is up);
// the chain drops a target whose +0x98 is under 1.0 — the parts' pools are not read for that.
constexpr int kChainPart = 1 << 20;                       // a part's target id: kChainPart + 100 x boss + part
auto chain_part = [&](int id, Vec3* pos, float* radius) {
    const int b = (id - kChainPart) / 100, pi = (id - kChainPart) % 100;
    switch (b) {
    case 1:
        if (!boss || boss->destroyed() || !boss->part_hittable(pi)) return false;
        *pos = boss->part(pi).pos; *radius = boss->part(pi).hurt_radius;
        return true;
    case 2:
        if (!boss2 || !boss2->part_hittable(pi)) return false;
        *pos = boss2->object(pi).part.pos; *radius = boss2->object(pi).part.radius;
        return true;
    case 3:
        if (!boss3 || boss3->destroyed() || !boss3->part_hittable(pi)) return false;
        *pos = boss3->part(pi).pos; *radius = boss3->part(pi).hurt_radius;
        return true;
    case 4:
        if (!boss4 || boss4->destroyed() || !boss4->part_hittable(pi)) return false;
        *pos = boss4->part(pi).centre; *radius = boss4->part(pi).hurt_radius;
        return true;
    case 5:
        if (!boss5 || boss5->destroyed() || !boss5->part_hittable(pi)) return false;
        *pos = boss5->part(pi).centre; *radius = boss5->part(pi).radius;
        return true;
    case 7:
        if (!boss7 || boss7_gone || game.g_flags[156] == 0) return false;
        *pos = boss7->part(pi).pos; *radius = boss7->part(pi).radius;
        return true;
    case 8:
        if (!boss8 || game.g_flags[156] == 0) return false;
        *pos = boss8->part(pi).pos; *radius = boss8->part(pi).radius;
        return true;
    }
    return false;
};
chain_parts = [&](const Vec3& ctr, std::vector<extra::Chain::Target>& found) {
    auto in_view = [&](const Vec3& q, float r) {           // as the handlers' FUN_0045d3c0 (view_enemy_tick.inl)
        if (!have_vp) return true;
        const auto& m = last_vp.m;
        const float row[4][4] = {{m[0], m[4], m[8], m[12]}, {m[1], m[5], m[9], m[13]},
                                 {m[2], m[6], m[10], m[14]}, {m[3], m[7], m[11], m[15]}};
        for (int k = 0; k < 6; ++k) {
            float p[4];
            for (int j = 0; j < 4; ++j)
                p[j] = k == 4 ? row[2][j] : k == 5 ? row[3][j] - row[2][j]
                     : row[3][j] + (k & 1 ? -1.0f : 1.0f) * row[k >> 1][j];
            const float n = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (n > 0.0f && (p[0] * q.x + p[1] * q.y + p[2] * q.z + p[3]) / n <= -r) return false;
        }
        return true;
    };
    // The mesh list's order is not modelled: the bosses never share a room. Nygtilger's parts: the head's, the tail's.
    static const int kCount[9] = {0, boss01::kParts, boss02::kPieces, boss03::kParts, boss04::kParts, boss05::kParts, 0,
                                  boss07::kParts, boss08::kBodyParts + 1};
    for (int b = 1; b <= 8; ++b)
        for (int pi = 0; pi < kCount[b]; ++pi) {
            if (b == 2 && pi != 0 && pi != boss02::kPieces - 1) continue;
            Vec3 p{};
            float r = 0;
            const int id = kChainPart + 100 * b + pi;
            if (!chain_part(id, &p, &r) || !in_view(p, r)) continue;
            if (extra::Chain::d100(ctr, p) < 0x3840) continue;             // sic: the NEAR parts are skipped
            found.push_back({id, p});
        }
};
auto chain_part_hit = [&](int id, float power) {
    const int b = (id - kChainPart) / 100, pi = (id - kChainPart) % 100, cls = 2;
    Vec3 at{};
    float r = 0;
    if (!chain_part(id, &at, &r)) return;
    auto damage = [&](float def, const float* resist) {
        AttackStats a;
        a.atk = player_stats().atk;
        a.bonus = power;
        EnemyDefense vd; vd.def = def;
        for (int k = 0; k < 4 && resist; ++k) vd.resist[k] = resist[k];
        PlayerDamageMods mods;
        mods.atk_debuff = ail.has(ailments::kCurse);       // FUN_004fa550: ATK - 5, floor 0
        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
        return lua_part_damage(enemy_incoming_damage(a, cls, vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), a, vd.def, cls);
    };
    int dmg = 0;
    switch (b) {
    case 1: { const Boss01Part& bp = boss->part(pi); dmg = damage(bp.def, bp.resist);
              if (dmg > 0) boss->add_part_damage(pi, float(dmg)); boss_part_feedback(bp, dmg, cls); break; }
    case 2: { const Boss02Part& bp = boss2->object(pi).part; dmg = damage(bp.def, bp.resist);
              if (dmg > 0) boss2->add_part_damage(pi, float(dmg)); part_hit_feedback(bp.pos, true, dmg, cls); break; }
    case 3: { const Boss03Part& bp = boss3->part(pi); dmg = damage(bp.def, bp.resist);
              if (dmg > 0) boss3->add_part_damage(pi, float(dmg)); part_hit_feedback(bp.pos, true, dmg, cls); break; }
    case 4: { const Boss04Part& bp = boss4->part(pi); dmg = damage(bp.def, bp.resist);
              if (dmg > 0) boss4->add_part_damage(pi, float(dmg)); part_hit_feedback(bp.centre, bp.broken, dmg, cls); break; }
    case 5: { const Boss05Part& bp = boss5->part(pi); dmg = damage(bp.def, bp.resist);
              if (dmg > 0) boss5->add_part_damage(pi, float(dmg)); part_hit_feedback(bp.centre, true, dmg, cls); break; }
    case 7: boss7_hit(HitShape{}, 0u, 0, power, cls, /*swing*/false, nullptr, pi); break;
    case 8: boss8_hit(HitShape{}, 0u, 0, power, cls, /*swing*/false, nullptr, pi); break;
    }
    if (std::getenv("CLERIA_DMG") && b != 7 && b != 8)
        std::printf("  [extra] t%llu chain hits boss%02d part %d: %d dmg\n", (unsigned long long)tick_count, b, pi, dmg);
};
