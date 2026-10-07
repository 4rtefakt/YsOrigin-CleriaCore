// M980's bullet scan (build/ai_spec/M980.md §3.4a, the brain's SCAN and the handler's dead path
// 0x4d6eed), run by the host just before the bullet pass. For the crystals' tornadoes (EF2 502) and
// lightning balls (503): the root particle is force-killed (FUN_0044ebb0 force 1: the kill flag, the
// impact — the ball's 231 explosion) when it has fewer than 5 ticks left, and [approx] at once when
// its crystal is gone (the engine waits for the 43-tick death fade). Their orbs (310) sink 0.3 a tick
// to y 0.31. [not modelled] the tornado's B2 +0x16 write (a no-op on the shipped data).
for (auto& bp : bullets.list()) {
    bullet::Bullet& cb = *bp;
    if (cb.gone || cb.destroyed || !cb.fx) continue;
    const int fxid = cb.s.effect;
    if (fxid != 502 && fxid != 503 && fxid != 310) continue;
    const Actor* cow = world.find(cb.s.owner);
    const bool crystal = cow && cow->cast_id == 0xb8;
    if (fxid == 310) {
        if (crystal && cb.s.pos.y > 0.31f) cb.s.pos.y = std::max(cb.s.pos.y - 0.3f, 0.31f);
        continue;
    }
    if (cb.fx->kill) continue;
    ef2sim::Particle* rp = fxsim.first_particle(cb.fx);
    if (rp && (rp->remaining < 5 || !crystal || cow->hp < 1.0f)) fxsim.strike(*rp);
}
