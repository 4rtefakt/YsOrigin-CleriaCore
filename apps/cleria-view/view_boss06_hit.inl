// Zava's heads at the player's swing: the accept-hit callback FUN_0053b930 (ZAVA_D.md §7.1; obj+0x480). The heads
// have no parts: the mode-0 scan's rays (`mesh_segs`, view_player_meshes.inl) are tested
// against the head's collision hull, and the callback always refuses the hit — no damage, no boost — after its side
// effects: the stun request (a charged skill, section 0x10f), the DAMAGE flinch out of WAIT, the "blocked" splash
// 344 at the hit point while the shield flag is up (one per 200 ms). Included by view_player_meshes.inl.
// [approx] the hull as one hit-testable mesh (the entry's +0x8a is the head's `hittable`, cleared by its death).
if (boss6) {
    const RoomMesh& b6m = phys_mesh(room);
    for (int i = 0; i < boss06::kHeads && atk_window > 0; ++i) {
        const Boss06Head& hb = boss6->head(i);
        if (hb.dead || hb.hits_off) continue;              // H+0x11 & 2; H+0x298 & 0x40
        const MeshObj* mo = meshes.find(boss6_name[i]);
        const RoomMesh::ObjRange* rr = b6m.object_range(boss6_name[i]);
        if (!mo || !rr || mo->no_collide) continue;
        Vec3 hit{};
        if (!mesh_seg_hit(b6m, rr->t0, rr->t1, &hit)) continue;
        // The throttle's clock is real time; a (lockstep) drive run reads the tick clock (as boss02's).
        const std::uint32_t now_ms = lockstep ? std::uint32_t(tick_count * 1000 / 60) : std::uint32_t(SDL_GetTicks());
        boss6_in.player_section = avm.section();           // P+0x994 as the callback reads it
        Boss06Output fb;
        boss6->accept_hit(i, hb.hittable, hit, now_ms, boss6_in, fb);   // returns 0: the hit is refused
        boss6_apply(fb);
        if (std::getenv("CLERIA_DMG"))
            std::printf("  hit zava head %c: refused (stun request %d, shield %d)\n", 'a' + i, int(hb.stun_req), int(hb.shield));
    }
}
