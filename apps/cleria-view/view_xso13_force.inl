// The script pushes 0x15E ChrAllInEventBox_SetForce / 0x15F ChrAll_SetPointForce on the player
// (build/event_re/FLOOR13_OPS.md), included in the player step before the wall sweep.
// FUN_0045a510 [EXPORT 0045a510.c lines 266..283]: while HP >= 1 and the counter +0x5dc is
// non-zero, the step's x / z gain +0x5d0 / +0x5d8 and a positive counter counts down; a dead
// actor's push is cleared. S_2014's water streams (EvtWindYoko) refresh a 4-tick push every tick,
// S_1013's pit (EvtWindArizigo) pulls toward (0, 0, 8).
if (player.hp < 1) {
    csHost.pforce = {};
} else if (csHost.pforce.n != 0) {
    feet.x += csHost.pforce.fx;
    feet.z += csHost.pforce.fz;
    if (csHost.pforce.n > 0) --csHost.pforce.n;
}
