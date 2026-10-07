// Velagunder's mesh contact (VELAGUNDER.md §3.4, U4 resolved 2026-10-07): the body's collision
// entries (obj+0x25c, one per mesh of the collision twin BOSS01_.YMO) hurt the player who touches
// one while its +0x8b is lit for the current move. Included in view_game.inl's hit phase, after
// the parts' armed volumes.
//
// The entry flags (+0x84) [EXPORT 005601e0.c:173-215]: the twin's model flags have 0x20, so each
// entry is bound by its mesh node's PARENT name (runtime node +0x184) to the render model's node of
// that name (FUN_005f4e10), and +0x84 = that node's +0x50 = the YMO node record's +0x290 (FUN_005f3c70).
// BOSS01.YMO: Bone(22/23/25/26/28/29/31/32) 0x45, Bone(18/19/20/36) 0x05, Bone(48/49/51/52/54/55/57/58)
// 0x23, Bone(43..46) 0x03, Bone(16/17/41/42/9) 0x01, Bone(81) 0 (Boss01Contact::entry_hurts reads them).
// The touch [EXPORT 0045a510.c:1031-1042 -> 0045a4a0 -> obj+0x484 = FUN_00505640]: the player's mover
// reports the side-contact mesh (obj+0x28c & 4) and the floor mesh (& 8); the boss sets both (0xc).
// FUN_00505640: the player hittable (+0x60 & 0x80, +0x9d7 clear, +0x734 == 0), the entry's +0x8b,
// HP >= 1, the re-hit bank free for the boss record (token obj+0xf0 = obj+0x14, re-hit +0xf4 = 15),
// then the knock angle 3 pi / 2 - blk+0xd4 while the fight flag is on, and FUN_004fe1a0 when
// obj+0x11c & 8 else FUN_004fd350, with the boss's record: power +0xfc = 18.0, STR, rtype 0 [H: +0x108
// is not written by the ctor].
// [approx] the mover's contact record is not kept: a side contact is the player's collision sphere
// (origin, r 0.675 + 0.05) overlapping the entry's triangles, a floor contact the feet probe
// (origin down to feet - 0.2) hitting them. [U3] FUN_004fe1a0 is run as FUN_004fd350.
if (boss && !boss->destroyed() && player.hp >= 1 && player_iframes == 0 && !p_untouchable) {
    const RoomMesh& cm = phys_mesh(room);
    const RoomMesh::ObjRange* br = cm.object_range("boss01");
    if (br && !boss_entries_built) {                       // once: each twin mesh's triangle range
        boss_entries_built = true;
        boss_entries.clear();
        const Model* rm = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss01\\boss01.ymo"));
        const Model* tw = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss01\\boss01_.ymo"));
        if (rm && tw) {
            std::unordered_map<std::string, std::uint32_t> rflags;
            for (const Node& n : rm->nodes) rflags[n.name] = n.flags;
            std::uint32_t t = 0, v = 0;
            for (const Node& n : tw->nodes) {                  // the bake's order (scene.cpp build_room_objects)
                if (n.mesh_index < 0) continue;
                const Mesh& me = tw->meshes[std::size_t(n.mesh_index)];
                if (me.vbs.empty() || me.ibs.empty()) continue;
                const std::uint32_t nv = me.two_stream() ? std::min(me.vbs[1].count, me.vbs[0].count) : me.vbs[0].count;
                std::uint32_t nt = 0;
                const std::size_t ni = me.ibs[0].idx.size();
                if (!me.submeshes.empty()) {
                    for (const Submesh& sm : me.submeshes)
                        for (std::uint32_t j = 0; j < sm.tri_count * 3; ++j) nt += std::size_t(sm.first_tri) * 3 + j < ni ? 1 : 0;
                } else {
                    nt = std::uint32_t(ni);
                }
                nt /= 3;
                const bool tw20 = (tw->flags & 0x20u) != 0;      // 0x21 for BOSS01_.YMO
                auto it = rflags.find(tw20 ? n.parent : n.name);
                boss_entries.push_back({t, t + nt, it != rflags.end() ? it->second : 0u});
                t += nt; v += nv;
            }
            if (t != br->t1 - br->t0 || v != br->v1 - br->v0) {
                std::printf("  [boss] mesh contact off: the twin's ranges (%u tris, %u verts) do not match the baked object (%u, %u)\n",
                            t, v, br->t1 - br->t0, br->v1 - br->v0);
                boss_entries.clear();
            } else if (std::getenv("CLERIA_BOSSLOG")) {
                std::printf("  [boss] %zu body entries (%u tris), flags:", boss_entries.size(), t);
                for (const BossEntry& e : boss_entries) std::printf(" %x", e.flags);
                std::printf("\n");
            }
        }
    }
    // drive bossentry=k: put the player's origin on entry k's first triangle (its centroid, pushed
    // 0.6 out along its normal) so the touch can be exercised headless.
    if (drive_boss_entry >= 0 && br && std::size_t(drive_boss_entry) < boss_entries.size()) {
        const BossEntry& e = boss_entries[std::size_t(drive_boss_entry)];
        const std::uint32_t tri = br->t0 + e.t0;
        if (std::size_t(tri) * 3 + 2 < cm.indices.size()) {
            const RoomVertex& A = cm.vertices[cm.indices[std::size_t(tri) * 3]];
            const RoomVertex& Bq = cm.vertices[cm.indices[std::size_t(tri) * 3 + 1]];
            const RoomVertex& C = cm.vertices[cm.indices[std::size_t(tri) * 3 + 2]];
            const Vec3 c{(A.px + Bq.px + C.px) / 3.0f, (A.py + Bq.py + C.py) / 3.0f, (A.pz + Bq.pz + C.pz) / 3.0f};
            const Vec3 u{Bq.px - A.px, Bq.py - A.py, Bq.pz - A.pz}, w{C.px - A.px, C.py - A.py, C.pz - A.pz};
            Vec3 n{u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x};
            const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (nl > 1e-6f) n = {n.x / nl, n.y / nl, n.z / nl};
            feet = {c.x + n.x * 0.6f, c.y + n.y * 0.6f - kOriginH, c.z + n.z * 0.6f};
            std::printf("  [drive] bossentry %d flags 0x%x -> origin (%.2f,%.2f,%.2f)\n", drive_boss_entry, e.flags,
                        feet.x, feet.y + kOriginH, feet.z);
        }
        drive_boss_entry = -1;
    }
    const Boss01Contact& ct = boss->contact();
    const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
    for (std::size_t k = 0; br && k < boss_entries.size(); ++k) {
        const BossEntry& e = boss_entries[k];
        if (!ct.entry_hurts(e.flags)) continue;            // entry +0x8b
        const std::uint32_t a0 = br->t0 + e.t0, a1 = br->t0 + e.t1;
        const bool side = sphere_overlaps_tris(cm, O, 0.675f + 0.05f, a0, a1);
        const bool floor = grounded && segment_hits_tris(cm, O, {feet.x, feet.y - 0.2f, feet.z}, a0, a1);
        if (!side && !floor) continue;
        if (player_bank.check_and_record(0xB0550001u, 15)) break;   // obj+0xd0 record, re-hit 15
        const Vec3 bp = boss->pos();
        const float ang = game.g_flags[156] != 0 ? 4.71238899f - boss->yaw()   // 0x6bd858
                                                 : std::atan2(O.z - bp.z, O.x - bp.x);
        const int hp0 = player.hp;
        hurt_player(boss->str(), 18.0f, 0, ang);
        if (std::getenv("CLERIA_DMG"))
            std::printf("  [hurt] t%llu velagunder body entry %zu flags 0x%x (%s, STR %.0f power 18) player hp %d -> %d\n",
                        (unsigned long long)tick_count, k, e.flags, side ? "side" : "floor", boss->str(), hp0, player.hp);
        break;
    }
}
