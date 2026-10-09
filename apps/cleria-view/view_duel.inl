// The story duels' script side (build/event_re/DUELS.md), included inside the game loop's setup
// next to view_xso13.inl.
//
// XSO op 0x111 FireBullet (handler 0x56aaa5 -> FUN_00571500): the actor is resolved (13 = the
// player, else the registry; FUN_00434d50) and, when it exists, FUN_00593550(ecx = actor,
// edx = kind, effect, &pos, &dir, block, NULL) runs with the script's block verbatim: the
// position and direction are not normalised and not offset by the actor (unlike the ASO's
// anime_FireBullet, BULLETS.md §1.2). The owner fields are the ones every GoXx reads off its
// owner at spawn (§1.3): side by +0x60 & 8 (a monster-class node: side 1, the player the only
// victim), the stat block +0xa4 for the effect's attack.
// S_3080's FireBulletEvent / FireBulletEvent2 (Hugo's and Toal's post-duel scenes) fire six
// kind-2 shots of EF2 308 (enemy\m_840_tama01.eft) from an invisible Shion (200) or Hugo (197).
csHost.fire_bullet_fn = [&](const FireBulletOp& fb) {
    const Actor* ow = fb.actor == 13 ? world.player() : world.find(fb.actor);
    if (!ow) {
        std::printf("  [xso] FireBullet: actor %d absent (no bullet)\n", fb.actor);
        return;
    }
    bullet::Spawn s = bullet::block_spawn(fb.kind, fb.effect, {fb.pos[0], fb.pos[1], fb.pos[2]},
                                          {fb.dir[0], fb.dir[1], fb.dir[2]}, fb.f);
    s.owner = ow->instance_id;
    s.owner_monster = !ow->is_player();
    s.owner_atk = ow->is_player() ? float(player_stats().atk) : float(ow->atk);
    const bullet::Bullet* b = bullets.spawn(s);
    std::printf("  [xso] FireBullet actor %d kind %d effect %d pos (%.2f, %.2f, %.2f) dir (%.2f, %.2f, %.2f) speed %.2f%s\n",
                fb.actor, fb.kind, fb.effect, fb.pos[0], fb.pos[1], fb.pos[2], fb.dir[0], fb.dir[1], fb.dir[2],
                fb.f[0], b ? "" : " (no effect)");
};

// The GoEffect pool for the brains' effect scans (AiWorld::for_each_effect; M820 0x4dfbc3, M860
// 0x4e33d8): every live effect (+0x11 & 2 clear = not yet destroyed here) in spawn order [H: the
// pool's own order is its slot order; only M860's 0x40 cap and its collected lists could tell].
auto ai_effects = [&](const std::function<void(const AiWorld::Fx&)>& f) {
    for (const auto& fx : fxsim.effects()) {
        if (fx->dead) continue;
        AiWorld::Fx e;
        e.id = fx->id; e.attach = fx->attach_mode; e.kill = fx->kill; e.owner = fx->owner; e.tag = fx->tag;
        f(e);
    }
};

// M860's pillar break (AiWorld::break_shields; KISHGAL_SHIELD.md §3.2): the overlap test over the particle pool
// (ef2sim::System::break_overlapping), then per broken pillar FUN_0043a040(se, 0x400) — a 2-D sound — and the
// impact effect at the pillar particle with rot 0, scale 1, power 0, knock 1 and the brain's stat block, its
// byte +0x1d5 = 0xff (0x4e37bc..0x4e3872).
auto ai_break_shields = [&](Actor& self, int shield, const int* ids, int n, int impact, int se) {
    const std::vector<Vec3> broke = fxsim.break_overlapping(shield, ids, n);
    for (const Vec3& at : broke) {
        audio.play(se, kSfxAnimVol);
        ef2sim::SpawnParams sp;
        sp.power = 0.0f; sp.knock = 1.0f; sp.atk = self.atk; sp.side = 1; sp.owner = self.instance_id;
        if (ef2sim::Effect* fx = spawn_sim_fx(impact, at, {}, 1.0f, sp, 0, {})) fx->attach_mode = 0xff;
        std::printf("  [m860] t%llu pillar broken at (%.2f, %.2f, %.2f)\n", (unsigned long long)tick_count, at.x, at.y, at.z);
    }
    return int(broke.size());
};

// op 0x95 SetTimeScale (view_xso13.inl: 0x6d6078) logged: S_3080's Battle_Win / EV_Feena01 slow the
// victory cutscene to 0.4 around Toal's dashes (the drive regressions read it).
if (auto prev = csHost.time_scale_fn) {
    csHost.time_scale_fn = [prev](float s) {
        std::printf("  [xso] SetTimeScale %.2f\n", s);
        prev(s);
    };
}

// The GoXx bullets under a Fix. FUN_0058fcb0 type 0xb -> FUN_00594230 gates only on the menu byte
// 0x74ab8f and the live-step byte 0x74c0f0 [BYTES 0x594262; EXPORT 00594230 L188 / L199], never on
// the fix counters 0x75ad90/94: a scripted FireBullet flies through its cutscene. The frozen tick
// runs the same update as the live one, minus the player's swing (no attack under a Fix) [H: the
// live path's Env otherwise]; the particle hits stay off under a Fix as before (EFFECTHIT.md, [H]).
auto step_bullets_frozen = [&] {
    if (bullets.list().empty() && bullets.probes().empty()) return;
    bullet::Env benv;
    benv.player = {feet.x, feet.y + kOriginH, feet.z};
    benv.player_hp = float(player.hp);
    benv.ray = [&](const Vec3& o, const Vec3& dl, int set) {
        bullet::RayHit r;
        if (set != 2) return r;                          // [not modelled] water (set 1)
        r.hit = segment_hit(phys_mesh(room), o, {o.x + dl.x, o.y + dl.y, o.z + dl.z}, &r.point, &r.normal);
        return r;
    };
    benv.actor = [&](int id, Vec3* p, float* hp) {
        const Actor* a = world.find(id);
        if (!a) return false;
        *p = {a->pos.x, a->pos.y + a->height, a->pos.z};
        *hp = a->hp;
        return true;
    };
    benv.character = character;
    benv.difficulty = difficulty >= 2 ? difficulty - 2 : 0;
    benv.rng = [&] { return pick_rng.next(); };
    std::vector<bullet::Event> bev;
    bullets.update(benv, &bev);
    for (const bullet::Event& ev : bev) {
        if (ev.type == bullet::Event::kSound) { sfx_at(ev.id, ev.pos, kSfxAnimVol); continue; }
        if (ev.type == bullet::Event::kPush) { player_kb2.x += ev.push.x; player_kb2.z += ev.push.z; continue; }
        if (ev.target == 0) player.hp = std::min(player.max_hp, player.hp + int(ev.amount));
        else if (Actor* a = world.find(ev.target)) a->hp = std::min(a->max_hp, a->hp + ev.amount);
    }
};
