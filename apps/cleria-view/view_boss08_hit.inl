// Darm's nine GoXx parts at the player's swing (DARM_D.md §4.3; boss8_hit in view_boss08.inl has the rules): the
// swing's serial against each part's own re-hit bank, the armed hitbox's power and its damage class, then the boost
// gain of a hit that was not refused (FUN_0046cc10(300.0), 0x4fb0d7). A player particle takes the same path with
// token 0 (view_fx_mode0.inl). Included at the swing after view_boss07_hit.inl.
if (boss8 && atk_window > 0) {
    bool one = false;
    if (boss8_hit(vol, player_hit_serial, atk_hb.rehit, atk_hb.power, atk_cls, /*swing*/true, &one)) {
        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
    }
    if (one && atk_hb.one_shot) atk_window = 0;
}
