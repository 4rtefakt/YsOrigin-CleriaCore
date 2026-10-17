// The hits on the player (included inside the game loop, twice): her DEF, the Untouchable test, the death, the
// hit itself (hurt_player: FUN_004fd350's player branch), Chr_Damage, then every monster's armed hit volume against
// her. The live tick includes it; so does the frozen branch under a Fix that holds slot 0 only (FIX_SLOTS.md S5c):
// the monsters' handlers run there, and the hit path reads neither fix counter (§1.4) — a monster can hit a player
// whose input is zeroed. Her i-frames do not count down under a Fix (the mover's no-fix block), so they are not
// ticked there.
            // Curse: FUN_004fa740 reads DEF - 5.0, no floor [EXPORT 004fa740.c:20-23] (STATUS_AILMENTS.md §6.4).
            const float pdef = player_stats().def - (ail.has(ailments::kCurse) ? ailments::kCurseStat : 0.0f);
            // A hit landing on the player: FUN_004fd350's player branch (src/cleria/hit.hpp).
            // Damage is FUN_004fa740 with the swing's own power as the bonus (attacker +0xfc
            // = GenerateCollision arg[11]), halved during Boost; then the severity reaction —
            // i-frames 8/12/16, hurt section 4/0xb/9, knockback 0.1 (+1.2 on a knockdown)
            // along `angle` (atan2(dz,dx) from the attacker to the player), and +dmg*100
            // Boost gauge. `rtype` is the swing's arg[9].
            // The player's death, whatever the cause (a hit, lava, drowning): FUN_0046f810.
            // With g_flags[133] (a losable duel) the fight ends instead of the game: 156 = 0,
            // 130 = 0 (174 stays 0, the scripts' "lost" branch), the boss gauge hidden, HP 1,
            // i-frames 9999, the motion fields zeroed — the velocity, the displacement and the script force
            // +0x5d0..+0x5dc [EXPORT 0046f810.c:14-33] — and the stats recomputed (FUN_00420c40, on demand here).
            // Otherwise the player is dead: the death count and the game over (death_t above).
            // FUN_004fe9e0 L31-34: a hit needs the victim's +0x60 & 0x80 AND its Untouchable byte
            // (+0x9d7, ASO funcid 28) clear — the player's skill sections set it.
            const bool p_untouchable = avm.untouchable() || gs_ramp < 1.0f ||
                                       player_vanished ||        // 0x14d: +0x60 & 0x80 cleared
                                       boss6_grab;               // ... and by a boss06 head's grab (view_boss06.inl)
            auto player_death = [&] {
                if (duel::losable_death(game.g_flags, player.hp, player_iframes)) {   // src/cleria/duel.hpp
                    hud.hide_boss_gauge();
                    player_kb = {}; pvel = {};               // +0x32c/+0x334/+0xa3c/+0xa44 zeroed
                    csHost.pforce = {};                      // +0x5d0 / +0x5d4 / +0x5d8 / +0x5dc
                    content_duel_lost();                     // mods: this 156 = 0 is not a BossDefeated (view_content.inl)
                    std::printf("  [duel] lost: flag 133 set, HP 1, 156 / 130 cleared (FUN_0046f810)\n");
                    return;
                }
                if (death_t == 0) {                          // +0x3fc 0 -> 1: the first dead step
                    death_t = 1;
                    player_kb = {}; pvel = {};               // +0x32c/+0x334/+0xa3c/+0xa44 zeroed
                    csHost.pforce = {};                      // +0x5d0 / +0x5d4 / +0x5d8 / +0x5dc
                    // [H] the death section 5 (the hit reaction's state for a lethal hit); for lava /
                    // drowning the setter is not traced.
                    avm.set_section(5, /*force*/true);
                    std::printf("  [death] HP %d at (%.2f, %.2f, %.2f), section %d\n", player.hp, feet.x, feet.y,
                                feet.z, avm.section());
                    content_on_death();                      // mods: PlayerDeath (view_content.inl)
                }
            };
            // The shell's Cheats (not retail): refilled before the death check.
            if (cheat_hp) player.hp = player.max_hp;
            if (cheat_mp) player.sp_cur = player.sp_max;
            if (cheat_boost && !player.boosting) player.boost = skillmp::kBoostFull;
            if (cheat_o2) oxy.o2 = 6000.0f;
            if (player.hp < 1) player_death();                // lava / noise / drowning this tick
            bool hurt_direct = false;                         // a brain's own FUN_004fd350 call: no FUN_004fe9e0 gate
            // `from_cast` / `from_level`: the monster whose hit volume it is (the Lua mods' "damage" query), -1 = none.
            auto hurt_player = [&](float atk, float bonus, int rtype, float angle, int status = 0, int from_cast = -1,
                                   int from_level = 0) {
                if (p_untouchable && !hurt_direct) return;
                // Hugo's Force Shield (FUN_004fd350 0x4fd9d4 -> 0x4fd90e): a charge left blocks the
                // hit: damage 0, i-frames at least 16, SE 121, the "GUARD" popup (0xffaa00, scale
                // 0.75, origin + 0.75; English 0x69bf04), knockback 0.1 [ABILITIES_VERIFIED §3.2].
                // The break follows on the next shield tick. [not modelled] the +0x7e0 guard (effect 0x90).
                if (character == 2 && shield_on && shield_hits > 0) {
                    --shield_hits;
                    player_iframes = std::max(player_iframes, 16);
                    player_white_flash(hitfx::kGuardFlashStep);   // +0x264..+0x274
                    audio.play(0x79, kSfxAnimVol);
                    if (popups.size() < 32)
                        popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                    {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                    // ...then on to the tail's red damage number with 0 (0x4fda7d -> 0x4fe120).
                    damage_popup(0, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                    const Impulse k = knock_impulse(angle, avm.section() == 9 ? 0.0f : 0.1f);   // k = 0 in section 9 (0x4fd385)
                    player_kb.x += k.x; player_kb.z += k.z;
                    return;
                }
                // Hugo's super barrier: while +0x7e0 stands and the Force Shield did not take the hit, a GUARD
                // with no counter and no MP [BYTES 0x4fd9fc..0x4fda7d].
                if (character == 2 && fx_live(barrier_fx)) {
                    player_iframes = std::max(player_iframes, 16);
                    player_white_flash(hitfx::kGuardFlashStep);
                    audio.play(0x79, kSfxAnimVol);
                    if (popups.size() < 32)
                        popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                    {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                    damage_popup(0, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                    const Impulse k = knock_impulse(angle, avm.section() == 9 ? 0.0f : 0.1f);   // k = 0 in section 9 (0x4fd385)
                    player_kb.x += k.x; player_kb.z += k.z;
                    return;
                }
                const int raw = player_incoming_damage(AttackStats{atk, bonus, 0}, pdef,
                    [&](float p) { return float(rng_pct(rng)) < p; },
                    IncomingMode{game.g_flags[144], game.g_flags[143] != 0, game.g_flags[201] != 0});   // DIFFICULTY.md G8
                int dmg = access::damage_taken(player_damage_after_boost(raw, player.boosting),
                                               settings().s.access.damage_taken);   // the shell's Cheats
                // The MP super-armour of Yunica's 0x10c / 0x10f and Toal's 0x10e / 0x111 (Marker 0): with the MP
                // it is spent and the hit is the GUARD above (0x4fd90e); without, 16 i-frames and x 0.75
                // (skillmp::armor, BOOST_SKILLS.md §3.3).
                if (const skillmp::Armor ar = skillmp::armor(character, avm.section(), avm.marker(), player.sp_cur, skin);
                    ar.active) {
                    player_iframes = std::max(player_iframes, skillmp::kArmorIframes);
                    if (ar.guard) {
                        player.sp_cur -= ar.spend;
                        player_white_flash(hitfx::kGuardFlashStep);
                        audio.play(0x79, kSfxAnimVol);
                        if (popups.size() < 32)
                            popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                        {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                        damage_popup(0, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                        const Impulse k = knock_impulse(angle, avm.section() == 9 ? 0.0f : 0.1f);   // k = 0 in section 9 (0x4fd385)
                        player_kb.x += k.x; player_kb.z += k.z;
                        return;
                    }
                    dmg = int(float(dmg) * ar.scale);
                }
                dmg = lua_damage(false, dmg, raw, atk, bonus, pdef, -1, from_cast, from_level, -1, float(player.hp), float(player.max_hp));   // Lua mods
                player.hp -= dmg;
                if (player.hp < 1) player.hp = 0;             // FUN_004fd350: hp - dmg < 1.0 -> hp = 0 (no "-3/60")
                if (lua_events_on()) {                        // mods (API 7): the hit as it landed
                    events::Event he = events::Event::named("player_hit");
                    he.with("damage", dmg).with("hp", player.hp).with("max_hp", player.max_hp);
                    if (from_cast >= 0) he.with("cast", from_cast).with("level", from_level);
                    content_emit(he);
                }
                if (dmg > 0) player_hurt_this_tick = true;    // +0x3bc |= 6 (FUN_004fd350 0x4fde3c)
                // 0x4fe014 (dmg > 0, after the reaction's writes, which draw nothing): FUN_004fab40(victim,
                // 0xff550a00) = five puffs at the origin - (cos, sin)(the hit angle, 256 steps) x the hurt radius
                // +0x68, size 0.3, radius 0.45, speed 0.025, layer 9 in water [BYTES 0x4fab40..0x4fabef].
                if (dmg > 0) {
                    const Cast* hc = casts.get(13);
                    const float hr = hc ? hc->hurt_radius : 0.45f;
                    const int hi = int(angle * 40.743664f);
                    dust_at("hurt", 5, 0xff550a00u, false,
                            {feet.x - cos_table(hi & 0xff) * hr, feet.y + kOriginH, feet.z - cos_table((hi - 0x40) & 0xff) * hr},
                            0.3f, 0.45f, 0.025f, submerged);
                    // ... then, at BloodyEffectLevel 2, three blood splats (18 draws) [BYTES 0x4fe02a..0x4fe063].
                    // [H] the position [ebp-0x58] is taken as the player's origin.
                    if (settings().s.game.bloody_effect_level > 1)
                        blood_at("hurt", 3, 0xff550a00u, 2, 0x18, 1.3f, 1.8f, {feet.x, feet.y + kOriginH, feet.z}, 0.65f,
                                 false, false);
                }
                // FUN_004fd350's tail 0x4fe10b..0x4fe120: FUN_004fae00(0xffff0000), the red number of
                // the damage taken, at the player's origin + 0.5. PLAYTEST_1_HUD.md item 3.
                damage_popup(dmg, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                const bool died = player.hp < 1;              // the engine's test is < 1.0
                ail_on_hit(status, dmg);                      // the record's +0x10b (0x4fdc58..0x4fde0c; none on a kill / 0)
                const PlayerReaction re = player_reaction(dmg, float(player.max_hp), rtype, died,
                                                          avm.durable(), avm.section());
                if (ai_debug)
                    std::printf("[hurt] dmg %d state %d grant %d pif %d psec 0x%x\n", dmg,
                                re.motion_state, re.iframes, player_iframes, avm.section());
                if (std::getenv("CLERIA_DMG"))
                    std::printf("  [hurt] dmg %d (atk %.0f + %.1f vs DEF %.0f) reaction section %d i-frames %d knock %.2f at %.1f deg\n",
                                dmg, atk, bonus, pdef, re.motion_state, re.iframes, re.knockback, angle * 57.29578f);
                if (re.iframes > 0) player_iframes = std::max(player_iframes, re.iframes);
                if (re.motion_state >= 0 && re.motion_state != 5) avm.set_section(re.motion_state);
                pad_rumble(pad::hit_effect(re.motion_state));   // FUN_005d3500 0x4fde93..0x4fdfb5 (view_pad.inl)
                if (re.face_attacker)
                    face_dir = {std::cos(angle + 3.14159265f), 0, std::sin(angle + 3.14159265f)};
                const Impulse k = knock_impulse(angle, re.knockback);
                player_kb.x += k.x; player_kb.z += k.z;
                if (re.boost_gain > 0) add_boost(re.boost_gain);
                // p_damage is the player's own hurt-reaction sound; death adds p_hit0.
                if (dmg > 0) audio.play(kSfxPlayerHurt, kSfxAnimVol);
                if (died) audio.play(kSfxDeathEntry, kSfxAnimVol);
                hud.debug_message("-" + std::to_string(dmg) + " HP");
                if (died) player_death();
            };
            // FUN_004fe1a0, the alt applier of a boss-fight record with +0x11c & 8 (FUN_004fe9e0 0x4fec7a,
            // FUN_00505640): it takes NO HP (VELAGUNDER.md U3, [C-DIS] 0x4fe1a0..0x4fe9db). One unused draw
            // (0x4fe1e6); the skill-section guards, which cancel nothing (0x4fe31a..0x4fe53d); the status
            // loops (path B); the hurt bits; then a big reaction always, unless in section 5 or anime_Durable.
            // [not modelled] the leg guard of equip bit 0x200 (no item sets it), Hit_NotifyAttacker, +0x4d4.
            auto hurt_player_alt = [&](float angle, int status = 0) {
                if (p_untouchable) return;
                float k = avm.section() == 9 ? 0.0f : 0.1f;    // 0x6bd19c, 0 in section 9
                (void)pick_rng.next();                         // 0x4fe1e6..0x4fe225: the value is unused
                bool guard = false;
                if (character == 2) {                          // the Force Shield: one charge, no barrier test
                    if (shield_on && shield_hits > 0) { --shield_hits; guard = true; }
                    if (guard) player_iframes = std::max(player_iframes, 16);
                } else if (const skillmp::Armor ar = skillmp::armor(character, avm.section(), avm.marker(), player.sp_cur, skin);
                           ar.active) {
                    player_iframes = std::max(player_iframes, skillmp::kArmorIframes);
                    if (ar.guard) { player.sp_cur -= ar.spend; guard = true; }
                }
                if (guard) {                                   // the white flash, SE 121, "GUARD": no damage number
                    player_white_flash(hitfx::kGuardFlashStep);
                    audio.play(0x79, kSfxAnimVol);
                    if (popups.size() < 32)
                        popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                    {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                }
                if (status != 0)                               // 0x4fe666..0x4fe7d7 (the HP is >= 1: both passes)
                    ailments::on_hit_flag8(std::uint32_t(status), float(player.hp), game.g_flags[144],
                                           [&] { return int((pick_rng.next() >> 17) % 100u); }, ail_apply);
                player_hurt_this_tick = true;                  // +0x3bc |= 6, whatever the damage (none)
                const int sec = avm.section();
                if (sec != 5) {
                    player_iframes = std::max(player_iframes, 16);
                    if (!avm.durable()) {                      // 0x4fe804..0x4fe8d2: the record's rtype is not read
                        k += 1.2f;                             // 0x6bd484
                        pad_rumble(pad::hit_effect(9));        // FUN_005d3500(3)
                        face_dir = {std::cos(angle + 3.14159265f), 0, std::sin(angle + 3.14159265f)};
                        avm.set_section(9);
                    }
                }
                const Impulse kk = knock_impulse(angle, k);    // 0x4fe8d8..0x4fe924
                player_kb.x += kk.x; player_kb.z += kk.z;
                if (std::getenv("CLERIA_DMG"))
                    std::printf("  [hurt] alt applier (no HP) section %d -> %d knock %.2f at %.1f deg%s\n", sec,
                                avm.section(), k, angle * 57.29578f, guard ? " GUARD" : "");
            };
            #include "view_xso13_damage.inl"     // 0x15c Chr_Damage on the player (FLOOR13_OPS.md)
            prof("tick:enemy-hits-player");
            for (EnemyRender& e : render) {
                Actor* en = e.actor;
                // An enemy hurts only through an armed volume: the body pass (FUN_0045e460) only
                // pushes, it never damages (HITVOLUME.md §5), so there is no contact damage.
                if (e.atk_win <= 0) continue;
                --e.atk_win;
                if (ai_debug && en)
                    std::printf("[swing] #%d win %d hit %d pif %d psec 0x%x box(%.2f,%.2f r%.2f) "
                                "dir(%.2f,%.2f) d_player %.2f\n", en->instance_id, e.atk_win,
                                int(e.atk_hit), player_iframes, avm.section(),
                                volume_centre(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing).x,
                                volume_centre(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing).z,
                                e.hb.radius, e.atk_dx, e.atk_dz,
                                std::sqrt((feet.x - en->pos.x) * (feet.x - en->pos.x) +
                                          (feet.z - en->pos.z) * (feet.z - en->pos.z)));
                // FUN_004fed50 mode 1 -> FUN_004fe9e0: the player alone (no team mask). The
                // volume follows the attacker: re-placed from its origin and visible facing
                // each tick. P+0x734 (player_iframes) guards; the player's bank keys on the
                // enemy's spawn serial, so its re-hits are paced only by the timer.
                if (en && en->alive() && !g_lua_targets.empty()) {   // the mods' other players (API 7): the mod is told, they take no HP here
                    const HitShape tv = volume_shape(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing);
                    const Cast* tc = casts.get(13);
                    for (int pid : g_lua_targets) {
                        Actor* pu = world.find(pid);
                        if (!pu) continue;
                        const HitShape tb = body_shape({pu->pos.x, pu->pos.y + kOriginH, pu->pos.z}, tc ? tc->hurt_radius : 0.45f,
                                                       tc ? tc->hurt_half_h : 0.5f, tc ? tc->hurt_shape : 1, tc ? tc->center_offset : -0.25f);
                        if (!overlap(tv, tb) || pu->bank.check_and_record(en->hit_serial, e.hb.rehit)) continue;
                        content_emit(events::Event::named("puppet_struck").with("puppet", pid).with("actor", en->instance_id).with("cast", en->cast_id)
                                         .with("level", en->level).with("atk", en->atk).with("power", e.hb.power));
                    }
                }
                if (!en || !en->alive() || player_iframes > 0 || p_untouchable) continue;
                const HitShape vol = volume_shape(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing);
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                float hang = 0;
                if (!overlap(vol, pbody, &hang)) continue;
                if (player_bank.check_and_record(en->hit_serial, e.hb.rehit)) continue;
                if (e.hb.one_shot) e.atk_win = 0;
                e.atk_hit = true;
                const float php_before = player.hp;
                hurt_player(en->atk, e.hb.power, e.hb.rtype, hang, e.hb.status, en->cast_id, en->level);
                // M470's lifesteal (FUN_0048de00 0x48e103, M470.md §2): a hit landed in section 8 heals it by the HP the
                // player lost (at least 1) when it is down by 1 HP or more, with a green number (FUN_004fae00, 0xff00ff00)
                // at its origin.
                const Vec3 en_origin{en->pos.x, en->pos.y + en->height, en->pos.z};
                if (en->ai_index == 40 && e.avm.section() == 8 && en->max_hp - 1.0f >= en->hp) {
                    const int n = int(std::max(1.0f, php_before - player.hp));
                    damage_popup(n, 0x00ff00, en_origin);
                    en->hp = std::min(en->max_hp, float(n) + en->hp);
                }
                if (en->ai_index == 21 && en->max_hp > en->hp) {   // M280's lifesteal (FUN_004a5300 0x4a55b7, M280.md §2)
                    const int bb5c = difficulty >= 2 ? difficulty - 2 : 0;
                    const int amt = std::max(1, int(std::floor(en->max_hp / (bb5c == 2 ? 7.5f : bb5c == 1 ? 14.0f : 20.0f))));
                    damage_popup(amt, 0x00ff00, en_origin);         // the green number, then the heal
                    en->hp = std::min(en->max_hp, en->hp + float(amt));
                }
            }
            // The boss parts' armed volumes (GoXx kind 9, mode-0 scan; VELAGUNDER.md §3.2): all
            // share re-hit token 0 with re-hit 30 [H-med], the damage with the boss's STR.
            if (boss && !boss->destroyed() && player_iframes == 0 && player.hp >= 1) {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                for (const Boss01Part& bp : boss->parts()) {
                    if (!bp.armed || bp.radius <= 0.0f) continue;
                    const HitShape vol{bp.radius, bp.centre, bp.half_h, bp.shape};
                    float hang = 0;
                    if (!overlap(vol, pbody, &hang)) continue;
                    if (player_bank.check_and_record(0xB0550000u, 30)) break;
                    const int hp0 = player.hp;
                    // FUN_004fe9e0 in a boss fight (0x76bb8c): +0x11c & 2 takes the knock angle 3 pi / 2 - the
                    // boss's yaw, & 8 runs FUN_004fe1a0 (the RUN parts 3, 4, 15..20: a knock-down, no HP).
                    const bool fight = game.g_flags[156] != 0;
                    if (fight && (bp.flags & 2u)) hang = 4.71238899f - boss->yaw();   // 0x6bd858
                    if (fight && (bp.flags & 8u)) hurt_player_alt(hang);
                    else hurt_player(boss->str(), bp.power, bp.rtype, hang);
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  [hurt] t%llu velagunder part %d (STR %.0f power %.1f rtype %d r %.2f) player hp %d -> %d\n",
                                    (unsigned long long)tick_count, int(&bp - &boss->parts()[0]), boss->str(), bp.power,
                                    bp.rtype, bp.radius, hp0, player.hp);
                    player_hurt_this_tick = true;
                    break;
                }
            }
            #include "view_boss01_touch.inl"        // Velagunder's body entries (VELAGUNDER.md §3.4)
            #include "view_boss06_touch.inl"        // Zava's heads: the touch push, the direct damage calls (ZAVA_D.md §7.2)
            // Nygtilger's touch (LAB_00513390, §3.6): the player in contact with a piece's live
            // collision hull [approx: a sphere of r 0.7 at the origin, not the mover's contact
            // record], then the piece's verdict, the shared re-hit record (+0xf0/+0xf4 = 15, one
            // for all pieces), the contact damage with the piece's STR [U2: power 0, rtype 0, as
            // FUN_004fd350 is not traced], then touch_applied. [not modelled] its section-9 reaction.
            if (boss2 && player_iframes == 0 && player.hp >= 1) {
                const RoomMesh& b2m = phys_mesh(room);
                const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
                for (int o = 0; o < boss02::kObjects; ++o) {
                    const char* nm = boss02::object_name(o);
                    const MeshObj* mo = meshes.find(nm);
                    const RoomMesh::ObjRange* rr = b2m.object_range(nm);
                    if (!mo || !rr || mo->no_collide) continue;
                    if (!sphere_overlaps_tris(b2m, O, 0.7f, rr->t0, rr->t1)) continue;
                    const Boss02Touch t = boss2->touch(o, float(player.hp));
                    if (!t.damage) continue;
                    if (player_bank.check_and_record(0xB0520000u, t.rehit)) break;
                    const Vec3 pp = boss2->object(o).pos();
                    hurt_player(t.str, 0.0f, 0, std::atan2(O.z - pp.z, O.x - pp.x));
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  touch %s (STR %.0f): player hp %d\n", nm, t.str, player.hp);
                    boss2->touch_applied(o, float(player.hp));
                    break;
                }
            }
            // Gelaldy's armed parts: the BEAT arm cylinders (+0x19c, r 3, half-height 3), the
            // damage with the boss's STR and the part's power / rtype. [H] one shared re-hit token
            // with re-hit 30, as Velagunder's parts.
            if (boss3 && !boss3->destroyed() && player_iframes == 0 && player.hp >= 1) {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                for (int pi = 0; pi < boss03::kParts; ++pi) {
                    const Boss03Part& bp = boss3->part(pi);
                    if (!bp.armed || bp.radius <= 0.0f) continue;
                    const HitShape vol{bp.radius, bp.centre, bp.half_h, bp.shape};
                    float hang = 0;
                    if (!overlap(vol, pbody, &hang)) continue;
                    if (player_bank.check_and_record(0xB0530000u, 30)) break;
                    hurt_player(boss3->str(), bp.power, bp.rtype, hang);
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  gelaldy arm %d hits the player: hp %d\n", pi, player.hp);
                    break;
                }
            }
