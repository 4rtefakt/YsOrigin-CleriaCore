// The player's section events that make something (included inside the game loop, twice): anime_GenerateEffect,
// anime_ChangeSpecular, anime_GenerateCollision and anime_GenerateLocus of the VM step just run. The live tick
// includes it after her avm.tick(); so does the frozen branch for a section caught by a Fix (FIX_SLOTS.md S4):
// FUN_0046fba0 runs the action machine and the ASO on every logic step under either slot, with a zeroed input
// record [EXPORT 0046fba0.c:50-116; 0046f980.c:24-51], so a swing in progress still arms its box and its locus.
            // anime_GenerateEffect from the player's ASO (FUN_004496f0, as the enemies' below): at
            // origin + (cos, sin)(angle + facing) x fwd, y + voff, yaw angle - (facing + pi/2), the
            // player's side (0), class FUN_0046d5e0 (the section's), Hugo's 0x86 / 0x87 with rtype 3
            // and their handle kept as the Force Shield (+0x7c4). Their particles hit the monsters
            // in the hit tick below.
            prof("pa:effects");
            for (const AsoVm::EffectEvent& fe : avm.effects()) {
                const float pf = std::atan2(face_dir.z, face_dir.x);
                const Vec3 org{feet.x, feet.y + kOriginH, feet.z};
                const bool shield_id = character == 2 && (fe.id == 0x86 || fe.id == 0x87);
                ef2sim::SpawnParams sp;
                sp.power = fe.power; sp.knock = fe.knock; sp.status = fe.status;
                sp.rtype = shield_id ? 3 : fe.rtype;
                sp.cls = player_damage_class(avm.section());
                sp.atk = player_stats().atk; sp.side = 0; sp.owner = 13;
                ef2sim::Effect* fx = spawn_generate_effect(fe, org, pf, sp, 13);
                if (shield_id) { shield_fx = fx; shield_on = fx != nullptr; }
                if (character == 2 && fe.id == 0x90) barrier_fx = fx;   // +0x7e0 [EXPORT 004496f0.c:113-115]
            }
            // GenerateCollision fired: adopt this section's own box for its own lifetime.
            // anime_ChangeSpecular of the player's own set: the same struct the GUARD / Boost-ready flash uses —
            // the last writer owns it and restarts it at t = 0 (ASO_NOOPS.md §1.3; Hugo's s5 / s274 issue "off").
            prof("pa:hitbox+loci");
            if (const AsoVm::SpecularEvent* sv = avm.specular()) {
                player_spec.a = sv->a; player_spec.b = sv->b; player_spec.t = 0.0f; player_spec.step = sv->step; player_spec.count = sv->count;
            }
            if (avm.armed_hitbox()) {
                atk_hb = avm.hitbox();
                atk_cls = player_damage_class(avm.section());   // FUN_004494e0: cls at arm time
                atk_window = atk_hb.life > 0 ? atk_hb.life : 1;
                atk_dir = face_dir;
                player_hit_serial = next_hit_serial(world.hit_serial_counter_);
            }
            // GenerateLocus fired: spawn a GoLocusSword (FUN_00464960) on the character's SWORD file,
            // with the swing's token (+0x3c4) and the player-section damage class (FUN_0046d5e0).
            for (const AsoVm::LocusEvent& ev : avm.loci()) {
                LocusArgs la;
                la.row = ev.i[0]; la.a1 = ev.i[1]; la.a2 = ev.i[2]; la.a3 = ev.i[3];
                la.clank_lo = ev.i[4]; la.clank_hi = ev.i[5];
                la.bonus = ev.bonus; la.knock = ev.knock; la.life = ev.i[8]; la.rehit = ev.i[9];
                const int si = locus_sword_index(0xd, character, game.g_flags[149]);
                PlayerLocus pl;
                pl.cls = player_damage_class(avm.section());
                if (pl.sword.spawn(sl2set.sword[std::size_t(si)], la, player_hit_serial,
                                   game.g_flags[215], game.g_flags[217], character))
                    loci.push_back(pl);
            }
