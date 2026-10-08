#!/usr/bin/env python3
"""CLERIA_DRIVE regressions for cleria-view: scripted live-game runs checked by their stdout.

Each case launches cleria-view on a room with a CLERIA_DRIVE command list (see the
CLERIA_DRIVE comment in apps/cleria-view/view_game.inl) and requires every `expect`
substring to appear in the output, in order. Saves go to a scratch directory
(CLERIA_SAVE_DIR), never ./saves.

    python tools/drive_regress.py                 # all cases
    python tools/drive_regress.py statue_swap     # one case
    python tools/drive_regress.py --exe build_diff/Release/cleria-view.exe --assets <dir>
"""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "Release", "cleria-view.exe")
ASSETS = os.path.join(ROOT, "assets")

# The S_1009 statue cleanse (Yunica, flags 310 visited / 84 the Crystal), confirmed through
# the narration box: SAVE.XSO's 311 = 1 must reach its s_SaveObjectChange child.
CLEANSE = "5:flag=310,1;6:flag=84,1;10:at=-0.036,8.2,0;20:act;60:ok;80:ok;100:ok;120:ok;140:ok;160:ok"

# S_2015 -> S_2010 by its door, then the swim to the S_2011 door (issue #47).
_S2010_ENTRY = ("5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;10:exit=1;40:move=0,1,75;120:move=-1,-0.4,75;"
                "210:every=12,600,jump;400:move=-1,0.3,150;560:move=-0.2,1,600;1240:quit")

