// Hugo's Eyes of Fact as the player's familiars: the host side (included in render_window before skill_tick).
// The eyes are world actors of cast 0xb0 (M900) owned by the player (+0x854), driven by the m900 brain's
// FUN_00470210 branch (src/cleria/brains/hugo_eyes.cpp). Spec: build/ability_re/EYE_FAMILIARS.md §7..§12.
//
//   hugo_eyes_tick()      FUN_0046af60's Hugo branch (the spawner, the Fire Wheel's flag-158 countdown and
//                         its Fix cancel) and FUN_0046a3e0's charged blast; once per player tick
//   hugo_eyes_cast(m, c)  FUN_0046ea00's 0x75 (Trap Mine) / 0x76 (Fire Wheel) branches; false = no cast
//   hugo_eyes_fire()      FUN_0046d6e0: section 7 (the eye's shot) on each free eye, on Hugo's attacks
//   hugo_eye(a)           a player-owned eye (its bullets / effects are the player's side, class FUN_0046d650)
//   hugo_eye_dead(e)      the handler's death branch (FUN_004780e0 + the ASO tick) for a dead eye
// [not modelled] the eye SEs' Se_Stop (0x97 / 0x9c), the tints (+0x264..+0x274), the +0x248 / +0x144
// ramps, the Extra skin (DAT_0074c0d8), the castinfo live counts.
FamiliarHost fam;
auto hugo_eye = [&](const Actor& a) {
    const Actor* pa = world.player();
    return pa && hugo::owned_eye(a, pa->instance_id);
};
auto eye_render = [&](const Actor& a) -> EnemyRender* {
    for (EnemyRender& er : render) if (er.actor == &a) return &er;
    return nullptr;
};
auto eye_section = [&](const Actor& a) {
    const EnemyRender* er = eye_render(a);
    return er && er->avm.ok() ? er->avm.section() : -1;
};
// An eye's ASO reads the game flags (section 7: no shot while flag 158 runs; flag 208) and the player's
// gear: Flag_IsEquip = FUN_00443ce0, Flag_SetMagicHolder = FUN_0046d560. Re-applied whenever the render
// list is rebuilt (a cutscene's spawn rebinds every ASO).
auto hugo_eye_bind = [&](EnemyRender& er) {
    AsoVm& vm = er.avm;
    vm.set_flags(game.g_flags, 0x200);
    vm.equip_fn = [&](int it) {
        const unsigned u = unsigned(it);
        if (u - 0x36u < 6u) return inv.accessory == it;              // 0x76a628[]
        if (u - 0x42u < 0x32u) return inv.held == it;               // 0x76a62c[]
        if (u < 6u) return game.g_flags[151] == it;                 // DAT_0076bb78
        if (u - 6u < 0x18u) return game.g_flags[152] == it;         // DAT_0076bb7c
        if (u - 0x1eu < 0x18u) return game.g_flags[153] == it;      // DAT_0076bb80
        return false;
    };
    vm.magic_slot_fn = [&] {
        return inv.magic == 0x74 ? 0 : inv.magic == 0x75 ? 1 : inv.magic == 0x76 ? 2 : 3;
    };
};
// The player side the familiars read (DAT_0074c09c's fields), refreshed before each use.
auto fam_refresh = [&] {
    const Actor* pa = world.player();
    fam.player_id = pa ? pa->instance_id : 0;
    fam.origin = {feet.x, feet.y + kOriginH, feet.z};
    fam.heading = fam.facing = std::atan2(face_dir.z, face_dir.x);   // +0x308 / +0x314 [H: one facing here]
    fam.hp = float(player.hp);
    fam.level = player.level;
    fam.boosting = player.boosting;
};
// FUN_0046af60, Hugo branch [EXPORT 0046af60.c:178-409]: keep E eyes; a new eye takes the lowest free
// slot k (free eyes 1..k, laid mines above them); more than E: they all die (no explosion). Another
// character: every eye dies. Then the Fire Wheel's countdown [BYTES 0x46b7ac..0x46c107] and FUN_0046a3e0's
// charged blast [BYTES 0x46a707..0x46aafe].
auto hugo_eyes_tick = [&] {
    fam_refresh();
    Actor* pa = world.player();
    if (!pa) return;
    auto kill_eye = [&](Actor& a) {
        a.hp = 0.0f;
        a.flags12c |= 0x80000000u; a.flags130 |= 0x1008u;
        if (eye_section(a) != 5) { fxsim.kill_owned(a.instance_id, 0); fxsim.kill_owned(a.instance_id, 1); }
        a.ai.i(0x88c) = 0; a.ai.i(0x890) = 0;
    };
    std::vector<Actor*> eyes;
    for (Actor& a : world.actors())
        if (hugo::owned_eye(a, pa->instance_id) && !a.destroy_requested()) eyes.push_back(&a);
    for (Actor* a : eyes)                                                   // rebound after a render rebuild
        if (EnemyRender* er = eye_render(*a); er && er->avm.ok() && !er->avm.equip_fn) hugo_eye_bind(*er);
    if (character != 2) {
        for (Actor* a : eyes) if (a->alive()) kill_eye(*a);
    } else {
        const int E = hugo::eye_count(game.g_flags[211], game.g_flags[210], player.boosting);
        const int n = int(eyes.size());
        auto laid = [&](const Actor& a) { return hugo::b8a4(a.ai) || eye_section(a) == 5; };
        for (int r = n; r < E; ++r) {
            SpawnRequest req;
            req.cast_type = 0xb0;
            req.pos = feet;
            req.yaw_degrees = (1.5707963f - fam.heading) * 57.29577951f;
            Actor* c = world.spawn(req);
            if (!c) break;
            c->height = cast_motion(casts, 0xb0).height();
            c->radius = cast_motion(casts, 0xb0).radius();
            c->pos = {feet.x, feet.y + kOriginH - c->height, feet.z};       // the player's origin
            c->level = player.level;                                       // P+0xc8
            // [H] FUN_00476d70's stat block (FUN_004202e0 at that level) is not computed for a class-2
            // spawn here; nothing hits an eye (+0x60 & 0x80 clear), only HP >= 1 (alive) is read.
            if (c->hp < 1.0f) c->hp = c->max_hp = 1.0f;
            c->kind = 0x81u;                                                // +0x60 (not an enemy: no 0x8)
            c->ai.hittable = false;
            c->ai.i(0x854) = pa->instance_id;
            c->ai.phase() = 0;
            c->ai.alpha = 0.0f;                                             // +0x4d8
            c->atk = player_stats().atk + ((game.g_flags[217] & 0x200000) ? 1.0f : 0.0f);   // DAT_0076a73c
            c->floor_y = feet.y;
            int k = 0;                                                      // free eyes, the new one included
            for (Actor* a : eyes) if (!laid(*a)) ++k;
            ++k;
            if (k == E) {                                                   // the chain from the owner
                c->ai.set_v(0x7e4, fam.origin); c->ai.set_v(0x7f0, fam.origin); c->ai.set_v(0x7fc, fam.origin);
                c->ai.f(0x7ac) = 0.0f;
            }
            for (Actor* a : eyes)                                           // the chain of the eye in slot k
                if (a->ai.i(0x730) == k) {
                    for (int o = 0x7e4; o < 0x808; o += 4) c->ai.i(o) = a->ai.i(o);
                }
            auto at_slot = [&](int j) -> Actor* {
                for (Actor* a : eyes) if (a->ai.i(0x730) == j) return a;
                return nullptr;
            };
            for (int j = k; j <= E - 2; ++j) {                              // close the gap behind laid mines
                Actor* lo = at_slot(j);
                Actor* hi = at_slot(j + 1);
                if (lo && hi && laid(*lo) && laid(*hi))
                    for (int o = 0x7e4; o < 0x808; o += 4) lo->ai.i(o) = hi->ai.i(o);
            }
            for (int j = E - 1; j >= k; --j)                                // laid mines move up a slot
                for (Actor* a : eyes) if (laid(*a) && a->ai.i(0x730) == j) a->ai.i(0x730) = j + 1;
            c->ai.i(0x730) = k;
            eyes.push_back(c);
            const std::string stem = casts.sprite_stem(0xb0);
            if (!stem.empty() && !efx.kinds.count(stem)) efx.load_kind(g, asset_root, stem);
            if (!stem.empty() && efx.kinds.count(stem)) {
                const Cast* cc = casts.get(0xb0);
                render.push_back({c, stem, (cc && !cc->display.empty()) ? cc->display : stem});
                bind_one_enemy(render.back());
                c->ai.alpha = 0.0f;
                hugo_eye_bind(render.back());
            }
        }
        if (n > E)
            for (Actor* a : eyes) if (a->alive()) kill_eye(*a);
    }
    // The Fire Wheel: a Fix cancels it; else flag 158 counts down and its end releases the eyes' fire.
    if (csMgr.world_frozen() && game.g_flags[158] != 0) {
        for (Actor& a : world.actors()) {
            if (a.cast_id != 0xb0) continue;
            fxsim.kill_owned(a.instance_id, 0); fxsim.kill_owned(a.instance_id, 1);
            a.ai.i(0x88c) = 0; a.ai.i(0x890) = 0;
            EnemyRender* er = eye_render(a);
            if (er && er->avm.ok() && er->avm.section() != 5 && er->avm.section() != 2) er->avm.set_section(2, true);
        }
        game.g_flags[158] = game.g_flags[157] = 0;
    } else if (game.g_flags[158] > 0) {
        if (--game.g_flags[158] == 0) {
            game.g_flags[157] = 0;
            for (Actor* a : eyes) {
                fxsim.kill_owned(a->instance_id, 0); fxsim.kill_owned(a->instance_id, 1);
                a->ai.i(0x88c) = 0; a->ai.i(0x890) = 0;
            }
        }
    }
    // FUN_0046a3e0: a charged mine has gone off (P+0x7d4 == 1): the blast at the mine point. EF2 0x93
    // PC2_S_C2 (0x9c PC2_S_C3 in Boost): power 5, knock 1, rtype 2, class 6, side 0, not attached.
    // Then Se_Stop(0x9e, 10) and SE 763 p2_slashex21b, 2-D at 0x400 [BYTES 0x46aaa6..0x46aaf6].
    if (character == 2 && fam.latch_7d4 == 1) {
        fam.latch_7d4 = 2;
        ef2sim::SpawnParams sp;
        sp.power = 5.0f; sp.knock = 1.0f; sp.rtype = 2; sp.status = 0; sp.cls = 6;
        sp.atk = player_stats().atk; sp.side = 0; sp.owner = 13;
        spawn_sim_fx(player.boosting ? 0x9c : 0x93, fam.mine_point, {}, 1.0f, sp, 0, {});
        audio.stop_se(0x9e, 10);
        audio.play(763, kSfxAnimVol);
    }
};
// FUN_0046ea00's Hugo branches for 0x75 / 0x76 [BYTES 0x46f022..0x46f278, 0x46f391..0x46f45x].
auto hugo_eyes_cast = [&](int magic, bool charged) -> bool {
    fam_refresh();
    if (magic == 0x75) {                                                    // Trap Mine
        std::vector<hugo::DeployEvent> ev;
        FnAiWorld w;
        w.each = [&](const std::function<void(Actor&)>& f) {
            for (Actor& a : world.actors()) if (!a.is_player() && !a.destroy_requested() && a.alive()) f(a);
        };
        const int laid = hugo::deploy_mines(charged, fam, w, eye_section, ev);
        for (const hugo::DeployEvent& d : ev) {
            if (d.kind == hugo::DeployEvent::kEffect) {
                ef2sim::SpawnParams sp;                                     // power 0, knock 1, rtype 2, class 0xff, side 0
                sp.power = 0.0f; sp.knock = 1.0f; sp.rtype = 2; sp.status = 0; sp.cls = -1;
                sp.atk = player_stats().atk; sp.side = 0; sp.owner = 13; sp.tag = d.id == 0x94 ? 0x94 : -1;
                spawn_sim_fx(d.id, d.pos, {}, 1.0f, sp, 0, {});
            } else {
                for (Actor& a : world.actors())
                    if (a.instance_id == d.eye)
                        if (EnemyRender* er = eye_render(a); er && er->avm.ok()) er->avm.set_section(d.id);   // gated
            }
        }
        if (laid != 0) return true;
        // Nothing to lay: with every eye out (n == E) the press sets them off; else nothing happens.
        // n counts every cast-0xb0 actor with +0x8a4 or +0x8a5 set (no owner test) [BYTES 0x46f3d0].
        int out = 0;
        for (const Actor& a : world.actors())
            if (a.cast_id == 0xb0 && !a.destroy_requested() && (hugo::b8a4(a.ai) || hugo::b8a5(a.ai))) ++out;
        if (out != hugo::eye_count(game.g_flags[211], game.g_flags[210], player.boosting)) return false;
        for (Actor& a : world.actors())
            if (a.cast_id == 0xb0 && hugo::b8a4(a.ai)) a.ai.i(0x7c8) = 0;   // detonate (any owner)
        if (fam.fx_7d8) { fxsim.kill_owned(13, 0x94); fam.fx_7d8 = false; }
        return false;                                                       // no section, no MP
    }
    if (magic == 0x76) {                                                    // Fire Wheel
        game.g_flags[157] = 1;
        game.g_flags[158] = 0x96;
        for (Actor& a : world.actors()) {                                   // every eye, any owner
            if (a.cast_id != 0xb0 || a.destroy_requested()) continue;
            fxsim.kill_owned(a.instance_id, 0); fxsim.kill_owned(a.instance_id, 1);
            a.ai.i(0x88c) = 0; a.ai.i(0x890) = 0;
            EnemyRender* er = eye_render(a);
            if (er && er->avm.ok() && er->avm.section() != 5)
                er->avm.set_section(charged ? 0x111 : 0x10e, /*force*/true);   // inline FUN_00446620
        }
    }
    return true;
};
// An eye's ASO FireBullet / GenerateEffect as the PLAYER's (BULLETS.md §1.3: owner +0x60 & 8 clear -> side 0,
// class FUN_0046d650(effect); FUN_004496f0 forces rtype 3 for cast 0xb0 and keeps the handle in +0x890).
auto hugo_eye_bullet = [&](const Actor& en, bullet::Spawn& s) {
    s.owner_monster = false;
    s.cls = hugo::eye_effect_class(s.effect);
    if (std::getenv("CLERIA_DMG"))
        std::printf("  [eye] t%llu #%d fires 0x%x (class %d)\n", (unsigned long long)tick_count, en.instance_id,
                    s.effect, s.cls);
};
auto hugo_eye_effect = [&](Actor& en, ef2sim::SpawnParams& sp, int id) {
    sp.side = 0; sp.cls = hugo::eye_effect_class(id); sp.rtype = 3; sp.tag = 1;
    fxsim.kill_owned(en.instance_id, 1);
    en.ai.i(0x890) = 1;
};
// The familiar's own effects (the wheel's EF2 0x89: class 1 passed explicitly = FUN_0046d650(0x89)).
auto hugo_eye_brain_fx = [&](const Actor& en, ef2sim::SpawnParams& sp, int id) {
    sp.side = 0; sp.cls = hugo::eye_effect_class(id);
    if (std::getenv("CLERIA_DMG"))
        std::printf("  [eye] t%llu #%d effect 0x%x (class %d power %.1f)\n", (unsigned long long)tick_count,
                    en.instance_id, id, sp.cls, sp.power);
};
// The handler's dead path for an eye (FUN_004780e0, then the FUN_004472e0 ASO tick): a laid eye
// (+0x8a4 / +0x8a5) is freed at once (+0x11 |= 2) after this tick's ASO step, which is where section 5's
// FireBullet(0, 0x88) goes off; any other fades out over ~43 ticks (alpha 255 - 6n) and is then freed.
// Returns true when it handled `e` (a dead player eye).
auto hugo_eye_dead = [&](EnemyRender& e) -> bool {
    Actor* en = e.actor;
    if (!en || en->alive() || en->destroy_requested() || !hugo_eye(*en)) return false;
    if (e.avm.ok()) {
        e.avm.tick();
        if (const AsoVm::DebrisEvent* dv = e.avm.debris())              // M_900's eight pieces (ASO_NOOPS.md §3.3)
            spawn_debris(en->cast_id, {en->pos.x, en->pos.y + en->height, en->pos.z}, *dv);
        for (const AsoVm::BulletEvent& fb : e.avm.bullets()) {          // as the enemy loop's FireBullet
            const float ang = fb.f[2] * 0.0174533f + e.facing;
            const float c = float(std::cos(double(ang))), sn = float(std::sin(double(ang)));
            bullet::Spawn s;
            s.kind = fb.kind; s.effect = fb.id;
            s.owner = en->instance_id; s.owner_atk = float(en->atk);
            hugo_eye_bullet(*en, s);
            s.pos = {en->pos.x + c * fb.f[0], en->pos.y + en->height + fb.f[1], en->pos.z + sn * fb.f[0]};
            Vec3 d{fb.f[3], fb.f[4], fb.f[5]};
            if (d.x * d.x + d.y * d.y + d.z * d.z == 0.0f) d = {c, 0.0f, sn};
            const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            s.dir = dl == 0.0f ? Vec3{} : Vec3{d.x / dl, d.y / dl, d.z / dl};
            s.speed = fb.f[6]; s.accel = fb.f[7]; s.gravity = fb.f[8];
            s.power = fb.f[9]; s.knock = fb.f[10];
            std::memcpy(&s.f158, &fb.f[11], 4);
            std::memcpy(&s.f15c, &fb.f[12], 4);
            std::memcpy(&s.f160, &fb.f[13], 4);
            s.f164 = fb.f[14] * 0.0174533f;
            s.f168 = fb.f[15]; s.f16c = fb.f[16]; s.f170 = fb.f[17]; s.f174 = fb.f[18];
            bullets.spawn(s);
        }
        for (const AsoVm::SoundEvent& se : e.avm.sounds()) sfx_at(se.id, en->pos, kSfxAnimVol);
    }
    if (hugo::b8a4(en->ai) || hugo::b8a5(en->ai) || en->ai.alpha <= 0.0f) {
        world.request_destroy(*en);
        en->visible = false;
    } else {
        en->ai.alpha = std::max(0.0f, en->ai.alpha - 6.0f / 255.0f);
    }
    return true;
};
// FUN_0046d6e0 [BYTES 0x46d6e0..]: on each of Hugo's attacks (not the 0x112 tap step) every free eye is
// asked for section 7 (FUN_00446550: priority-gated). [not modelled] the Extra skin's direct 0x82 shot.
auto hugo_eyes_fire = [&] {
    const Actor* pa = world.player();
    if (character != 2 || !pa) return;
    for (Actor& a : world.actors()) {
        if (!hugo::owned_eye(a, pa->instance_id) || hugo::b8a4(a.ai) || !a.alive()) continue;
        EnemyRender* er = eye_render(a);
        if (er && er->avm.ok() && er->avm.section() != 5) {
            const bool took = er->avm.set_section(7);
            if (std::getenv("CLERIA_DMG"))
                std::printf("  [eye] t%llu #%d section 7 %s\n", (unsigned long long)tick_count, a.instance_id,
                            took ? "on" : "refused");
        }
    }
};
