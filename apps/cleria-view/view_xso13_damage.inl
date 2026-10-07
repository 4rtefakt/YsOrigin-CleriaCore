// 0x15C Chr_Damage on the player (build/event_re/FLOOR13_OPS.md), included in the player hit
// pass right after hurt_player. FUN_00571ae0 hits only when no fix is held (checked when the op
// runs, CutsceneHost::chr_damage), the victim's +0x60 & 0x80 is set (0x14d), its HP >= 1.0, its
// Untouchable byte +0x9d7 is clear (hurt_player's p_untouchable) and the player's i-frames +0x734
// are 0. The GoScript is the attacker: +0xa4 = the op's atk, so FUN_004fd350 -> FUN_004fa740
// prices it like any hit on the player; the angle is the player's heading +0x314 + pi
// (0x6bd6ac), i.e. a push backwards. [H] the attacker's per-move power +0xfc and reaction type
// +0x108 are taken as 0: FUN_00571ae0 never writes them on the GoScript.
for (const auto& d : csHost.player_damage) {
    if (player.hp < 1 || player_iframes > 0) continue;
    hurt_player(d.atk, 0.0f, 0, std::atan2(face_dir.z, face_dir.x) + 3.14159265f);
    std::printf("  [xso] Chr_Damage player atk %.0f -> HP %d\n", double(d.atk), player.hp);
}
csHost.player_damage.clear();