CASES = {
    # STATUE_CLEANSE.md D2: the touch cleanse swaps save_01 for save_03 at once, and the
    # cleansed statue's @Save menu -> Save opens the book (0xe0), which writes slot 1.
    "statue_swap": dict(
        room="S_10/S_1009/S_1009",
        drive=CLEANSE + ";220:act;300:ok;400:ok;450:esc;500:quit",
        expect=["[probe] save_01 -> save", "chest save_03", "[probe] save_03 -> @save",
                "SaveMenu_Enter -> the book's Save page", "[cont] snapshot S_1009"]),
    # The blessing shop (@GrowMenu): buying "Strengthen current armor" for 100 SP, then
    # "Nothing right now" (the cancel) ends every script and thaws the world.
    "blessing": dict(
        room="S_10/S_1009/S_1009",
        drive=CLEANSE + ";200:sp=250;220:act;290:down;300:ok;400:ok;480:ok;570:ok;650:ok;651:scripts;730:ok;"
              "820:esc;830:scripts;840:quit",
        expect=["[probe] save_03 -> @save", "script @GrowMenu", "script @GrowYes", "t830 scripts",
                "frozen 0 fix 0/0"]),
    # The game over: HP 0 -> 100 frames -> gameover.dds + Retry / Return to Title; Retry
    # re-enters the snapshot's room at its pose (the room root's SaveContData).
    "gameover_retry": dict(
        room="S_10/S_1009/S_1009",
        drive="10:move=1,0,40;70:hp=0;220:ok;260:pos;300:quit",
        expect=["[cont] snapshot S_1009 (0.05, 2.00, 7.80)", "[death] HP 0", "[gameover] gameover.dds",
                "[gameover] chose retry", "Retry -> S_1009 (0.05, 2.00, 7.80), retries 1",
                "feet (0.055, 2.000, 7.800)"]),
    "gameover_title": dict(
        room="S_10/S_1009/S_1009",
        drive="70:hp=0;200:down;210:ok;400:quit",
        expect=["[gameover] gameover.dds", "[gameover] chose title", "Return to Title",
                "[title] t60 fader 0.00 list 1 book 0"]),
    # PLAYTEST_1_MISC.md #3: the pause book's Option > Return to Title > Yes (FUN_005b8d40, box 0xf) leaves
    # the game for the title: its list is up once the entry fade-in is over (the drive shot, title.ppm).
    "book_return_title": dict(
        room="S_10/S_1009/S_1009",
        drive="20:esctap;70:right;80:right;90:right;120:ok;160:up;200:up;230:ok;260:left;290:ok;400:quit",
        expect=["[book] Return to Title", "[game] Return to Title: back to the title",
                "[title] t60 fader 0.00 list 1 book 0"]),
    # PLAYTEST_1_MISC.md #1: the book's Crystal map starts @UseWing with the player bound (FUN_005abd10
    # 0x5accea), so its Eft_LocateChr(51, 998, .., 'warp') rides the player. The cleanse unlocks warp 1.
    "crystal_warp_book": dict(
        room="S_10/S_1009/S_1009",
        drive=CLEANSE + ";240:esctap;290:right;330:ok;360:up;370:up;380:left;390:left;400:left;410:left;"
              "430:ok;470:ok;510:up;530:ok;570:fx;900:quit",
        expect=["t570 fx", "'warp' id 51 alpha 1.000 -> 1.000 attach 1", ">> entering S_1009"]),
    # PLAYTEST_1_MISC.md #2: a Yunica / Hugo cleanse places eft_clear 221 (SAVE.XSO pc 69) and the aura 84
    # (s_SaveObjectChange pc 111), never Toal's aura 85; Toal's places 85 alone.
    "cleanse_fx_hugo": dict(
        room="S_10/S_1009/S_1009", char="2",
        drive=CLEANSE + ";90:fx;300:fx;320:quit",
        expect=["t90 fx", "'eft_clear' id 221", "'eft_heal' id 84", "t300 fx", "'eft_heal' id 84"],
        absent=["'eft_heal' id 85"]),
    "cleanse_fx_toal": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="5:flag=310,1;6:flag=86,1;10:at=-0.036,8.2,0;20:act;90:fx;300:fx;320:quit",
        expect=["t90 fx", "'eft_heal' id 85", "t300 fx", "'eft_heal' id 85"],
        absent=["'eft_heal' id 84", "'eft_clear' id 221"]),
    # The shell's Cutscene skip = Double press (build/shell_re/ACCESS.md §1): during the cleanse the first
    # Esc only shows the prompt (the world stays frozen), the second skips to the end.
    "access_skip_double": dict(
        room="S_10/S_1009/S_1009",
        drive="5:flag=310,1;6:flag=84,1;8:opt=CutsceneSkip=1;10:at=-0.036,8.2,0;20:act;50:esctap;55:scripts;"
              "80:esctap;85:scripts;90:quit",
        expect=["[access] skip prompt: press again", "t55 scripts", "frozen 1",
                "[access] cutscene skip (double)", "t85 scripts", "frozen 0 fix 0/0"]),
    # The Cheats (ACCESS.md §2): Damage dealt x10 kills the Selnarg (74 HP) in 4 hits of 20/30, EXP x2
    # credits 102, Damage taken 0 keeps the 60 HP it would otherwise lose some of.
    # 2026-10-07: was 66 (= the old max(1, round(33 x 2))). The kill award is now the engine's
    # (src/cleria/exp_award, FUN_004fbb40 + FUN_004fa2f0): the lv3 Selnarg for a lv1 player is
    # 33 x 1.5^2 x 0.6666 = 49.495, x2 (ExpRate, retail's rate slot) = 98.99 -> floor 98, x the
    # 4-hit combo's chain bonus 1.04 = 101.92.
    "access_cheats": dict(
        room="S_10/S_1002/S_1002",
        drive="8:opt=DamageDealt=10;9:opt=ExpRate=2;10:opt=DamageTaken=0;15:stats;" +
              ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 400, 25)) + ";420:stats;430:quit",
        expect=["stats exp 0 lv 1 hp 60/60", "stats exp 102 lv 1 hp 60/60"]),
    # FUN_00459ff0: a SEN arrival stands on the floor below the arrival point, not on the room's
    # top surface at that x/z (S_2008's door mouth at y 0 was put on the y 14 slab above it).
    "arrival_floor": dict(
        room="S_20/S_2007/S_2007",
        drive="40:exit=1;60:pos;70:quit",
        expect=[">> entering S_2008", "arrival @ (1.0,0.0,-8.0) [SEN]", "feet (1.000, 0.000, -8.000)"]),
    # S_2006 has no CAST_ADOL: entering it by a door still puts the player on foot (not free-fly).
    "arrival_on_foot": dict(
        room="S_20/S_2004/S_2004",
        drive="20:exit=2;40:move=1,0,60;110:pos;120:quit",
        expect=[">> entering S_2006", "arrival @ (0.0,0.0,0.0) [SEN]", "feet (8.316, 0.000, 0.000)"]),
    # Speedrun mode (build/shell_re/SPEEDRUN.md): the default Yunica route's first split
    # (boss:S_1099) fires by itself when Velagunder's HP gauge empties (156 = the fight flag; Lv 60).
    "speedrun_autosplit": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="60"),
        drive="5:speedrun=on;10:speedrun=start;150:flag=156,1;"
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(200, 2400, 12)) + "2410:speedrun=status;2420:quit",
        expect=["[speedrun] phase 1 split 0/8", "auto-split 0 'Velagunder' on boss gauge empty in S_1099",
                "[speedrun] phase 1 split 1/8"]),
    # Velagunder's attacks (build/boss_re/VELAGUNDER.md §5, §7, §3.4), Yunica Lv 15: the intro by its event
    # box, the stun part broken twice, then standing in the arena. The bubble spray hits (EF2 323), the player
    # left standing on the (wood) body after a stun draws the taunt (state 8, ACTION_01), the run drops M890
    # Spawns (the boss level, the chaser first), the laser's terrain-ray bursts hit (EF2 326), the inhale
    # swallows Spawns and the bite (part 2) lands.
    "velagunder_attacks": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="15", CLERIA_BOSSLOG="1", CLERIA_DMG="1"),
        drive="10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 1801, 40))
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2000, 3201, 12)) + "3300:at=0,19,180;5500:quit",
        expect=["[boss] Velagunder: HP 700 STR 70 DEF 53 lv 8", "[boss] 31 body entries (820 tris)",
                "fx 323 (atk 70 power 0.0 knock 5.00", "state 8 sub 1", "clip ACTION_01",
                "M890 #175 spawned", "hp 58 lv 8 mode 1", "fx 326 (atk 70 power 0.0 knock 1.00 rtype 1", "eaten (1 so far)",
                "velagunder part 2 (STR 70 power 14.0 rtype 2"]),
    # ... and the kill (Lv 60): the death timeline's end block sets flag 220 and awards the EXP, which releases
    # BATTLE_0.XSO into SENTOUGO.XSO (the stairs rise: stepup_se).
    "velagunder_kill": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="60"),
        drive="10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 1801, 40))
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2000, 3000, 12)) + "3000:at=0,22,180;3460:scripts;"
              + "".join(f"{t}:ok;" for t in range(3500, 3661, 40)) + "3700:scripts;3710:quit",
        expect=["flag 220 = 1", "EXP 86 (lv 8)", "script sentougo", "script stepup_se"]),
    # FUN_00561f40 queues a 0x8040-flag material (floor 2's water surfaces) on render layer 0xb,
    # flushed after the actor sprites (layers 6/9): S_2007's two water batches draw last, so a
    # submerged player shows through them.
    "water_layer": dict(
        room="S_20/S_2008/S_2008",
        drive="20:exit=0;40:quit",
        expect=[">> entering S_2007", "layer 0xb: 2 batches (drawn after the actors)"]),
    # FUN_005f8de0's cutout depth pre-pass (MATERIAL.md §6.4): S_0002's Roda-tree foliage (00znuki00,
    # flags 0x10, z-write off) lays down its solid texels' depth, so the mountain band 00znuki10
    # (0x20000: no pre-pass) drawn after it no longer paints over the tree in Hugo's prologue.
    "cutout_prepass": dict(
        room="S_00/S_0002/S_0002",
        drive="20:quit",
        expect=["depth pre-pass: 1 batches (FUN_005f8de0)"]),
    # op 0xa2 Scene_SetClipPlain (FUN_004348a0): S_1101's root sets the projection to (1, 4000). The
    # frame used a near plane of room diag x 0.01 = 60 units here (the B_100 skybox is ~6000 across),
    # which clipped the corridor and the player away, leaving only the skybox.
    "skybox_clip": dict(
        room="S_11/S_1101/S_1101",
        drive="20:quit",
        expect=["[scene] clip planes 1.0 / 4000.0 (op 0xa2)"]),
    # Leaving S_2099 mid-intro (a frozen world) used to snap the arrival to the highest surface
    # at that x/z: S_2012's closed door_01 top, y 10.5. The snap is now the floor below the origin.
    "frozen_arrival_floor": dict(
        room="S_20/S_2099/S_2099",
        drive="20:exit=0;40:pos;50:quit",
        expect=[">> entering S_2012", "arrival @ (28.0,2.0,21.0) [SEN]", "[cont] snapshot S_2012 (28.00, 2.00, 21.00)",
                "feet (28.000, 2.000, 21.000)"]),
    # Water mode 2 (the 0x68a20c room list): without the Scales held O2 drains 10 a step and the
    # player drowns at maxHP/60 a step; holding them (0x76a62c == 0x61) drains 1 (WATER.md §3).
    "drown_no_scales": dict(
        room="S_20/S_2008/S_2008",
        drive="600:pos;620:pos;700:quit",
        expect=["O2 10 a255 hp 60 sub 1", "O2 0 a255 hp 41 sub 1", "[death] HP 0"]),
    "scales_air": dict(
        room="S_20/S_2008/S_2008",
        drive="2:held=97;700:pos;710:quit",
        expect=["O2 5292 a255 hp 60 sub 1"]),
    # ROUTE_F13 D2, the ceiling while swimming (FUN_0045a510's ceiling ray 0x45b219 + the sphere sweep's
    # push-out of an embedded contact). Mashing jump every 4 ticks under S_2010's y 13 slab: the top of
    # the 0.675 sphere stays at 13 (feet 11.25); without it the player rose through the room to y 60.
    "swim_ceiling": dict(
        room="S_20/S_2010/S_2010",
        drive="5:opt=InfiniteOxygen=1;6:held=97;20:every=4,620,jump;620:pos;621:quit",
        expect=["feet (7.983, 11.2"]),
    # ...and the open shaft still climbs: west to x 0, up the shaft, over to the S_2011 door (y 15.75+).
    "swim_up_s2011": dict(
        room="S_20/S_2010/S_2010",
        drive="5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;20:move=-1,-0.4,75;110:every=12,500,jump;"
              "300:move=-1,0.3,150;460:move=-0.2,1,600;1100:quit",
        expect=[">> entering S_2011"]),
    # Issue #47 (ROUTE_F13_HUGO HD2): S_2010 swum from its real entry, the S_2015 door's arrival (8, -2, -2)
    # on the stairs, for Yunica and Hugo: up the stairs to the landing, west to the open column at x ~0
    # (the only clear rise; the landing's north side is under the y 3 overhang and the y 13 slab), straight
    # up, then west and north into the S_2011 door. A straight diagonal from the stairs sticks under them.
    "swim_s2010_entry_y": dict(
        room="S_20/S_2015/S_2015", char="1",
        drive=_S2010_ENTRY,
        expect=[">> entering S_2010", "arrival @ (8.0,-2.0,-2.0)", ">> entering S_2011"]),
    "swim_s2010_entry_h": dict(
        room="S_20/S_2015/S_2015", char="2",
        drive=_S2010_ENTRY,
        expect=[">> entering S_2010", "arrival @ (8.0,-2.0,-2.0)", ">> entering S_2011"]),
    # S_2015's tackle wall kabe_00 (flag 337) fills the S_2010 door (y 24..26.5 at z 53.75); the room's
    # ceiling (34) and the lintel keep a swimmer from going over it. Swim up from the arrival, then press
    # into the door; with the wall broken (s_Kabe00) the same door walks through.
    "swim_kabe_s2015": dict(
        room="S_20/S_2015/S_2015",
        drive="5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;20:every=10,1100,jump;330:move=0,1,800;"
              "1100:obj=kabe_00;1110:every=6,1300,jump;1110:move=0,1,190;1300:mesh=kabe_00;1301:quit",
        expect=["at object kabe_00", "mesh kabe_00 pos"], absent=[">> entering S_2010"]),
    "swim_kabe_break": dict(
        room="S_20/S_2015/S_2015",
        drive="5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;8:bracelet=117;20:obj=kabe_00;30:skill+;32:skill-;"
              "200:obj=kabe_00;210:move=0,1,200;420:quit",
        expect=["[hit] kabe_00 -> s_kabe00", ">> entering S_2010"]),
    # PLAYTEST_1 §1: a jump beside a wall rises as high as on open floor (2.63). The ceiling sweep ran
    # from the 0.45 capsule's spot, so the 0.675 sphere's rim caught the wall top / a ledge / a wall's
    # inner seam: S_1002's east wall cut it to 2.43, S_1001's corner (x -16, z 20.7 / 22) to 0.30.
    "jump_wall_s1002": dict(
        room="S_10/S_1002/S_1002",
        drive="10:move=1,0,200;100:jump;118:pos;119:quit",
        expect=["feet (2.536, 2.530, -0.640)"]),   # z: §6 (no more slide along the wall)
    "jump_corner_s1001": dict(
        room="S_10/S_1001/S_1001",
        drive="10:at=-18.5,21,90;40:move=1,0,59;100:jump;118:pos;119:quit",
        expect=["feet (-16.450, -1.383, 21.150)"]),   # z: §6
    # PLAYTEST_1 §4: Hugo through a wall on the way down. A tick's move past the 0.45 radius (a knock's 0.36
    # cap + the run into the wall) put the centre beyond S_1002's one-plane east wall (x ~2.99) and the push-out
    # finished the crossing (t58 x 2.54 -> 3.85, landing at x 17.9). The move is now pushed out in sub-steps.
    "kb_wall_s1002": dict(
        room="S_10/S_1002/S_1002", char="2",
        drive="10:at=2.0,0.6,90;12:move=1,0,100;40:jump;58:kb=2,0;90:pos;91:quit",
        expect=["feet (2.536, -0.098, 0.600)"]),   # z: §6
    # PLAYTEST_1 §5: the B (pad Button 2 = Jump/Cancel) that closes the pause book, still held, is not a jump:
    # the player reads the mapper's pressed edge (+0x24 -> +0x50c), whose history ran through the book ticks.
    # The view's own last-tick latch had gone stale and jumped (feet y 2.489 at t75). X (DIK 45) the same.
    "book_close_b": dict(
        room="S_10/S_1002/S_1002",
        drive="10:pos;20:esctap;60:padhold=1,40;75:pos;76:quit",
        expect=["[book] t60 closed (cancel)", "feet (-0.005, -0.098, -0.640) O2 6000 a0 hp 60 sub 0 sec 0x2"]),
    "book_close_x": dict(
        room="S_10/S_1002/S_1002",
        drive="10:pos;20:esctap;60:keyhold=45,40;75:pos;76:quit",
        expect=["[book] t60 closed (cancel)", "feet (-0.005, -0.098, -0.640) O2 6000 a0 hp 60 sub 0 sec 0x2"]),
    # ...and a tap with no menu open still jumps; B held across the F1 shell's close does not (the re-latch).
    "jump_tap_b": dict(
        room="S_10/S_1002/S_1002",
        drive="20:padhold=1,1;30:pos;31:quit",
        expect=["feet (-0.005, 2.251, -0.640)"]),
    "shell_close_b": dict(
        room="S_10/S_1002/S_1002",
        drive="10:pos;20:shell;21:padhold=1,80;40:pos;41:quit",
        expect=["[drive] t40 pos", "feet (-0.005, -0.098, -0.640) O2 6000 a0 hp 60 sub 0 sec 0x2"]),
    # PLAYTEST_1 §6: an idle player against a wall stays put (retail sweeps a zero move: FUN_0045a510), and
    # one pushing into it holds a steady contact. S_1006's mask wall (z 5, stacked quads), enemies removed:
    # walk north until t145, then nothing; the same feet at t146 (pushing just ended), t200 and t300.
    "idle_wall_s1006": dict(
        room="S_10/S_1006/S_1006", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="15:killall;20:at=24,0,1;25:move=0,1,120;140:pos;141:pos;146:pos;200:pos;300:pos;301:quit",
        expect=["feet (24.275, 0.000, 4.550)"] * 5),
    # PLAYTEST_1 §2: the cutscene-start facing. S_3015's EV_3015_Yunica_1 runs at entry: Fix, Chr_Pos(13)
    # (snaps +0x314 to +0x308), Chr_Dir(13, 315). The world player's +0x308 was the SCT's (heading 90), so
    # she snapped round and turned back (t22 face 100.3); now +0x308 is the arrival heading (135).
    "facing_arrival_s3015": dict(
        room="S_30/S_3009/S_3009",
        drive="10:flag=251,1;20:exit=1;22:chr=13;30:quit",
        expect=["arrival facing 315 deg", "[chr] 13 face 135.0 cmd 135.0", "pos (155.55, 1.50, -7.72)"]),
    # A SEN arrival facing of 0 is Falcom 0 (+Z), not "unset": S_1097 -> S_2000 kept S_1097's facing (-90).
    "facing_arrival_zero": dict(
        room="S_10/S_1097/S_1097",
        drive="5:at=-0.06,0.1,180;20:exit=0;22:chr=13;30:quit",
        expect=[">> entering S_2000", "arrival facing 0 deg", "[chr] 13 face 90.0 cmd 90.0"]),
    # S_3002's soldiers turn under Evnt_3002_Yunica's Fix (LookAtChr, then Chr_Dir); on the thaw #167 used
    # to pop back to its pre-cutscene heading (t1160 face 439.7). +0x314 is one field: it stays at -45.
    "facing_thaw_s3002": dict(
        room="S_30/S_3002/S_3002",
        drive="5:opt=InfiniteHP=1;10:every=30,1170,ok;1160:chr=167;1161:quit",
        expect=["[drive] t1160 chr=167", "[chr] 167 face -45.0 cmd -45.0"]),
    # The SCT yaw +0x108 is an int (0x4342e1 `mov ecx,[edi]` -> FUN_00443a80): S_1004's 180-degree M100
    # (#189) faces heading -90; read as a float it was a denormal and faced 90 like its neighbour #187.
    "sct_yaw_s1004": dict(
        room="S_10/S_1004/S_1004",
        drive="1:chr=189,187;2:quit",
        expect=["[chr] 189 face -90.0 cmd -90.0", "[chr] 187 face 90.0 cmd 90.0"]),
    # PLAYTEST_1 §3: the brains wake on FUN_0045d3c0's 2.5 sphere round the origin, not on the feet point.
    # S_2001's Oggnome #54 begins its 0x12e vanish on tick 1 and freezes off screen at alpha 1; a camera
    # panning so it rises in from the bottom edge used to show that opaque frozen pose until the feet
    # crossed in (t120: culled 1 alpha 1.00); now it wakes first and has vanished (culled 0 alpha 0.00).
    "oggnome_wake_s2001": dict(
        room="S_20/S_2001/S_2001", env=dict(CLERIA_AI="1"),
        drive=";".join("%d:cam=33.1,14,%.2f,90,-45" % (40 + i, 34 - i * 0.12) for i in range(120)) + ";125:quit",
        expect=["[ai] #54 cast 54", "culled 1 sec 302", "alpha 1.00",
                "[ai] #54 cast 54", "culled 0 sec 302 mov 1.00 hp 180.0 mk 0 at (33.1,29.8) alpha 0.00"]),
    # A load is the "load" warp to the snapshot (0x76c13c/140/144), placed by FUN_00459ff0: S_2014's
    # door at y 36 stays at 36 (the old re-ground put it on the y 48.5 roof).
    "saveload_floor": dict(
        room="S_20/S_2008/S_2008",
        drive="2:held=97;20:exit=1;50:saveload;60:quit",
        expect=[">> entering S_2014", "arrival @ (1.0,36.0,0.0) [SEN]", "loaded S_2014 feet (1.000, 36.000, 0.000)"]),
    # The Video page's passes (build/shell_re/VIDEO.md): none at the defaults; Render scale 200% draws the world
    # at 2560x1600; GammaValue 1.5 runs the post pass; StretchAspect pins the projection to 4:3 (FUN_005e2670).
    "video_passes": dict(
        room="S_10/S_1009/S_1009",
        drive="10:vstat;11:vopt=RenderScale=2;20:vstat;21:vopt=RenderScale=1;22:vopt=GammaValue=1.5;30:vstat;"
              "31:vopt=GammaValue=1;32:vopt=StretchAspect=1;40:vstat;50:quit",
        expect=["[video] world 1280x800 scale 1.00 gamma 1.00 post off aspect 1.6000",
                "[video] world 2560x1600 scale 2.00", "gamma 1.50 post on", "aspect 1.3333"]),
    # The frame limiter: FpsCap 30 holds the presented frames at <= 31 a second (the logic keeps 60 Hz).
    "video_fpscap": dict(
        room="S_10/S_1009/S_1009",
        drive="5:vopt=FpsCap=30;200:vstat;210:quit",
        expect=["[video] opt FpsCap=30", "cap 30 held"]),
    # Mod content (build/shell_re/CONTENT.md), the samples/mods/chest_swap sample: the S_1001 chest behind the
    # barrier (kill1 breaks it, ~6 s) gives the NEW item 0x100 instead of the Panacea: the rising icon and the
    # treasure box are re-labelled, the rule fires, the box flag 302 still opens the chest. The sidecar
    # round-trips: saved with 1 lantern, a second given, the load brings back 1.
    "content_chest": dict(
        room="S_10/S_1001/S_1001", mods=["chest_swap"],
        drive="10:kill1;420:obj=box_01;430:act;740:ok;770:cstate;780:csave=2;790:give=0x100;795:cstate;800:cload=2;"
              "810:cstate;820:quit",
        expect=["[content] Chest Swap: 5 file(s)", "icon 0x59 -> 0x100", "[probe] box_01 -> s_box01",
                "chest S_1001/S_BOX01: grant of 0x59 redirected", "item 0x100 x1 (Lantern of Solace) from chest",
                "rule lantern_found fires on item", "chest S_1001/S_BOX01 opened (flag 302)",
                'treasure box 0x59 -> 0x100 "Acquired\\n<color:0xfff2ea8c>Lantern of Solace<color:> x 1."',
                "cstate room S_1001 content 1 items{ 0x100=1 }", "sidecar yso_02.cleria written",
                "items{ 0x100=2 }", "event load", "items{ 0x100=1 }"]),
    # The same chest with the mod installed but turned off: the retail Panacea, no content line at all.
    "content_off": dict(
        room="S_10/S_1001/S_1001", mods=["chest_swap"], mods_off=True,
        drive="10:kill1;420:obj=box_01;430:act;740:ok;770:cstate;780:csave=2;790:quit",
        expect=["[probe] box_01 -> s_box01", "chest S_1001/S_BOX01 opened (flag 302)", "content 0 items{ }"],
        absent=["redirected", "icon 0x59", "treasure box", "sidecar"]),
    # The F1 Mods tab's sub-pages (SHELL_CORE.md tab policy): a Lua mod's page is Mods > <mod> (no tab of its own),
    # Mods > Content additions, an unknown sub-page refused, Back (Esc / the pad's Back) returns to the list and
    # only then would close the panel.
    "shell_subpages": dict(
        room="S_10/S_1009/S_1009", mods=["hello", "chest_swap"],
        drive="60:shell=Mods:sample.hello;80:shellback;100:shell=Mods:content;120:shell=Mods:nope;140:shell=Mods;"
              "160:shellback;180:shell=Hello Lua;200:quit",
        expect=["[lua] Hello Lua: started", "[mods] page: Mods > sample.hello", "[mods] page: Mods\n",
                "shell back on Mods: to the page", "[mods] page: Mods > content", "Mods has no sub-page 'nope'",
                "shell: no page Mods:nope", "[mods] page: Mods\n", "shell back on Mods: would close the panel",
                "shell: no page Hello Lua"]),
    # A rule on a flag (rules.ini of a generated test mod): flag 480 0 -> 1 fires it once (give, text, cflag).
    "content_rule": dict(
        room="S_10/S_1009/S_1009",
        content={"rules.ini": '[rule.flagrule]\nOn=flag 480 >= 1\nDo=give 0x57 2; text "flag rule"; cflag t.x += 5\nOnce=1\n'},
        drive="10:flag=480,1;20:cstate;30:flag=480,0;40:flag=480,1;50:cstate;60:quit",
        expect=["rule flagrule fires on flag", "item 0x57 x2 (Roda Fruit) from rule", 'text "flag rule"',
                "cflags{ rule.flagrule=1 t.x=5 }", "t50 cstate", "cflags{ rule.flagrule=1 t.x=5 }"]),
    # FLOOR13_OPS.md: 0x80 Reg_EntryLabel. Through S_3009's S_3015A door Yunica's hint plays; through
    # S_3008's S_3015B door it does not.
    "entry_label": dict(
        room="S_30/S_3009/S_3009", drive="10:flag=251,1;20:exit=1;40:scripts;41:quit",
        expect=[">> entering S_3015", "script EV_3015_Yunica_1"]),
    "entry_label_b": dict(
        room="S_30/S_3008/S_3008", drive="10:flag=251,1;20:exit=1;40:scripts;41:quit",
        expect=[">> entering S_3015", "t40 scripts"], absent=["EV_3015_Yunica_1"]),
    # WEATHER.md: S_3005's FIRE_DROP (FireDrop(220), Wait_Time(10) = a new ember emitter every 9 ticks)
    # draws the shared xorshift 10 times on each 16th tick of every emitter's life. 330 / 1340 / 2790
    # draws by t100 / t200 / t300 is the export's schedule for births on 1 + 9k.
    "weather_embers": dict(
        room="S_30/S_3005/S_3005", drive="300:quit", env=dict(CLERIA_WX_LOG="1"),
        expect=["[weather] FireDrop(220)", "[weather] t100 steps 100 emitters 12 objects 45 draws 330 ",
                "[weather] t200 steps 200 emitters 23 objects 157 draws 1340 ",
                "[weather] t300 steps 300 emitters 25 objects 284 draws 2790 "]),
    # WEATHER.md: S_3009's FRAME_SMOKE (0x122) and FIRE_BLUR (0x123) run on entry; the haze timer holds
    # the strength the script rolled (0.3 / 0.25 / 0.2).
    "weather_smoke": dict(
        room="S_30/S_3009/S_3009", drive="300:quit", env=dict(CLERIA_WX_LOG="1"),
        expect=["[weather] FrameSmoke(120)", "[weather] FireBlur(1.0000, -0.0025, 0.", "[weather] FireDrop(180)",
                "[weather] t300 steps 300 emitters", "haze 0."]),
    # WEATHER.md §5: FireBlur's warp pass draws while 0x7b249c != 0 and each drawn frame lets the next
    # update step the wave grid (0x7b2e1c); one frame per tick (lockstep) = one step per tick once on.
    # When it turns on depends on FIRE_BLUR.XSO's 0x81 rolls, which draw from the shared game RNG since
    # TRANSIT.md §5 (was 63 / 163 on the old private script stream; the grid math is pinned by weathertest).
    "fireblur_warp": dict(
        room="S_30/S_3009/S_3009", drive="300:quit", env=dict(CLERIA_WX_LOG="1", CLERIA_DRIVE_LOCKSTEP="1"),
        expect=["[weather] FireBlur(1.0000, -0.0025, 0.", "[weather] t200 steps 200 ", "warp 1 grid 44 cam",
                "[weather] t300 steps 300 ", "warp 1 grid 144 cam"]),
    # FLOOR13_OPS.md: a SOB trigger-4 touch volume (the S_1011 ward) runs Touch_kekkai -> 0x15c Chr_Damage.
    "ward_touch": dict(
        room="S_10/S_1011/S_1011", drive="30:obj=kekkai_01;32:hp=999;35:move=-1,1,40;80:quit",
        expect=["[touch] kekkai_01 -> touch_kekkai", "Chr_Damage player atk 50 -> HP"]),
    # FLOOR13_OPS.md: a breakable runs its own @Break_30 (0x40 / 0xb5 / 0x17c) instead of the stand-in.
    # Its two 50 % rolls (0x81) draw the one game generator 0x74622c (build/warp_re/TRANSIT.md §5): 66 misses,
    # 67 drops. (The script VM's private copy, fresh from the .data seed, rolled 48 then 85: 66 dropped.)
    "break_script": dict(
        room="S_10/S_1001/S_1001", drive="30:obj=break_01;45:atk;60:scripts;150:scripts;160:quit",
        expect=["[drop] item 67 at break_01", "[hit] break_01 -> @break_30", "script @break_30", "t150 scripts"]),
    # The kill EXP (src/cleria/exp_award; FUN_004fbb40 0x4fcb45.., FUN_004fa2f0): the lv3 Selnarg
    # (+0xb0 = 33) killed with Damage dealt x10 at three player levels. Award = max(1, floor(33 x
    # level_scale x 0.6666)) x (1 + 0.01 combo); combo = the damaging hits it took.
    #   lv1 (2 under): 33 x 1.5^2 x 0.6666 = 49.495 -> 49, 4 hits -> 49 x 1.04 = 50.96
    #   lv3 (even):    33 x 0.6666 = 21.998 -> 21,         2 hits -> 21 x 1.02 = 21.42
    #   lv5 (2 over):  33 / 1.5^2 x 0.6666 = 9.777 -> 9,   1 hit  -> 9 x 1.01  = 9.09
    "exp_under": dict(
        room="S_10/S_1002/S_1002", env=dict(CLERIA_LEVEL="1"),
        drive="8:opt=DamageDealt=10;10:opt=DamageTaken=0;" +
              ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 400, 25)) + ";420:abil;430:quit",
        expect=["[abil] t420", "combo 4/", "exp 50.96 lv 1"]),
    "exp_even": dict(
        room="S_10/S_1002/S_1002", env=dict(CLERIA_LEVEL="3"),
        drive="8:opt=DamageDealt=10;10:opt=DamageTaken=0;" +
              ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 400, 25)) + ";420:abil;430:quit",
        expect=["[abil] t420", "combo 2/", "exp 21.42 lv 3"]),
    "exp_over": dict(
        room="S_10/S_1002/S_1002", env=dict(CLERIA_LEVEL="5"),
        drive="8:opt=DamageDealt=10;10:opt=DamageTaken=0;" +
              ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 400, 25)) + ";420:abil;430:quit",
        expect=["[abil] t420", "combo 1/", "exp 9.09 lv 5"]),
    # Skills on a fresh game (ABILITIES_VERIFIED.md §2): no bracelet owned -> the wheel equips nothing and
    # the skill button casts nothing; with Ventus (item 116) owned the wheel equips it and a tap casts
    # Whirlwind, the normal section 0x10d (PTR_0068afe0), for 700 MP.
    "skill_gate": dict(
        room="S_10/S_1002/S_1002",
        drive="10:wheel=-120;20:skill+;40:skill-;42:abil;50:flag=116,1;55:wheel=-120;75:abil;80:skill+;"
              "100:skill-;102:abil;110:quit",
        expect=["[abil] t42 sec 0x2 mp 3000 lag 3000 boost 0 boosting 0 iframes 0 magic -1",
                "[abil] t75 sec 0x2 mp 3000 lag 3000 boost 0 boosting 0 iframes 0 magic 116",
                "[abil] t102 sec 0x10d mp 2300"]),
    # The opening Riffligan fight's fence (src/cleria/arena_player.hpp, FUN_0046fba0 0x4700aa): BattleStart1
    # sets arena mode 2 (flag 190) and EnclosePlayer(0, 0, -1.3, 14); running north at the great staircase
    # the ring's knock-channel push holds the player at radius ~14. With 190 = 0 (BATTLE_0's own write at
    # pc 371 when the fight ends) the same run goes up the staircase.
    "opening_arena": dict(
        room="S_10/S_1000/S_1000",
        drive="5:opt=DamageTaken=0;" + ";".join("%d:ok" % t for t in range(40, 3370, 40)) +
              ";3400:scripts;3410:arena;3420:at=0,0;3422:move=0,1,260;3660:arena;3705:flag=190,0;3706:at=0,0;"
              "3708:move=0,1,260;3990:arena;4000:quit",
        expect=["script battle_0", "t3410 arena", "arena mode 2 rect (0, 0, -1.3, 14) dist 2.",
                "t3660 arena", "arena mode 2 rect (0, 0, -1.3, 14) dist 14.", "t3990 arena",
                "arena mode 0 rect (0, 0, -1.3, 14) dist 35."]),
    # The HUD's equipped-skill badge (view_hud.inl skill_badge_cell, builder A 0x43e156): none on a fresh
    # game; with all three bracelets owned the wheel cycles Ventus -> Ignis -> Terra and the badge follows
    # (Yunica's column U 120, rows V 132 / 212 / 172).
    "hud_skill_badge": dict(
        room="S_10/S_1002/S_1002",
        drive="10:hudskill;20:flag=116,1;21:flag=117,1;22:flag=118,1;25:wheel=-120;45:hudskill;60:wheel=-120;"
              "80:hudskill;100:wheel=-120;120:hudskill;125:quit",
        expect=["hud skill -1 badge none", "hud skill 116 badge (120, 132)", "hud skill 118 badge (120, 212)",
                "hud skill 117 badge (120, 172)"]),
    "hud_skill_badge_hugo": dict(
        room="S_10/S_1002/S_1002", env=dict(CLERIA_CHAR="2"),
        drive="20:flag=116,1;25:wheel=-120;45:hudskill;50:quit",
        expect=["hud skill 116 badge (360, 132)"]),
    # build/audit/PLAYTEST_1_HUD.md item 1 (src/cleria/hudstate.hpp skill_ready): builder B lights the badge
    # and the MP strip while Yunica's Ventus (700) is affordable, not at MP 300; charged (Lv2, held) it
    # stays lit (the pale cell). A "Reduce MP usage" blessing (0x76bc80 & 0x40) skips the MP test (0x43e933).
    "hud_skill_ready": dict(
        room="S_10/S_1002/S_1002",
        drive="10:hudstate;20:flag=116,1;21:flag=182,2;25:wheel=-120;45:hudstate;50:mp=300;52:hudstate;"
              "60:mp=3000;61:skill+;200:hudstate;201:skill-;230:mp=300;231:flag=217,64;233:hudstate;240:quit",
        expect=["[hud] ready 0 charge 0", "mp 3000 magic -1", "[hud] ready 1 charge 0", "mp 3000 magic 116",
                "[hud] ready 0 charge 0", "mp 311 magic 116", "[hud] ready 1 charge 2", "[hud] ready 1 charge 0",
                "mp 322 magic 116"]),
    # Item 2 (hudstate.hpp tick_buffs; builder A 0x43d9a5 bars, builder D 0x440420 lines): the Strength /
    # Defense / MP Drops touched at the feet start their 1800-tick timers, which count down once a tick
    # (FUN_00462c40) and end the buffs at 0.
    "hud_buffs": dict(
        room="S_10/S_1002/S_1002",
        drive="20:drop=72;21:drop=74;22:drop=76;80:hudstate;1900:hudstate;1901:quit",
        expect=["str 0.5/1790 def 0.3/1791 mp 1.2/1792 full 1800", "str 0.0/0 def 0.0/0 mp 0.0/0 full 1800"]),
    # Item 3 (FUN_004fae00): a landed swing pops the white number on the Selnarg (FUN_004fbb40 0x4fd241),
    # the combo line's meter starts (0x76c1a8), and a hit on the player pops the red one (FUN_004fd350
    # 0x4fe120).
    "hud_damage_numbers": dict(
        room="S_10/S_1002/S_1002",
        drive="100:near=17;101:atk;140:hudstate;150:hurt=60;160:quit",
        expect=["[popup] 2 ffffff", "combo 1/", "[popup] 21 ff0000"]),
    # Boost (ABILITIES_VERIFIED.md §1, audit combat.md #20-#22): a 21-damage hit adds 2100 to the gauge;
    # entering grants 60 i-frames (59 a tick later), so a hit 30 ticks in is refused (HP stays 39) and
    # one after the window lands halved (21 -> 10).
    "boost_iframes": dict(
        room="S_10/S_1002/S_1002",
        drive="3:flag=187,1;5:hurt=60;6:abil;41:boost=30000;45:boostkey;46:abil;75:hurt=60;76:abil;"
              "105:hurt=60;106:abil;110:quit",
        expect=["Chr_Damage player atk 60 -> HP 39", "[abil] t6 sec 0xb mp 3000 lag 3000 boost 2103",
                "[abil] t46 sec 0x2 mp 3000 lag 3000 boost 29970 boosting 1 iframes 59",
                "[drive] t75 hurt=60", "[abil] t76", "hp 39/60",
                "Chr_Damage player atk 60 -> HP 29", "[abil] t106"]),
    # Hugo's basic shot (FireBullet(2, 130): its EF2 particles, side 0) damages through FUN_004fa550
    # like a swing, and each damaging hit counts on the combo meter.
    "hugo_shot": dict(
        room="S_10/S_1002/S_1002", env=dict(CLERIA_CHAR="2", CLERIA_DMG="1"),
        drive="10:abil;" + ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 200, 25)) +
              ";210:abil;220:quit",
        expect=["hit Selnarg (lv3 DEF 53): 2 dmg, 72/74 left", "hit Selnarg (lv3 DEF 53): 2 dmg, 70/74 left",
                "[abil] t210", "combo 16/"]),   # #57: his two eyes' section-7 shots (0x82) land too
    # build/audit/ROUTE_F13_HUGO.md: a player particle runs the rest of FUN_004fed50's mode-0 scan
    # (EFFECTHIT.md §5.1): Hugo's shot smashes a pot (@Break_30) and breaks the S_2008 wall (0x186 child
    # sphere -> s_Kabe00, 471), which only a swing could do before. The S_2015 wall is the way to S_2010.
    "hugo_shot_breaks_pot": dict(
        room="S_10/S_1001/S_1001", env=dict(CLERIA_CHAR="2"),
        drive="30:obj=break_01;31:atk;55:atk;80:atk;105:atk;160:quit",
        expect=["[hit] break_01 -> @break_30"]),
    # #53: Hugo's normal shot is class 3 (FUN_0046d5e0, his attack sections); the wall resists it (thunder only).
    "hugo_shot_wall_immune": dict(
        room="S_20/S_2008/S_2008", env=dict(CLERIA_CHAR="2"),
        drive="5:opt=InfiniteHP=1;20:at3=-48,-2,-0.5,0;21:atk;41:atk;61:atk;81:atk;101:atk;140:flagdump;150:quit",
        expect=["t140 flagdump"], absent=["[hit] kabe_00", " 471=1"]),
    # #57: Hugo's skills act through his Eyes of Fact (cast 0xb0, build/ability_re/EYE_FAMILIARS.md §7..§12).
    # Trap Mine (117): FUN_0046d8c0 lays the free eye whose slot is the free count at Hugo's spot, fuse 60;
    # at 0 it plays M_900 section 5, whose FireBullet(0, 0x88) is thunder (FUN_0046d650) and breaks the
    # S_2015 wall (the way to S_2010).
    "kabe_thunder_hugo": dict(
        room="S_20/S_2015/S_2015", char="2", env=dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_DMG="1"),
        drive="5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;8:bracelet=117;20:obj=kabe_00;30:skill+;32:skill-;"
              "33:abil;200:obj=kabe_00;210:move=0,1,200;420:quit",
        expect=["[abil] t33 sec 0x10c", "fires 0x88 (class 2)", "[hit] kabe_00 -> s_kabe00", ">> entering S_2010"]),
    # ... the charged Trap Mine (Lv2, c138 == 2): every free eye laid on Hugo's spot, fuse 180; the expiry
    # latches P+0x7d4 and FUN_0046a3e0 sets off EF2 0x93 (class 6 = thunder) there.
    "hugo_mine_charged": dict(
        room="S_20/S_2008/S_2008", char="2", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="5:opt=InfiniteHP=1;6:opt=InfiniteOxygen=1;7:held=97;8:bracelet=117;9:flag=183,1;"
              "20:at3=-48,-2,-0.5,0;100:skill+;240:skill-;241:abil;400:flagdump;460:flagdump;470:quit",
        expect=["[abil] t241 sec 0x10f", "t400 flagdump", "[hit] kabe_00 -> s_kabe00", " 471=1"]),
    # Fire Wheel (118): flags 157 / 158 = 1 / 150, the eyes circle Hugo at 1.5 (mode 2) carrying EF2 0x89
    # (class 1, fire), which lights the S_3004 torch from the floor.
    "torch_fire_hugo": dict(
        room="S_30/S_3004/S_3004", char="2", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="5:opt=InfiniteHP=1;8:bracelet=118;20:at3=66.6,3,38,90;60:skill+;62:skill-;63:abil;250:flagdump;251:quit",
        expect=["[abil] t63 sec 0x10e", "[hit] fire_01 -> s_syokudai01", " 354=1"]),
    # FUN_0046d6e0: each of Hugo's attacks asks his free eyes for section 7, the eye's own shot (0x82).
    "hugo_eyes_shoot": dict(
        room="S_10/S_1002/S_1002", char="2", env=dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_DMG="1"),
        drive="5:opt=InfiniteHP=1;200:near=17;201:atk;225:near=17;226:atk;260:quit",
        expect=["section 7 on", "fires 0x82 (class 3)"]),
    # build/audit/ROUTE_F13.md: leaving a ward room ends its scripts; the KekkaiZako watcher no longer gets a
    # tick in the next room (no seeds there) and clears the ward (S_2008's 336) without a fight.
    "ward_leave_kept": dict(
        room="S_20/S_2007/S_2007", drive="20:exit=1;60:exit=0;100:flagdump;110:quit",
        expect=[">> entering S_2008", ">> entering S_2007", "[flags]"], absent=[" 336=1"]),
    # ROUTE_F13.md: a Chr_LocateDummy talk target stays at its script point (S_3014's door, y 8), not on the
    # closed door's top, so the action probe from the floor reaches it.
    "dummy_floor": dict(
        room="S_30/S_3014/S_3014", drive="5:flag=374,1;10:exit=0;40:exit=4;100:chr=166;110:quit",
        expect=[">> entering S_3014", "[chr] 166", "pos (-43.80, 8.00, 38.00)"]),
    # ROUTE_F13.md: the action probe compares origins (FUN_00460470 +0x54): a talk target with no sprite
    # (CAST_C000, the S_1009 Blue Moon altar's dummy 167) floats +0x324 above its floor like any actor, so
    # the player on the dais in front of it reaches it.
    "dummy_probe": dict(
        room="S_10/S_1009/S_1009", drive="5:flag=111,1;30:at3=0,9,33.3,0;31:act;60:scripts;70:quit",
        expect=["[probe] actor 167 -> s_bluemoon", "script s_bluemoon"]),
    # ROUTE_F13.md: a live Mesh_Delete (0x169) drops the object's draw and collision at once. S_1015's
    # trap: the chest raises kekkai_01, the seeds die, StepMove_01 deletes it; walking out of the cage
    # used to stop on the deleted ward (and re-run Touch_kekkai) until the room was reloaded.
    "mesh_delete_live": dict(
        room="S_10/S_1015/S_1015",
        drive="10:obj=box_01;11:act;" + ";".join(f"{t}:ok" for t in range(40, 900, 25)) + ";900:kill1;"
              + ";".join(f"{t}:ok" for t in range(925, 1500, 25)) + ";1510:obj=box_01;1511:move=0.976,-0.22,250;"
              "1800:pos;1810:quit",
        expect=["[probe] box_01 -> s_box01", "t900 kill1", "t1800 pos"], absent=["[touch] kekkai_01"]),
    # ROUTE_F13.md: Chr_Muteking(998, 0) clears the i-frames (FUN_00571390). @UseWing's 6000 i-frames
    # (the Crystal warp) used to survive it, so a ward or a fire vent did no damage for 100 s after a warp.
    "muteking_zero": dict(
        room="S_10/S_1011/S_1011",
        drive="10:flag=137,2;11:run=@UseWing;700:obj=kekkai_01;702:hp=999;705:move=-1,1,40;800:quit",
        expect=["run @UseWing -> started", "ChangeScene 's_10\\s_1011' -> S_1011", "[touch] kekkai_01 -> touch_kekkai",
                "Chr_Damage player atk 50 -> HP"]),
    # ROUTE_F13_TOAL.md F-T1: 0x13d Chr_IsInEventBox tests the actor's ORIGIN (+0x54 = floor + 1.075,
    # FUN_00570df0), not the feet. Toal's S_2080 box EVT2_2080 starts 0.05 above the floor, so with the feet
    # it never fired and his floor-2 duel (s_Evt2080_4 -> Battle_2, M810 Hugo) could not start.
    "evtbox_origin_toal": dict(
        room="S_20/S_2080/S_2080", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="20:at3=0,2,50,0;80:scripts;81:quit",
        expect=["script s_Evt2080_4"]),
    # ROUTE_F13_TOAL.md F-T2: a scene change spawns a new player actor (FUN_00431260 0x74c09c = 0,
    # FUN_00434020 -> FUN_004590b0 zeroes +0x734), so i-frames do not carry over. Boost's 60 here; a
    # boss's 9999 (FUN_00461120(2)) used to last ~166 s into the next floor.
    "iframes_scene_reset": dict(
        room="S_10/S_1004/S_1004", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="15:flag=187,1;20:boost=30000;21:boostkey;22:abil;23:exit=0;30:abil;31:quit",
        expect=["boosting 1 iframes 59", ">> entering S_1005", "boosting 1 iframes 0 magic"]),
    # #52: a hit's damage class is the record's +0x109, stamped at arm time from FUN_0046d5e0 (the player's
    # section). Menoak (ENEMYINFO resist 1 / -1 / -5 / 1) takes nothing from melee (class 3) and the minimum
    # from Toal's Thunder Claw (item 117, section 0x10c -> class 2) at Lv 1.
    "menoak_class_toal": dict(
        room="S_20/S_2008/S_2008", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_DMG="1"),
        drive="5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;8:bracelet=117;20:nearest;21:atk;33:atk;45:atk;"
              "100:nearest;101:skill+;103:skill-;104:abil;200:quit",
        expect=["hit Menoak (lv14 DEF 91): 0 dmg, 146/146 left", "sec 0x10c", "hit Menoak (lv14 DEF 91): 1 dmg, 145/146 left"]),
    # #53: a torch's 0x186 child (FUN_005735b0) carries its SOB block (FUN_00501a30): resists 1 / 0 / 1 / 1 and no
    # min-1 chip, so only class 1 (the fire skill, item 118) lights it. 33 melee swings do nothing; Inferno does.
    "torch_fire_toal": dict(
        room="S_30/S_3004/S_3004", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="5:opt=InfiniteHP=1;20:at3=66.6,3,38,90;22:every=12,420,atk;430:flagdump;450:bracelet=118;"
              "460:at3=66.6,3,38,90;462:skill+;464:skill-;600:flagdump;601:quit",
        expect=["t430 flagdump", "[hit] fire_01 -> s_syokudai01", " 354=1"]),
    # ... and Yunica's Phoenix Flame from a jump under it (the torch stands 3 above the floor).
    "torch_fire_yunica": dict(
        room="S_30/S_3004/S_3004", char="1", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="5:opt=InfiniteHP=1;8:bracelet=118;20:at3=68,3,38,90;22:skill+;24:skill-;200:at3=67.3,3,38,90;"
              "202:skill+;204:skill-;400:jump;402:skill+;404:skill-;600:flagdump;601:quit",
        expect=["[hit] fire_01 -> s_syokudai01", " 354=1"]),
    # The crumbling walls (S_2008 / S_2015 / S_4006 kabe_00, flags 0x670): resists 1 / 1 / 0 / 1, only class 2 (the
    # thunder skill, item 117). Yunica's five swings leave S_2008's wall; her Bolt Crash breaks it.
    "kabe_thunder_yunica": dict(
        room="S_20/S_2008/S_2008", char="1", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="5:opt=InfiniteHP=1;6:opt=InfiniteOxygen=1;7:held=97;20:at3=-48,-2,-0.5,0;21:atk;41:atk;61:atk;81:atk;"
              "101:atk;140:flagdump;150:bracelet=117;160:at3=-48,-2,-0.5,0;162:skill+;164:skill-;300:at3=-48,-2,-0.5,0;"
              "302:skill+;304:skill-;450:flagdump;451:quit",
        expect=["t140 flagdump", "[hit] kabe_00 -> s_kabe00", " 471=1"]),
}

