// CLERIA_LOS=1 (audit aid, issue #59): report an engaged actor that drops to phase 1 this tick, with the
// segment FUN_00475c00's keep tests (its origin +0x50 -> the target's origin +0x50, selector 0) and the
// collision triangles that block it. Included in the tick body next to view_revive.inl.
const bool los_log = std::getenv("CLERIA_LOS") != nullptr;
auto los_report = [&](const Actor& en, const AiEnv& env) {
    const RoomMesh& M = phys_mesh(room);
    std::printf("[los] t%llu #%d cast %d 2->1 self (%.2f,%.2f,%.2f) tgt (%.2f,%.2f,%.2f) dist %.2f blocked %d\n",
                (unsigned long long)tick_count, en.instance_id, en.cast_id, env.self.x, env.self.y, env.self.z,
                env.target.x, env.target.y, env.target.z, en.ai.dist(), int(segment_blocked(M, env.self, env.target)));
    for (std::uint32_t t = 0; t * 3 + 2 < M.indices.size(); ++t)
        if (segment_hits_tris(M, env.self, env.target, t, t + 1)) {
            std::printf("[los]   tri %u '%s'", t, std::string(surface_of(M, int(t))).c_str());
            for (int k = 0; k < 3; ++k) {
                const RoomVertex& v = M.vertices[M.indices[t * 3 + k]];
                std::printf(" (%.2f,%.2f,%.2f)", v.px, v.py, v.pz);
            }
            std::printf("\n");
        }
};
