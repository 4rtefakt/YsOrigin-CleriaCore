// SOB trigger 4, the TOUCH volumes (the Touch_kekkai wards' 0x15C Chr_Damage(998, 50); trigger 6 =
// 2|4 is @Touch_Toge), included after the step-plate loop (build/event_re/FLOOR13_OPS.md).
// The engine: the SOB loader turns trigger 2 into +0x28c |= 4 and trigger 4 into |= 8 [BYTES
// 0x502611..0x502646]; the integrator FUN_0045a510 calls FUN_0045a4a0 for a FLOOR contact with a
// mesh whose +0x28c & 4 (the plate loop above) or a SIDE contact with +0x28c & 8, and that starts
// the mesh's script when no fix is held, +0x298 & 0x880 is clear and the toucher is the player
// [BYTES 0x45a4a0..0x45a4f8] (BARRIER_OBJECTS.md §1).
// [STAND-IN] the side contact: CleriaCore's wall push (resolve_walls) is not the engine's sweep and
// keeps no contact record, so a side contact is the player's sphere (pm.radius(), the radius the wall
// push keeps her from walls, + 0.05 at the origin height) overlapping the object's own collision triangles.
if (events_enabled && !csMgr.world_frozen() && player.hp >= 1)
    for (const SceneTrigger& t : room.mesh.triggers) {
        if (!(t.trigger & 4) || t.script.empty()) continue;
        std::string ln = t.name;
        for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
        const RoomMesh::ObjRange* r = phys_mesh(room).object_range(ln);
        if (!r || r->t1 <= r->t0) continue;
        const MeshObj* mo = meshes.find(ln);
        if (mo && !mo->event_on) continue;                       // Mesh_EnableEvent(off)
        if (!sphere_overlaps_tris(phys_mesh(room), {feet.x, feet.y + kOriginH, feet.z}, pm.radius() + 0.05f,
                                  r->t0, r->t1))
            continue;
        std::string stem = t.script_stem;
        for (char& ch : stem) ch = char(std::tolower((unsigned char)ch));
        XsoVm::Vars ov;
        ov.s[0] = t.name; ov.i[0] = t.i0; ov.i[1] = t.i1; ov.f[0] = t.f0;
        if (csMgr.start(stem, ov, /*owner: the player*/ 13))
            std::printf("  [touch] %s -> %s\n", t.name.c_str(), stem.c_str());
    }