# The story duels on floors 1-3 (build/event_re/DUELS.md), each played from the room's event box through
# its cutscene (`ok` every 20 ticks), the fight (`duel=<cast>`: near + attack while 156 = 1 and the world
# is not frozen) and the scripted outcome. Lockstep (one tick a frame) keeps the long runs short and
# deterministic; CLERIA_LEVEL picks how the fight goes.
_S1080 = "21:move=0,-1,300;320:every=20,{end},ok;321:every=7,{end},duel={cast};"
_S3080 = "20:at=-12,36,0;30:move=0,1,30;80:every=20,{end},ok;81:every=7,{end},duel={cast};"
_LOCK = dict(CLERIA_DRIVE_LOCKSTEP="1")
CASES.update({
    # S_1080 Yunica: Vagullion (M460) dies -> Battle_1's Wait_FlagEqualInt(156, 0) -> 230, 311, warp 1.
    "duel_s1080_vagullion": dict(
        room="S_10/S_1080/S_1080", char="1", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=11000, cast=50) + "11000:dflags;11001:quit",
        expect=["[duel] t", "cast 50 hp 200/200", "flags 130=0 131=0 133=0 156=0 174=0 230=1 232=0 311=1"]),
    # D1 (build/audit/ROUTE_F13.md): the long Vagullion fight, through phase 4's dissolve / recall /
    # re-scatter cycles and the split below half HP, to its death at about t9200 (Lv 2; the shell's
    # Damage taken 0 keeps Yunica up, no flag is forced). Before M970's phase-4 box clamp and the
    # sweep's edge / embedded-face fixes a bat stuck over the railing or in a wall pillar never docked,
    # M460's recall (sub 0x14) waited for it forever and 156 stayed 1 (a softlock).
    "duel_s1080_vagullion_long": dict(
        room="S_10/S_1080/S_1080", char="1", env=dict(_LOCK, CLERIA_LEVEL="2"),
        drive=_S1080.format(end=10500, cast=50) + "330:opt=DamageTaken=0;7000:dflags;10500:dflags;10501:quit",
        expect=["[access] opt DamageTaken=0", "cast 50 hp 200/200", "flags 130=0 131=0 133=0 156=1 174=0 230=0",
                "flags 130=0 131=0 133=0 156=0 174=0 230=1 232=0 311=1"],
        absent=["[death]"]),
    # S_1080 Hugo: Epona (M850, Battle_Hugo) -> the post-duel scene (Mask of Eyes) -> 230, 311.
    "duel_s1080_epona": dict(
        room="S_10/S_1080/S_1080", char="2", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=12000, cast=171) + "12000:dflags;12001:quit",
        expect=["[duel] t", "cast 171 hp 190/190", "156=0 174=0 230=1 232=0 311=1"]),
    # S_1080 Toal: Galleon (M830, Battle2) stops at HP 1 (his low-HP chain) -> 174 = 1, 230, 311.
    "duel_s1080_galleon": dict(
        room="S_10/S_1080/S_1080", char="3", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=12000, cast=169) + "12000:dflags;12001:quit",
        expect=["[duel] t", "cast 169 hp 200/200", "156=0 174=1 230=1 232=0 311=1"]),
    # S_1080 is not losable (no 133): a KO is the game over; Retry re-enters at Battle_1's SaveContData
    # (205 = 1), whose root branch restarts the duel.
    "duel_s1080_gameover": dict(
        room="S_10/S_1080/S_1080", char="1", env=dict(_LOCK, CLERIA_LEVEL="1"),
        drive=_S1080.format(end=4900, cast=50) + "4700:hp=0;4900:quit",
        expect=["cast 50 hp 200/200", "[death] HP 0", "[gameover] chose retry", ">> entering S_1080",
                "[gameover] Retry -> S_1080", "cast 50 hp 200/200"], absent=["[duel] lost"]),
    # S_3080 Yunica: Kishgal (M860) ends the fight at half HP in this room -> 174 = 1 -> the Silver
    # Harmonica scene -> 232 = 1 and ChangeScene to S_1000.
    "duel_s3080_kishgal": dict(
        room="S_30/S_3080/S_3080", char="1", env=dict(_LOCK, CLERIA_LEVEL="24"),
        drive=_S3080.format(end=14000, cast=172) + "8100:dflags;14000:dflags;14001:quit",
        expect=["cast 172 hp 300/300", "133=1 156=0 174=1", ">> entering S_1000", "232=1"]),
    # S_3080 Hugo wins against Toal (M820): 174 = 1 -> Battle_Win (its 0.4 slow-mo) -> EV_Feena02_2, where
    # FireBulletEvent fires Shion's six shots (op 0x111) -> S_1000.
    "duel_s3080_toal_win": dict(
        room="S_30/S_3080/S_3080", char="2", env=dict(_LOCK, CLERIA_LEVEL="40"),
        drive=_S3080.format(end=16500, cast=168) + "9000:dflags;16500:dflags;16501:quit",
        expect=["cast 168 hp 300/300", "156=0 174=1", "[xso] SetTimeScale 0.40", "[xso] SetTimeScale 1.00",
                "[xso] FireBullet actor 200 kind 2 effect 308 pos (-12.00, -3.00, 33.00) dir (0.00, -0.19, 1.00) speed 0.30",
                "[xso] FireBullet actor 200 kind 2 effect 308", "dir (0.15, -0.17, 1.00) speed 0.50",
                ">> entering S_1000", "232=1"], absent=["[duel] lost"]),
    # S_3080 Hugo loses (a KO under flag 133, FUN_0046f810): no game over, 174 stays 0 -> Battle_Lose ->
    # EV_Feena02_2 -> S_1000.
    "duel_s3080_toal_lose": dict(
        room="S_30/S_3080/S_3080", char="2", env=dict(_LOCK, CLERIA_LEVEL="20"),
        drive=_S3080.format(end=16500, cast=168) + "8400:hp=0;8500:dflags;16500:dflags;16501:quit",
        expect=["cast 168 hp 300/300", "[duel] lost: flag 133 set", "133=1 156=0 174=0", "[xso] FireBullet actor 200",
                ">> entering S_1000", "232=1"], absent=["[gameover]", "SetTimeScale 0.40"]),
    # S_3080 Toal: Shion (M840, Battle2) -> 174 = 1 -> FireBulletEvent2 (Hugo's shots, one of Shion's) -> S_4104.
    "duel_s3080_shion": dict(
        room="S_30/S_3080/S_3080", char="3", env=dict(_LOCK, CLERIA_LEVEL="28"),
        drive=_S3080.format(end=14000, cast=170) + "14001:quit",
        expect=["cast 170 hp 300/300", "[xso] FireBullet actor 197 kind 2 effect 308", "[xso] FireBullet actor 200",
                "[xso] FireBullet actor 197", ">> entering S_4104"]),
})

