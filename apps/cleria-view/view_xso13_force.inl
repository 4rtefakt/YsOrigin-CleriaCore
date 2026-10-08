// The script pushes 0x15E ChrAllInEventBox_SetForce / 0x15F ChrAll_SetPointForce on the player
// (build/event_re/FLOOR13_OPS.md), included in the player step before the wall sweep.
// FUN_0045a510 [EXPORT 0045a510.c lines 266..283]: while HP >= 1 and the counter +0x5dc is
// non-zero, the step's x / z gain +0x5d0 / +0x5d8 and a positive counter counts down; a dead
// actor's push is cleared. S_2014's water streams (EvtWindYoko) refresh a 4-tick push every tick,
// S_1013's pit (EvtWindArizigo) pulls toward (0, 0, 8).
// The vertical part (S_6009's updrafts, 0x15e with fy 0.2; ROUTE_F47.md B2): while the counter runs and
// +0x5d4 is non-zero, the vertical timer +0x330 is raised to it before the step's dy is taken [EXPORT
// 0045a510.c:145-151], and a rising step under a push clears the grounded byte +0x388 [EXPORT
// 0045a510.c:270-272]. [approx] "the step rises" is read as the timer being positive, and the byte is
// cleared here, before dy (the engine clears it after: this tick's dy is 0.5 g t^2 instead of t).
if (player.hp < 1) {
    csHost.pforce = {};
} else if (csHost.pforce.n != 0) {
    if (csHost.pforce.fy != 0.0f && pv.t < csHost.pforce.fy) pv.t = csHost.pforce.fy;
    if (pv.t > 0.0f) pv.grounded = false;
    feet.x += csHost.pforce.fx;
    feet.z += csHost.pforce.fz;
    if (csHost.pforce.n > 0) --csHost.pforce.n;
}
