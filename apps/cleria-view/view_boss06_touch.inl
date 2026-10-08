// Zava's heads against the player in the hit phase (included after view_boss01_touch.inl):
//  - the touch callback FUN_0053b780 (ZAVA_D.md §7.2; obj+0x484): a head the player's mover touches (side or floor
//    contact) SETS his knock channel 1 to normalize(P - head) x the head's speed, unless the contact is collision
//    entry 1 [U-D4]. No damage (no head entry has +0x8b, D5).
//    [approx] as Velagunder's touch: the mover's contact record is not kept — a side contact is the player's
//    collision sphere (origin, r 0.675 + 0.05) overlapping the entry's triangles, a floor contact the feet probe;
//    the y of the push is dropped (the view's channel has none). [not modelled] actors other than the player.
//  - the direct damage calls the two modules asked for this frame, FUN_004fd350(attacker, P, &zero16) with the
//    attacker's STR overridden: the bite (237) and the burn (226) with byte P+0x9d8 = 1 round the call (no reaction:
//    the player's Durable for the call), Zava's blast (267) without it. No FUN_004fe9e0 gate: the i-frame timer,
//    P+0x60 & 0x80 and the re-hit bank are not consulted (hurt_direct); bonus / rtype 0 and angle 0 (the zeroed
//    result block) [H: H+0xfc / Z+0xfc / +0x108 have no writer in the band].
if (boss6 && player.hp >= 1) {
    const RoomMesh& cm = phys_mesh(room);
    const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
    for (int i = 0; i < boss06::kHeads; ++i) {
        const Boss06Head& hb = boss6->head(i);
        const RoomMesh::ObjRange* br = cm.object_range(boss6_name[i]);
        const MeshObj* mo = meshes.find(boss6_name[i]);
        if (!br || !mo || mo->no_collide || hb.dead) continue;
        if (!boss6_entries_built[std::size_t(i)]) boss6_build_entries(i);
        const auto& ents = boss6_entries[std::size_t(i)];
        for (std::size_t k = 0; k < ents.size(); ++k) {
            const std::uint32_t a0 = br->t0 + ents[k].first, a1 = br->t0 + ents[k].second;
            const bool side = sphere_overlaps_tris(cm, O, 0.675f + 0.05f, a0, a1);
            const bool floor = grounded && segment_hits_tris(cm, O, {feet.x, feet.y - 0.2f, feet.z}, a0, a1);
            if (!side && !floor) continue;
            if (const std::optional<Vec3> v = boss6->touch(i, int(k), O)) { player_kb.x = v->x; player_kb.z = v->z; }
        }
    }
}
for (const Boss6Damage& bd : boss6_dmg) {
    if (player.hp < 1) break;
    const bool d0 = avm.durable();
    const int hp0 = player.hp;
    if (bd.no_reaction) avm.set_durable(true);             // byte P+0x9d8 = 1 round the call
    hurt_direct = true;
    hurt_player(bd.atk, 0.0f, 0, 0.0f);
    hurt_direct = false;
    if (bd.no_reaction) avm.set_durable(d0);
    if (boss6_log || std::getenv("CLERIA_DMG"))
        std::printf("  [boss06] t%llu direct damage ATK %.0f (%s) player hp %d -> %d\n", (unsigned long long)tick_count, bd.atk,
                    bd.head < 0 ? "Zava's blast" : bd.no_reaction ? "a head, no reaction" : "a head", hp0, player.hp);
}
boss6_dmg.clear();