# The floor-4 brains (build/ai_spec/M560.md .. M290.md, REVIVE.md, BOMB.md; build/audit/FLOOR4_TOUR.md).
_F4 = dict(CLERIA_AI="1")
CASES.update({
    # M560 Karon: buried in S_4004's sand, surfaces, is killed (one-hit kills), lies opaque for 600
    # dead ticks and revives in place with a new instance id (REVIVE.md §3.5, FUN_00434db0).
    "karon_revive": dict(
        room="S_40/S_4004/S_4004", env=_F4,
        drive="7:opt=InfiniteOxygen=1;8:opt=OneHitKill=1;9:opt=DamageTaken=0;20:at3=4.6,-1,12,90;100:every=12,300,near=#60;"
              "101:every=12,300,atk;350:stats;800:stats;801:quit",
        expect=["[revive] #186 cast 60 hp 261 phase 1 7a0 0 gnd 1 t 0.000 y -4.00", "defeated 1", "stats exp 3 lv 1",
                "cast 60 revived as #207 (section 10)"]),
    # M570 Zarue summons its hands (M410) next to the player in S_4006; they appear and are removed.
    "zarue_summon": dict(
        room="S_40/S_4006/S_4006", env=_F4,
        drive="9:opt=DamageTaken=0;10:at=-3.2,28,90;200:actors;400:actors;401:quit",
        expect=["cast 61 phase 2", "sec 7 ", "[actor] id 45 cast 45 enemy hp 351", "cast 45 phase 2",
                "[actor] id 45 cast 45 enemy hp 0"]),
    # M280 Chups in S_4012 latches on the player from behind (sub 1, section 7).
    "chups_latch": dict(
        room="S_40/S_4012/S_4012", env=_F4,
        drive="9:opt=DamageTaken=0;10:at=0,13,90;400:quit",
        expect=["cast 32 phase 2 sub 1 act 0x2", "sec 7 "]),
    # M290 Bicmobre in S_4019 throws its kind-0xf bombs; killed, it throws its death bombs.
    "bicmobre_bombs": dict(
        room="S_40/S_4019/S_4019", env=_F4,
        drive="9:opt=DamageTaken=0;10:near=#187,7;150:bullets;420:opt=OneHitKill=1;421:near=#187,2;422:atk;"
              "440:near=#187,2;441:atk;520:quit",
        expect=["cast 33 phase 2 sub 3", "[bomb] age", "M290 #187 death bomb (dead tick 15)",
                "M290 #187 death bomb (dead tick 30)"]),
    # The player in S_4004's quicksand (src/cleria/sand.hpp, WATER.md §1a/§2a): walking off the stone she
    # falls into the selector-1 set over sand2 and stays "grounded" in it, walking lifts her 0.0025 a tick
    # (0.01 x the sand2 rise x0.25), standing sinks her 0.015 a tick until she is buried (+0x38c) and the O2
    # drains 100 a tick.
    "sand_wade": dict(
        room="S_40/S_4004/S_4004", env=_F4,
        drive="8:opt=DamageTaken=0;20:move=0,1,200;150:sand;219:sand;300:sand;330:sand;331:quit",
        expect=["[sand] surf 18/18 water -2.00", "sub 1 buried 0 grounded 1 feet y -2.4", "sub 1 buried 0 grounded 1 feet y -2.2",
                "buried 1 grounded 1 feet y -3.45", "buried 1 grounded 1 feet y -3.90"]),
    # Ice (src/cleria/ice.hpp): standing on S_4002's ice ramp the player slides down it (FUN_0045a510
    # 0x45b053: knock channel 1 += 0.02 x the slope); walking off it she glides to a stop (k 0.35).
    "ice_slide": dict(
        room="S_40/S_4002/S_4002",
        drive="8:opt=DamageTaken=0;20:at3=-70.67,10,5.92,0;21:sand;35:pos;115:pos;120:move=0,1,8;121:pos;122:pos;128:pos;131:pos;134:pos;135:quit",
        expect=["[sand] surf 11/11", "feet (-70.795, 7.605, 5.920)", "feet (-71.511, 7.247, 5.920)",
                "feet (-71.564, 7.220, 5.969)", "feet (-71.573, 7.216, 6.049)", "feet (-71.627, 7.189, 6.780)",
                "feet (-71.654, 7.176, 6.960)", "feet (-71.680, 7.162, 7.010)"]),
    # The floor-4 boss (M360 Jenocres + its eight M980 crystals, build/ai_spec/M360.md / M980.md): S_4013's
    # entry event (Yunica) runs to its end and starts Battle_Yunica; the boss lays out its crystals; a
    # one-hit kill ends it, kills the crystals and (255 ticks later) clears flag 156 so the script goes on.
    "jenocres_fight": dict(
        room="S_40/S_4013/S_4013", char="1",
        drive="4:opt=DamageTaken=0;20:every=20,4300,ok;4380:actors;4390:opt=OneHitKill=1;"
              "4400:every=15,4700,near=40;4401:every=15,4700,atk;4700:actors;5000:dflags;5001:quit",
        expect=["[actor] id 168 cast 40 enemy hp 5869", "[actor] id 184 cast 184 enemy hp", "defeated 1",
                "M360 death section 5", "[actor] id 168 cast 40 enemy hp 0", "[actor] id 184 cast 184 enemy hp 0",
                "flags 130=0 131=1 133=0 156=0"]),
    # Floor 5: M430 Zeruena (build/ai_spec/M430.md) — inside 7 u the barrier (sub 0x70), at 8 u the summon:
    # 4 hands of cast 0x30 (M440) on Normal, which appear by the player and are gone again by t700.
    "zeruena_summon": dict(
        room="S_50/S_5009/S_5009",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:near=47,5;80:near=47,5;120:every=40,1000,near=47,8;"
              "700:actors;1000:quit",
        expect=["near cast 47 id 47 hp 413 flags 156=0 130=0 phase 2 sub 112", "phase 2 sub 102",
                "[actor] id 48 cast 48 enemy hp 0", "[actor] id 199 cast 48", "[actor] id 200 cast 48",
                "[actor] id 201 cast 48"]),
    # Floor 5 bullets: M640 Samachryce's kind-0x2a boomerangs (M640.md §7) and M660 Urobass's strike carriers.
    "floor5_shots": dict(
        room="S_50/S_5015/S_5015",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:near=68,5;60:every=50,1200,near=68,5;"
              "61:every=50,1200,bullets;1200:quit",
        expect=["[bullet] kind 42 effect 260"]),
    "urobass_strikes": dict(
        room="S_50/S_5010/S_5010",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:near=70,6;60:every=60,1500,near=70,6;"
              "61:every=60,1500,bullets;1500:quit",
        expect=["[bullet] kind 1 effect 494 age 4"]),
    # Issue #59: an engaged monster keeps the player on S_1004's slope (feet y 7 -> -2 over the run). The
    # player is re-placed only where the monster can see it (seen=), walks off, and the M170 Vilvan
    # never drops to phase 1 (CLERIA_LOS reports any engaged -> phase-1 drop with its blocking tris).
    "los_keep_slope": dict(
        room="S_10/S_1004/S_1004",
        env={"CLERIA_AI": "1", "CLERIA_LOS": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#17,4;60:every=200,1300,seen=#17,5;"
              "100:every=200,1300,move=1,0.5,40;160:every=200,1300,move=-0.5,1,40;1300:quit",
        expect=["seen #17 placed", "[ai] #17 cast 17 phase 2", "seen #17 placed feet (9.58, 0.00",
                "[ai] #17 cast 17 phase 2"],
        absent=["[los]"]),
    # Floor 6 (build/audit/FLOOR6_TOUR.md). M700 Fliogan: the brain-spawned dash effect 496 it owns and the
    # slash's 299 shots (M700.md).
    "floor6_fliogan": dict(
        room="S_60/S_6009/S_6009",
        env={"CLERIA_AI": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#74,4;60:every=200,1800,seen=#74,4;"
              "61:every=60,1800,bullets;1800:quit",
        expect=["496(owner 74", "[bullet] kind 2 effect 299"]),
    # M470 Levenueba: it moves at all (its 1.8 sphere over a 1.575 origin height), fans its 315 globs as
    # kind-0x29 lobs and kind-1 shots, and its grab holds the player in the mouth (M470.md §3.5.2).
    "floor6_levenueba": dict(
        room="S_60/S_6051/S_6051",
        env={"CLERIA_AI": "1"},
        drive="5:opt=InfiniteHP=1;20:seen=#193,6;900:quit",
        expect=["[hold] #193 holds the player"]),
    "floor6_levenueba_globs": dict(
        room="S_60/S_6051/S_6051",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#193,6;61:every=30,1500,bullets;1500:quit",
        expect=["[bullet] kind 41 effect 315"]),
    # S_6014, the mirror-floor room: M450's ring of tagged 252 orbs, M720's fan of 310s; M230's lobbed 272s.
    "floor6_piache_ring": dict(
        room="S_60/S_6014/S_6014",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#49,6;60:every=400,2000,seen=#49,6;"
              "61:every=30,2000,bullets;2000:quit",
        expect=["[bullet] kind 1 effect 252", "[bullet] kind 2 effect 310"]),
    "floor6_euchreas": dict(
        room="S_60/S_6014/S_6014",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#27,7;60:every=500,2000,seen=#27,7;"
              "61:every=30,2000,bullets;2000:quit",
        expect=["[bullet] kind 2 effect 272"]),
    # Khonsclard (boss04, build/boss_re/KHONSCLARD.md): the intro (START), flag 156, two pods rise; breaking
    # them downs the boss and opens the core (jump attacks); every 20 % adds a pod; the kill writes 223 and
    # the EXP, and the 1/340 fade destroys the boss ~339 frames later. Level 50 and Damage x10 keep it short.
    "khonsclard_fight": dict(
        room="S_40/S_4099/S_4099",
        env={"CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=10;20:at=-2.8,23.3,0;"
              "40:every=20,700,ok;720:every=40,3700,boss4;728:every=40,3700,atk;3700:quit",
        expect=["[boss04] Khonsclard: body 3000, pods 600", "clip START fr", "f156 1", "states 00", "down 1 ",
                "band 2", "band 5", "flag 223 = 1", "EXP 328", "destroyed"]),
    # Pictimos (PICTIMOS.md): walk into BATTLE.XSO's event box, the 700-frame intro (RAKKATYUU / TYAKUTI), the
    # fight from BATTLE_START, ZOROMEKA's 27 Chryolos below half HP (m220_boss), the guts at HP 0 with live
    # summons (survives at 1, absorbs them), the death: EXP, flag 224 at b4 500 and the destroy.
    "pictimos_fight": dict(
        room="S_50/S_5099/S_5099",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=2;8:tier=10;10:move=0,1,200;"
              "40:every=20,1000,ok;1030:every=40,4100,boss5;1038:every=40,4100,atk;4100:quit",
        expect=["[boss05] Pictimos: body 4000, STR 220 lv 40", "sect RAKKATYUU", "sect TYAKUTI", "f156 1",
                "sect ZOROMEKA", "/27 |", "sect DEAD", "guts 1 waves", "EXP (lv 40)", "flag 224 = 1", "destroyed"]),
    # Zava (build/boss_re/ZAVA*.md; the heads src/cleria/boss06, herself brains/m870). The room's own retry start:
    # flag 205 = 1 and a reload make the root script place the player, Zava and the heads and run Battle_Yunica
    # (flags 130 / 156, then it waits for 225); the first-visit intro before SUMMON is some 7,000 frames of dialogue.
    # The fight is played the way a player plays it: drive `boss6` (view_boss06.inl) only ever stands the player on
    # the arena floor and presses Jump / Attack / the stick / Skill, so every hit goes through the input path. What
    # a save at this point has and the direct room start lacks is set by hand: flag 163 (the double jump), the
    # level / weapon tier; DamageTaken=0 + InfiniteHP keep the run alive, DamageDealt scales the length.
    # Phase 1: she hovers at origin y 13 (out of a single jump's reach); under her in the hover, jump, the second
    # jump, the air combo: the first damaging hit in the air knocks her down (Normal), she lies and is hit, gets up.
    # Phase 2 under 70 %: the flight onto a head (BossCam 3 / 4, moves 9 / 10), four orbs; her hit ticks while riding
    # count (Z+0x7a8), each threshold kills an orb and swaps the heads (move 12), the last breaks the fusion (move
    # 8): she is thrown off and lies hittable. Then the floor cast (action 8): the other head dives (move 11, the
    # ice floor of head a) for 600 frames; the next mount, the second fusion break, the kill (flag 225, the EXP) and
    # the heads' death (move 15).
    "zava_fight": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.5;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss6;4400:quit",
        expect=["[boss06] Zava's heads: a (-39.46,6.10,30.00) b (-28.82,6.10,30.00), HP 5000 STR 232 DEF 320",
                "BossCam 1", "Zava phase 1 action 1 hp 6000/6000", "jump under Zava (act 4)", "second jump",
                "air attack", "y 13.00, falling, hits left 0", "Zava phase 1 action 11", "y 6.83, grounded",
                "Zava phase 1 action 0", "Zava phase 2 action 0 hp 3878/6000", "BossCam 3", "move 9", "BossCam 4",
                "move 10", "riding, hits left 0, ride count 1, lives 0/4", "head b move 12 (pattern 2, lives 3)",
                "lives 0/2", "lives 0/1", "head b move 8 (pattern 2, lives 0)", "Zava is thrown off head b",
                "grounded, hits left 0, ride count 1, lives 0/0", "Zava phase 2 action 7", "Zava phase 2 action 8",
                "head a move 11", "Zava phase 2 action 12", "Zava phase 2 action 0", "head a move 9",
                "riding, hits left 4, ride count 1, lives 4/0", "Zava is thrown off head a", "BossCam 6", "EXP (lv 45)",
                "flag 225 = 1", "move 15", "head b destroyed", "head a destroyed"],
        absent=["direct damage ATK 267"]),
    # ... under 25 % at her re-position the big attack instead (action 20, both heads dive = move 17, the pull to
    # the centre, the rings, the blast at frame 360): the pilot runs to the arena's edge while she casts, then holds
    # the stick outward and keeps jumping (the pull is 0.3 grounded, 0.1 airborne), and the blast's direct hit (within
    # 2.0 of the centre) does not land. A head's bite on the way: mashing Attack ends the hold (8 press ticks).
    "zava_big_attack": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.6;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss6;3800:quit",
        expect=["Zava phase 2 action 0", "Zava is thrown off head b", "Zava phase 2 action 7", "Zava phase 2 action 20",
                "head a move 17", "head b move 17", "Zava phase 2 action 12", "Zava phase 2 action 0", "head a move 9",
                "holds the player", "lets the player go", "Zava is thrown off head a", "flag 225 = 1"],
        absent=["direct damage ATK 267"]),
    # Nightmare (drive diff=4: Zava 7500 HP, the heads' DEF 325): the fourth damaging hit in the air knocks her down
    # (Z+0x8a8 = 4, ZAVA_AB B.3), here inside the lightning cast (action 8, she holds her place); knocked out of the
    # cast before its end the forcing timer Z+0x7c4 is still 0, so her next choice is lightning again (B.4).
    "zava_nightmare_knockdown": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:diff=4;5:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.8;8:tier=10;9:flag=205,1;"
              "10:saveload;11:flag=163,1;60:boss6=lightning;360:quit",
        expect=["HP 5000 STR 232 DEF 325, level 4", "Zava phase 1 action 1 hp 7500/7500", "Zava phase 1 action 8",
                "jump under Zava (act 8)", "flying, hits left 3", "flying, hits left 2", "flying, hits left 1",
                "falling, hits left 0", "Zava phase 1 action 11", "y 6.83, grounded", "Zava phase 1 action 0",
                "flying, hits left 4", "Zava phase 1 action 4", "Zava phase 1 action 8"]),
    # Without the double jump (flag 163 unset) no jump attack reaches her: a single jump tops at feet y 8.62.
    "zava_single_jump": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;8:tier=10;9:flag=205,1;10:saveload;60:boss6;300:quit",
        expect=["jump under Zava (act 4)", "second jump", "Zava phase 1 action 8", "jump under Zava (act 8)"],
        absent=["Zava took a hit", "air attack"]),
    # Yunica's Thunder (bracelet 117, section 0x10c: a rising slash) from the second jump. Cast within 4.0 of her
    # while her dodge is armed it sets off the dash (action 7, B.1 (3)); the next cast finds her cornered at the
    # arena's z edge (the dash's probe fails, B.6: back to action 1 each frame) and knocks her down.
    "zava_thunder": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.5;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;12:bracelet=117;13:flag=183,1;60:boss6=thunder;300:quit",
        expect=["jump under Zava (act 4)", "air skill", "Zava phase 1 action 7", "Zava phase 1 action 1", "air skill",
                "y 13.00, falling, hits left 0", "Zava phase 1 action 11"],
        absent=["air attack"]),
})

