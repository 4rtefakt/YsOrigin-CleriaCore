// The GoXx type-1 objects (included inside the game loop, twice): the world pickups and the debris step, bounce
// and are collected (FUN_00584f90 / FUN_005856c0 / FUN_00585ff0), the chest item rises (FUN_00586910). The live
// tick includes it, and so does the frozen branch (FIX_SLOTS.md S6): the GoXx pool is gated on the menu byte
// 0x74ab8f and the logic-step byte only [EXPORT 0058fcb0.c:83-163; 00584f90.c:78, 209, 306], and none of those
// three functions nor their direct callees reads a fix counter — but FUN_00586910, where a chest item with
// +0x13d & 0x20 starts to close only once no Fix holds. So a drop falls, bounces, expires and is collected
// under either slot. [H] the callees' callees were not read for a fix test.
            // World pickups: motion, bounce, collect (FUN_00584f90 / FUN_005856c0 / FUN_00585ff0).
            prof("tick:pickups+fx");
            if (!pickups.empty()) {
                PickupWorld pw;
                pw.live = int(pickups.size());
                pw.window_open = csMgr.fix_any();          // the chest item's gate: 0x75ad90 + 0x75ad94 [EXPORT 00586910.c:50-52]
                pw.hit = [&](const Vec3& a, const Vec3& b, Vec3* pt, Vec3* n) {
                    return segment_hit(phys_mesh(room), a, b, pt, n);
                };
                pw.player = {feet.x, feet.y + kOriginH, feet.z};       // P+0x50, the origin (0x58606d..0x586080)
                // The player's first sword trail (pool 4's first object with +0x120 == the player [H: `loci` keeps
                // the pool's order]): its tip sphere, as placed by its last armed tick (a trail not yet placed has
                // none: radius 0).
                for (const PlayerLocus& pl : loci)
                    if (pl.sword.alive()) {
                        pw.sword = pl.sword.spheres()[0].radius > 0.0f;
                        pw.sword_tip = pl.sword.spheres()[0].centre;
                        break;
                    }
                pw.sparkle = pickup_sparkle;              // view_weather.inl
                pw.stain = debris_stain;                  // view_weather.inl
                pw.touch = [&](Pickup& pk) {
                    if (fly || player.hp <= 0) return false;
                    float hp = float(player.hp);
                    EffectEnv env;
                    env.flags = game.g_flags; env.hp = &hp; env.max_hp = float(player.max_hp);
                    env.player_alive = player.hp > 0; env.boost = &player.boost; env.oxygen = &oxy.o2;
                    env.sp_rate = settings().s.access.sp_rate;    // SPrate 0x76a5f8 (the shell's Cheats)
                    if (game.g_flags[144] == 2) env.gold_scale = 0.01f;   // DIFFICULTY.md G13 (0x6bd084)
                    // mods (LUA_API.md §5.2): what the pickup applies, for pickup_collected; the pickup's own value
                    // (a drop filter's, spawn_pickup's), then the "pickup" filter, then for SP the "sp" filter.
                    PickupFx got{pk.id, 7, 1.0f, 0};
                    bool got_none = false;
                    env.mod = [&](PickupFx& fx) {
                        if (pk.mod_value >= 0.0f) fx.value = pk.mod_value;
                        if (lua_combat_on(script::CombatQuery::Kind::Pickup)) {
                            static const char* const kWhat[] = {"sp", "hp", "atk_buff", "def_buff", "mp_buff", "boost", "oxygen", "item"};
                            script::CombatQuery q;
                            q.kind = script::CombatQuery::Kind::Pickup;
                            q.item = fx.id; q.value = fx.value; q.timer = fx.timer;
                            q.num = {{"x", double(pk.pos.x)}, {"y", double(pk.pos.y)}, {"z", double(pk.pos.z)}};
                            q.str = {{"what", kWhat[fx.what]}, {"name", g_content.items.name(fx.id)}};
                            const script::CombatDecision d = lua_combat(q);
                            if (d.action == script::CombatDecision::Action::Suppress) got_none = true;
                            else if (d.action == script::CombatDecision::Action::Replace) {
                                fx.value = float(std::clamp(d.value, 0.0, 999999.0));
                                fx.timer = d.timer;
                            }
                        }
                        if (fx.what == 0 && !got_none) {
                            fx.value = float(lua_sp(int(fx.value), int(fx.value), "pickup"));
                            got_none = fx.value <= 0.0f;
                        }
                        if (got_none) fx.value = 0.0f;
                        got = fx;
                        return !got_none;
                    };
                    inv.sp = player.sp;
                    int pop = 0;
                    if (pk.id >= content::kNewItemFirst) { if (!content_touch_new(pk)) return false; }   // mods
                    else if (pickup(inv, inv_items, env, pk.id, &pop) != PickupResult::Taken) return false;
                    content_taken(pk);                                // mods: collected flag, ItemObtained
                    {   // mods: pickup_collected
                        static const char* const kWhat[] = {"sp", "hp", "atk_buff", "def_buff", "mp_buff", "boost", "oxygen", "item"};
                        events::Event pe;
                        pe.kind = events::Kind::PickupCollected;
                        pe.item = pk.id; pe.source = kWhat[got.what]; pe.amount = got.value; pe.count = got.timer;
                        pe.old_value = got_none ? 1 : 0;
                        pe.pos[0] = pk.pos.x; pe.pos[1] = pk.pos.y; pe.pos[2] = pk.pos.z;
                        content_emit(pe);
                    }
                    player.sp = inv.sp;
                    player.hp = int(hp);
                    // The GoPopup (PICKUPS.md §7.3, §13.6); a full pool skips it, the grant stands.
                    auto pop_at = [&](const std::string& t, std::uint32_t col, float sc, const Vec3& at) {
                        if (popups.size() < 32) popups.push_back(make_popup(t, col, sc, {at.x, at.y + 0.5f, at.z}));
                    };
                    const int id = pk.id;
                    // The player-anchored ones stand at P+0x50 + (0, 0.5, 0), the ORIGIN [BYTES 0x5866a3..0x5866d1,
                    // 0x443a20..0x443a52], not the feet.
                    const Vec3 porg{feet.x, feet.y + kOriginH, feet.z};
                    if (id >= 0x78 && id <= 0x7f) pop_at(std::to_string(pop), 0xcdacd0, 0.75f, pk.pos);
                    else if (id >= 0x42 && id <= 0x47) { if (pop > 0) pop_at(std::to_string(pop), 0x00ff00, 1.0f, porg); }
                    else if (id == 0x72) pop_at("BOOST MAX", 0x00ff00, 0.65f, porg);
                    else if (id == 0x73) pop_at("OXYGEN MAX", 0x00ff00, 0.65f, porg);
                    else if (id == 0x48 || id == 0x49) pop_at("str +", 0xff6060, 1.0f, porg);
                    else if (id == 0x4a || id == 0x4b) pop_at("def +", 0x6060ff, 1.0f, porg);
                    else if (id == 0x4c || id == 0x4d) pop_at("mp +", 0xffff60, 1.0f, porg);
                    // common\cure.eft attached to the player at (0, -1, 0) [0x7de1f4 / 0x7d9c34], mode 2
                    // (FUN_00585ff0 for 0x48..0x4d, FUN_00443390 0x4434a3 for the heals and seeds);
                    // its depth bias (5.0) draws it in front of her.
                    if ((pk.id >= 0x42 && pk.id <= 0x4d) || pk.id == 0x72 || pk.id == 0x73)
                        attach_to_player(spawn_sim_fx(63, {}, {}, 1.0f, {}, 0, {}), {0.0f, -1.0f, 0.0f}, 0.0f);
                    spawn_sim_fx(72, pk.pos, {}, 1.0f, {}, 0, {});      // common\z_item.eft
                    sfx_at(pickup_collect_se(pk.id), pk.pos, kSfxAnimVol);   // p_takeitem; s_growup0..2 for the buff drops
                    return true;
                };
                for (Pickup& pk : pickups) step_pickup(pick_rng, pk, pw);
                pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                             [](const Pickup& pk) { return pk.dead; }), pickups.end());
            }
