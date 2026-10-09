// The rest of FUN_004fed50's mode-0 scan for a PLAYER PARTICLE attacker (included in the fxsim.step hit
// callback, side 0, after the actors and Velagunder's parts; `p` the particle, `vol` its hit volume).
//
// FUN_005645b0 (the EF2 particle hit tick) calls FUN_004fed50(mode = side 0, record = p, origin = NULL), the
// same scan a swing runs (EFFECTHIT.md §4.2 / §5.1, HITVOLUME.md §2.3). After the actor pass it runs:
//   - the scene-mesh pass (0x4fef2e..): for an attacker whose vtable type is 0xe (a particle) a
//     sphere-vs-object-ORIGIN test: d = centre - obj+0x50 (dy scaled by r / half-height for a cylinder
//     narrower than it is tall), |d| < r * 0.8 (0x6bd3a8) + 1.0 (0x6bd438); the contact is one unit from the
//     origin toward the particle; then FUN_004fecb0(obj, entry 0, contact): obj+0x298 & 0x88 == 8, entry 0
//     struck-able (+0x8a), the accept-hit callback +0x480; each contact is hit through FUN_004faf20(1, 0).
//     In CleriaCore that is Nygtilger's pieces (boss02);
//   - the SOB sub-part pass (0x4ff6a7..0x4ff78c): every +0x27c sub-part of every GoMesh through
//     FUN_004faf20(0, 0), HP -= +0x134 and the object's script (FUN_00434970) when it drops below 1:
//     the 0x186 child spheres (the S_2008 / S_2015 walls, the S_3004 torches), Gelaldy's five parts and
//     Nygtilger's head / tail parts. CleriaCore's breakables pass (smash_breakables) stands for it, as
//     for the swing.
// Before this, a player particle (Hugo's shot, any player magic) reached only the monsters and
// Velagunder: Hugo could not break the S_2015 wall (the way to S_2010), light the S_3004 torches, smash a
// pot, or hurt Nygtilger / Gelaldy (build/audit/ROUTE_F13_HUGO.md).
// Token 0 for every bank (EFFECTHIT.md §5.4); the particle's power, class and re-hit.
{
    const int cls_p = p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone;
    auto fx_part_damage = [&](float def, const float* resist) {
        AttackStats a;
        a.atk = player_stats().atk;
        a.bonus = p.hit.power;
        EnemyDefense vd; vd.def = def;
        for (int k = 0; k < 4; ++k) vd.resist[k] = resist[k];
        PlayerDamageMods mods;
        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
        return access::damage_dealt(enemy_incoming_damage(a, cls_p, vd, mods,
            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);   // the shell's Cheats
    };
    // The SOB sub-part pass: the 0x186 child spheres and the @Break_* objects (CleriaCore's breakables pass).
    const int struck0 = mesh_struck;
    // The particle's own power and class (+0x109 from its emitter, FUN_0046d5e0 at spawn): a 0x186 child
    // takes FUN_004fa550 against the SOB block, so only the fire / thunder skills light a torch / break a wall.
    const int smashed = smash_breakables(vol, 0u, p.hit.rehit, false, p.hit.power, cls_p);
    fx_broke += smashed;
    if (smashed || mesh_struck != struck0) landed = true;
    // Gelaldy's parts (+0x27c sub-parts, GELALDY.md §2.3): the hurt sphere, the part's own bank.
    if (boss3 && !boss3->destroyed())
        for (int pi = 0; pi < boss03::kParts; ++pi) {
            if (!boss3->part_hittable(pi)) continue;
            const Boss03Part& bp = boss3->part(pi);
            if (!overlap(vol, body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f))) continue;
            if (boss3_banks[std::size_t(pi)].check_and_record(0u, p.hit.rehit)) continue;
            const int dmg = fx_part_damage(bp.def, bp.resist);
            if (dmg > 0) boss3->add_part_damage(pi, float(dmg));   // [not modelled] the element bonus on HP
            if (std::getenv("CLERIA_DMG"))
                std::printf("  fx %d hits gelaldy part %d (DEF %.0f): %d dmg, part HP %.0f body %.0f\n",
                            p.fx ? p.fx->id : -1, pi, bp.def, dmg, bp.hp, boss3->hp());
            landed = true;
        }
    if (boss2) {
        // Nygtilger's head / tail kind-9 parts (+0x27c sub-parts, NYGTILGER.md §3.1): a sphere, their own banks.
        for (int k = 0; k < 2; ++k) {
            const int o = k == 0 ? 0 : boss02::kPieces - 1;
            if (!boss2->part_hittable(o)) continue;
            const Boss02Part& pt = boss2->object(o).part;
            if (!overlap(vol, body_shape(pt.pos, pt.radius, 0.0f, 0, 0.0f))) continue;
            if (boss2_part_banks[std::size_t(k)].check_and_record(0u, p.hit.rehit)) continue;
            const int dmg = fx_part_damage(pt.def, pt.resist);
            if (dmg > 0) boss2->add_part_damage(o, float(dmg));
            if (std::getenv("CLERIA_DMG"))
                std::printf("  fx %d hits %s part (DEF %.0f): %d dmg, part HP %.0f pool %.0f\n", p.fx ? p.fx->id : -1,
                            boss02::object_name(o), pt.def, dmg, pt.hp, boss2->pool());
            landed = true;
        }
        // The scene-mesh pass, type 0xe: the particle against each struck-able piece's origin.
        // [approx] `mesh_hittable` tests the piece's entries as a whole, not entry 0 alone.
        for (int o = 0; o < boss02::kObjects; ++o) {
            if (!boss2->mesh_hittable(o)) continue;
            const char* nm = boss02::object_name(o);
            const MeshObj* mo = meshes.find(nm);
            if (!mo || mo->no_collide) continue;
            float dx = vol.centre.x - mo->pos.x, dy = vol.centre.y - mo->pos.y, dz = vol.centre.z - mo->pos.z;
            if (vol.shape == 1 && vol.radius < vol.half_h) dy *= vol.radius / vol.half_h;
            const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!(dl < vol.radius * 0.8f + 1.0f)) continue;
            if (dl > 0.0f) { dx /= dl; dy /= dl; dz /= dl; }
            const Vec3 hit{mo->pos.x + dx, mo->pos.y + dy, mo->pos.z + dz};
            Boss02Output fb;
            const bool ok = boss2->accept_hit(o, hit, std::uint32_t(SDL_GetTicks()), fb);
            boss2_apply(fb);
            if (!ok) continue;
            if (boss2_banks[std::size_t(o)].check_and_record(0u, p.hit.rehit)) continue;
            const Boss02Piece& bp = boss2->object(o);
            const int dmg = fx_part_damage(bp.def, bp.resist);
            if (dmg > 0 && bp.hp >= 1.0f) boss2->add_damage(o, float(dmg));   // [H] as the swing's gate
            if (std::getenv("CLERIA_DMG"))
                std::printf("  fx %d hits %s (DEF %.0f): %d dmg, HP %.0f pool %.0f\n", p.fx ? p.fx->id : -1, nm,
                            bp.def, dmg, bp.hp, boss2->pool());
            landed = true;
        }
    }
    // Khonsclard's eight parts and Pictimos's thirteen (+0x27c sub-parts; KHONSCLARD.md §A.2.3 / §A.3, PICTIMOS.md
    // §P1.4.1): the same sub-part pass [EXPORT 004fed50.c:296-345 -> FUN_004faf20(0, 0)] runs for a particle as for
    // the swing (view_boss04_hit.inl / view_boss05_hit.inl) — the part's hurt volume, its own bank on token 0, its
    // DEF (999 = no damage) / resist. Without it Hugo's shots passed through both bosses.
    if (boss4 && !boss4->destroyed())
        for (int pi = 0; pi < boss04::kParts; ++pi) {
            if (!boss4->part_hittable(pi)) continue;
            const Boss04Part& bp = boss4->part(pi);
            if (!overlap(vol, body_shape(bp.centre, bp.hurt_radius, bp.half_h, bp.shape, 0.0f))) continue;
            if (boss4_banks[std::size_t(pi)].check_and_record(0u, p.hit.rehit)) continue;
            const int dmg = fx_part_damage(bp.def, bp.resist);
            if (dmg > 0) boss4->add_part_damage(pi, float(dmg));
            if (std::getenv("CLERIA_DMG"))
                std::printf("  fx %d hits khonsclard part %d (DEF %.0f): %d dmg, part HP %.0f body %.0f\n",
                            p.fx ? p.fx->id : -1, pi, bp.def, dmg, bp.hp, boss4->hp());
            landed = true;
        }
    if (boss5 && !boss5->destroyed())
        for (int pi = 0; pi < boss05::kParts; ++pi) {
            if (!boss5->part_hittable(pi)) continue;
            const Boss05Part& bp = boss5->part(pi);
            if (!overlap(vol, body_shape(bp.centre, bp.radius, 0.0f, 0, 0.0f))) continue;
            if (boss5_banks[std::size_t(pi)].check_and_record(0u, p.hit.rehit)) continue;
            const int dmg = fx_part_damage(bp.def, bp.resist);
            if (dmg > 0) boss5->add_part_damage(pi, float(dmg));
            if (std::getenv("CLERIA_DMG"))
                std::printf("  fx %d hits pictimos part %d (DEF %.0f): %d dmg, body %.0f\n", p.fx ? p.fx->id : -1, pi,
                            bp.def, dmg, boss5->hp());
            landed = true;
        }
    // Dalles's parts (+0x27c sub-parts; DALLES_AB.md §B.2, view_boss07.inl): the element barriers are broken by the
    // skills' particles as by their swings — the particle's own class, power and re-hit, token 0.
    if (boss7_hit(vol, 0u, p.hit.rehit, p.hit.power, cls_p, /*swing*/false, nullptr)) landed = true;
    // Darm's nine parts (DARM_D.md §4.3, view_boss08.inl): the same path.
    if (boss8_hit(vol, 0u, p.hit.rehit, p.hit.power, cls_p, /*swing*/false, nullptr)) landed = true;
}