# Room transit (build/warp_re/TRANSIT.md): the door trigger FUN_0042cf00 and its fade machine FUN_004106e0,
# the cutscene skip's walks (ROUTE_F13.md D3), O2 through a load and a Retry (D5).
CASES.update({
    # S_1004 -> S_1005, then straight back into S_1005's door 1 (41 ticks after arriving: the old hand-set
    # 1.5 s cooldown + edge trigger swallowed this and needed a walk out and back in). The trigger is level-
    # triggered; it fades to black at 0.05 a step and commits on the 20th step (t61 -> t81), then the arrival
    # fades in over 20 steps (1.00 at t82, 0.05 at t101, clear at t102).
    "door_reenter": dict(
        room="S_10/S_1004/S_1004",
        drive="20:exit=0;40:move=-1,0,200;71:fader;82:fader;101:fader;102:fader;130:quit",
        expect=[">> entering S_1005", "[door] t61 S_1005 -> S_1004 (fade out)", "t71 fader",
                "fader 0.45 -> 1.00 at 0.050 (door pending) (latched)", "[door] t81 commit S_1004 (fade 1.00)",
                ">> entering S_1004", "arrival @ (65.0,6.0,-19.0) [SEN]", "t82 fader", "fader 1.00 -> 0.00 at 0.050",
                "t101 fader", "fader 0.05 -> 0.00", "t102 fader", "fader 0.00 -> 0.00"]),
    # #51: every scene entry resets the fader and fades in from black (FUN_00431260 -> FUN_00431840 ->
    # FUN_00439170), whatever fader (colour, rate) the old room left. Held white, then a door (exit=), a
    # same-room load (saveload: the "load" scene load) and a script ChangeScene (@UseWing -> S_1009) each
    # arrive at 1.00 -> 0.00, 0.05 a step, black, and clear 20 steps later.
    "fader_scene_entry": dict(
        room="S_10/S_1004/S_1004",
        drive="20:blackout=ffffff;21:fader;30:exit=0;31:fader;52:fader;60:blackout=ffffff;61:saveload;62:fader;"
              "83:fader;90:blackout=ffffff;91:flag=137,1;92:run=@UseWing;330:fader;340:quit",
        expect=["t21 fader", "fader 1.00 -> 1.00 at 1.000 rgb ffffff", ">> entering S_1005", "t31 fader",
                "fader 1.00 -> 0.00 at 0.050 rgb 000000", "t52 fader", "fader 0.00 -> 0.00", "[drive] loaded S_1005",
                "t62 fader", "fader 1.00 -> 0.00 at 0.050 rgb 000000", "t83 fader", "fader 0.00 -> 0.00",
                "ChangeScene 's_10\\s_1009' -> S_1009", ">> entering S_1009", "t330 fader",
                "fader 0.00 -> 0.00 at 0.050 rgb 000000"]),
    # No door during a boss fight (flag 156 = 0x76bb8c, FUN_0042cf00's step gate): standing deep inside the
    # volume does nothing; the tick the flag clears, the level trigger fires without leaving the box.
    "door_boss_gate": dict(
        room="S_10/S_1004/S_1004",
        drive="20:exit=0;25:flag=156,1;30:move=-1,0,60;120:pos;140:flag=156,0;180:quit",
        expect=[">> entering S_1005", "t120 pos", "[door] t140 S_1005 -> S_1004 (fade out)",
                "[door] t160 commit S_1004", ">> entering S_1004"]),
    # The Double-press skip across SwordTutorial1's Wait_ChrMoving (pc 617): the walks run in skip time,
    # so the scene drains to its end (274 = 1, the world thaws) instead of stopping there.
    "access_skip_walks": dict(
        room="S_10/S_1001/S_1001",
        drive="5:flag=331,1;8:opt=CutsceneSkip=1;10:exit=1;60:esctap;70:esctap;71:scripts;72:flagdump;80:quit",
        expect=[">> entering S_1000", "[access] skip prompt: press again", "[access] cutscene skip (double)",
                "t71 scripts", "frozen 0 fix 0/0", " 274=1 "]),
    # OK keeps advancing the windows while the skip prompt is up: "Ah..." (Wait_Prompt pc 181) is accepted
    # by a real OK press at t335.. inside the prompt's 120-tick window, and no skip happens.
    "access_skip_ok": dict(
        room="S_10/S_1001/S_1001",
        drive="5:flag=331,1;8:opt=CutsceneSkip=1;10:exit=1;331:esctap;332:scripts;335:every=6,440,oktap;"
              "400:scripts;401:quit",
        expect=["[access] skip prompt: press again", "t332 scripts", "script SwordTutorial1 pc 181",
                "t400 scripts", "script SwordTutorial1 pc 203"],
        absent=["cutscene skip"]),
    # A content load (cload) restores O2 0x76c1b8 from the record (5710, saved at t30; 5700 after the
    # load tick's drain) and takes the continue snapshot, so a
    # Retry under water returns to the loaded point with the loaded O2 (it used to fall back to the respawn
    # stand-in, and O2 was never touched).
    "o2_load_retry": dict(
        room="S_20/S_2008/S_2008",
        drive="30:csave=3;150:pos;151:cload=3;152:pos;200:hp=0;360:ok;361:pos;400:quit",
        expect=["t150 pos", "O2 4510", "[cont] snapshot S_2008", "t152 pos", "O2 5700", "[gameover] chose retry",
                "Retry -> S_2008 (0.74, 0.00, -7.78)", "t361 pos", "O2 5710"],
        absent=["no continue snapshot"]),
})

# The first story playtest (build/audit/PLAYTEST_1_STORY.md).
CASES.update({
    # §1: S_1006's s10wall_00 (SOB word 0x1800 -> +0x298 bit 2) stops the walk north without the Mask of Eyes;
    # with it (acc=54 -> 0x76bbac) the hull leaves the collision at once, the wall fades out over 60 updates
    # (FUN_00503190) and the walk reaches the S_1012 door behind it.
    # PLAYTEST_1.md §6: the room's enemies go first (15:killall): S_1006's cast-183 #174 walks up and hits
    # her around t147, so t150 measured its knock, not the wall. Was 4.431 / 4.530 alternating each tick
    # (resolve_walls double-pushed the wall's stacked quads); the contact is now a steady 4.550 (5 - 0.45).
    "mask_of_eyes_wall": dict(
        room="S_10/S_1006/S_1006",
        drive="10:mask;15:killall;20:at=24,0,1;25:move=0,1,120;150:pos;160:acc=54;161:mask;190:mask;230:mask;"
              "231:at=24,0,1;235:move=0,1,120;400:quit",
        expect=["maskobj 1 bits 2 hidden 0 alpha 1.0000 coll on", "feet (24.275, 0.000, 4.550)",
                "maskobj 1 bits 2 hidden 1 alpha 0.9833 coll off", "maskobj 1 bits 2 hidden 1 alpha 0.5000 coll off",
                "maskobj 1 bits 2 hidden 1 alpha 0.0000 coll off", "S_1006 -> S_1012 (fade out)", ">> entering S_1012"]),
    # §2: S_1014's Roo (TalkC430): "<roo>My hunger knows no bounds..." lays out as extfont cells (M 24, y 36,
    # h 19, u 32, n 25, g 18, e 16, r 29 — FUN_005f03f0's tag-15 map); the '.' stay letters.
    "roo_alphabet": dict(
        room="S_10/S_1014/S_1014",
        drive="20:run=TalkC430;100:wndtext;101:quit",
        expect=["run TalkC430 -> started", "wndtext alive 1", "glyphs 49 ext 44 cells 24 36 19 32 25 18 16 29"]),
})
# §3: the whole Yunica opening from the New-Game router (S_0000 -> S_0002 Prologue_Event_0/1 -> S_0001
# Prologue_Event_2 -> S_1000 BattleStart1 / battle_0) skipped in each mode, pressing only inside a cutscene
# (`skiptap` / `skiphold` act while a Fix holds; `killall` ends the intro's waves). Played out with OK mashed
# it takes ~24,100 ticks (6.7 min), almost all of it the scripts' own Wait_Time (12,210 ticks in Prologue_Event_2
# alone); skipped, under 900. Before 83a05fb the drain stopped at the first Wait_ChrMoving (BattleStart1 pc 155,
# Prologue_Event_2 pc 670) and the rest played at normal speed.
_OPEN = "40:every=50,{end},killall;{end}:scripts;{q}:quit"
for _m, _v, _press, _end in (("double", 1, "20:every=20,{end},skiptap", 1200), ("single", 0, "20:every=20,{end},skiptap", 1200),
                             ("hold", 2, "20:every=100,{end},skiphold=70", 1500)):
    CASES["opening_skip_" + _m] = dict(
        room="S_00/S_0000/S_0000",
        drive=f"5:opt=CutsceneSkip={_v};" + _press.format(end=_end) + ";" + _OPEN.format(end=_end, q=_end + 1),
        expect=[">> entering S_0002", ">> entering S_0001", "cutscene skip (" + _m + ")", ">> entering S_1000",
                f"t{_end} scripts", "frozen 0 fix 0/0", "script s_Stop1000_03"],
        absent=["script battle_0", "script BattleStart1", "script Prologue_Event"])
# §3 (second report, Hugo, Hold mode): the skip button held from New Game on. Hold now counts any hold, so a
# held button skips every scene of the chain one second in, and the shell's skip ends the three movies too
# (they decode here: CLERIA_MOVIE=1). Before, only the first scene skipped and Prologue_Event_2 played out.
for _c, _bs in (("1", "BattleStart1"), ("2", "BattleStart2")):
    CASES["opening_hold_through_" + _c] = dict(
        room="S_00/S_0000/S_0000", char=_c, env=dict(CLERIA_MOVIE="1"),
        drive="1:opt=CutsceneSkip=2;1:eschold=1500;40:every=50,1600,killall;1600:scripts;1601:quit",
        expect=["demo1 skipped", "cutscene skip (hold)", "demo2 skipped", ">> entering S_0002", "cutscene skip (hold)",
                "demo3 skipped", ">> entering S_1000", "cutscene skip (hold)", "t1600 scripts", "frozen 0 fix 0/0",
                "script s_Stop1000_03"],
        absent=["script battle_", "script " + _bs, "script Prologue_Event", "ended at"])


# Playtest 1, effects (build/audit/PLAYTEST_1_FX.md).
CASES.update({
    # 1. A damaging hit sets the monster's hurt timer +0x238 = 1.65 (FUN_00478510) on its next tick, the
    # integrator runs it down by 0.075 (FUN_0045a3a0): 1.575 one tick later, 0 after 22 ticks. While it
    # runs the sprite gets the 0x6b3713 specular add (no white flash).
    "fx_hurt_tint": dict(
        room="S_10/S_1002/S_1002", env=dict(_LOCK, CLERIA_SFXLOG="1"),
        drive="20:near=17;21:atk;28:actors;60:actors;61:quit",
        expect=["[hitfx] t27 spark 0x7f", "[actor] id 17 cast 17 enemy hp 72", "hurt 1.575",
                "[actor] id 17 cast 17 enemy hp 72", "hurt 0.000"]),
    # 5. Boost entry (0x465bea..0x465ca6): adol_S_mode_f attached half a unit toward the eye (camera yaw 0
    # here: -z); the aura (FUN_00463f40) attached at the origin with its depth bias 2 (emitter 0 B2+0x0c).
    "fx_boost_attach": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="10:flag=187,1;12:boost=30000;20:boostkey;21:fxall;30:quit",
        expect=["[fxall] id 50 attach 2 at", "rel (0.00, 0.00, -0.50) bias 0.00 #boost_f",
                "[fxall] id 39 attach 2 at", "rel (0.00, 0.00, 0.00) bias 2.00 #boost_aura"]),
    # 2/3. Drops draw as camera-plane quads at pos + (0, 0.1, 0) - F 0.15 + V 0.075 (FUN_00586bf0), their
    # shadow at the ground (hit y + 0.02) - F 0.15, not in the floor; collecting the recovery drop (0x42) puts
    # cure.eft on the player at (0, -1, 0), attached, depth bias 5, and z_item.eft at each pickup.
    "fx_pickup_place": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="8:opt=DamageDealt=10;9:opt=DropRate=10;20:near=17;21:atk;45:near=17;46:atk;70:near=17;71:atk;"
              "95:near=17;96:atk;120:pickups;153:fxall;160:quit",
        expect=["[pickup] id 66 pos (32.35, -4.10, -38.08) ground -4.078 rest 1", "sprite (32.35, -3.86, -38.18)",
                "shadow y -4.007", "[fxall] id 63 attach 2 at", "rel (0.00, -1.00, 0.00) bias 5.00",
                "[fxall] id 72 attach 0 at (32.35, -4.10, -38.08)"]),
    # 4. Eft_LocateChr (0x118) under a Fix: the attach reads the actor's current +0x20, so the smith
    # script's player light follows the player the opening moved (it sat at the world origin).
    "fx_locate_under_fix": dict(
        room="S_10/S_1000/S_1000", env=_LOCK,
        drive="20:run=TalkC260_Weapon_Sub;60:rep=30,40,ok;699:pos;700:fx;710:quit",
        expect=["feet (0.000, -2.997, -23.557)", "'Player_Light' id 50 alpha 1.000 -> 1.000 attach 1 at (0.00, -1.92, -23.56)",
                "'Player_Light2' id 63 alpha 1.000 -> 1.000 attach 1 at (0.00, -1.92, -23.56)"]),
})

# The Archipelago cases, played by the Lua mod samples/mods/archipelago_lua against a local scripted AP
# server (tools/aplua_regress.py, LUA_API.md §9).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aplua_regress  # noqa: E402
CASES.update(aplua_regress.CASES)
# The .cleriamod install cases: a mod file on the command line (tools/modfile_regress.py, MODFILE.md).
import modfile_regress  # noqa: E402
CASES.update(modfile_regress.CASES)


def content_setup(case, env, tmp):
    """Mod content cases (build/shell_re/CONTENT.md): a scratch settings folder with the case's mods
    installed and turned on. `mods`: sample folders under samples/mods; `content`: {file: text} of a
    generated test mod "test.regress"; `mods_off`: installed but turned off."""
    import shutil
    mods = os.path.join(tmp, "mods")
    os.makedirs(mods, exist_ok=True)
    ids = []
    for m in case.get("mods", []):
        shutil.copytree(os.path.join(ROOT, "samples", "mods", m), os.path.join(mods, m))
        ids.append("sample." + m)
    if case.get("content"):
        d = os.path.join(mods, "regress")
        os.makedirs(os.path.join(d, "content"))
        open(os.path.join(d, "mod.ini"), "w").write("[mod]\nId=test.regress\nApiVersion=2\n")
        for f, t in case["content"].items():
            open(os.path.join(d, "content", f), "w").write(t)
        ids.append("test.regress")
    on = [] if case.get("mods_off") else ids
    open(os.path.join(tmp, "cleria.ini"), "w").write(
        '[cleria.mods]\nUseMods=1\nOrder="%s"\nActive="%s"\n' % (",".join(ids), ",".join(on)))
    open(os.path.join(tmp, "yso_win.ini"), "w").close()
    env["CLERIA_INI"] = os.path.join(tmp, "yso_win.ini")


def run(name, case, exe, assets):
    with tempfile.TemporaryDirectory() as saves:
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR=case.get("char", "1"), CLERIA_DRIVE=case["drive"],
                   CLERIA_SAVE_DIR=saves)
        env.update(case.get("env", {}))            # a case may override any of these (CLERIA_CHAR)
        env.setdefault("CLERIA_SHOT_DIR", saves)   # a case's `shot` / `shell` frames stay in its temp folder
        if "mods" in case or "content" in case:
            content_setup(case, env, os.path.join(saves, "settings"))
        srv = aplua_regress.start(case, env, os.path.join(saves, "settings"))   # an `aplua` case: its server, the mod
        extra = modfile_regress.start(case, env, os.path.join(saves, "settings"))   # a `modfile` case: the file
        sob = os.path.join(assets, "MAP", case["room"] + ".SOB")
        r = subprocess.run([exe, assets, sob] + extra, env=env, capture_output=True, text=True, timeout=900)
    out = r.stdout + r.stderr
    if not aplua_regress.finish(name, case, srv):
        return False
    for e in case.get("absent", []):
        if e in out:
            print(f"FAIL {name}: unexpected '{e}'")
            return False
    at = 0
    for e in case["expect"]:
        k = out.find(e, at)
        if k < 0:
            print(f"FAIL {name}: missing '{e}' (after offset {at})")
            # Say how the run ended, so a flake shows its cause (a crash, a GPU loss, an early quit).
            tail = [l for l in r.stderr.splitlines() if l.strip()][-3:]
            print(f"     exit code {r.returncode}; last stdout line: {(r.stdout.splitlines() or [''])[-1][:160]!r}")
            for l in tail:
                print(f"     stderr: {l[:160]}")
            return False
        at = k + len(e)
    print(f"ok   {name}")
    return True


def main():
    args = sys.argv[1:]
    opt = {"--exe": EXE, "--assets": ASSETS}
    for k in opt:
        if k in args:
            i = args.index(k)
            opt[k] = os.path.abspath(args[i + 1])
            del args[i:i + 2]
    names = args or list(CASES)
    fails = sum(not run(n, CASES[n], opt["--exe"], opt["--assets"]) for n in names)
    print(f"{len(names) - fails}/{len(names)} passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
