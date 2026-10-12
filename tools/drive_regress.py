#!/usr/bin/env python3
"""CLERIA_DRIVE regressions for cleria-view: scripted live-game runs checked by their stdout.

Each case launches cleria-view on a room with a CLERIA_DRIVE command list (see the
CLERIA_DRIVE comment in apps/cleria-view/view_game.inl) and requires every `expect`
substring to appear in the output, in order. Saves go to a scratch directory
(CLERIA_SAVE_DIR), never ./saves.

    python tools/drive_regress.py                 # all cases
    python tools/drive_regress.py statue_swap     # one case
    python tools/drive_regress.py --jobs 8        # cases side by side (the default: half the CPUs, at most 8;
                                                  # --jobs 1 = one after the other)
    python tools/drive_regress.py --smoke         # the quick tier (SMOKE below), for a per-commit check
    python tools/drive_regress.py --exe build_diff/Release/cleria-view.exe --assets <dir>
"""
import concurrent.futures, os, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "Release", "cleria-view.exe")
ASSETS = os.path.join(ROOT, "assets")

# The S_1009 statue cleanse (Yunica, flags 310 visited / 84 the Crystal), confirmed through
# the narration box: SAVE.XSO's 311 = 1 must reach its s_SaveObjectChange child.
CLEANSE = "5:flag=310,1;6:flag=84,1;10:at=-0.036,8.2,0;20:act;60:ok;80:ok;100:ok;120:ok;140:ok;160:ok"

# S_2015 -> S_2010 by its door, then the swim to the S_2011 door (issue #47).
# (36:killall since ENEMY_WATER.md W1 / W2: in the flooded rooms the monsters are submerged like every actor, the
# Kelemare's own water launches work, and they now swim up to the swimmer and knock him off these routes.)
_S2010_ENTRY = ("5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;10:exit=1;36:killall;40:move=0,1,75;120:move=-1,-0.4,75;"
                "210:every=12,600,jump;400:move=-1,0.3,150;560:move=-0.2,1,600;1240:quit")

def record_file(dwords):
    """A record.* file (build/mode_re/CLEAR_DATA.md §1) holding `dwords`: the header, the key, crc32 of the plain
    payload, and the additive cipher over crc + payload."""
    import struct, zlib
    payload = struct.pack("<%dI" % len(dwords), *dwords)
    body = bytearray(struct.pack("<I", zlib.crc32(payload)) + payload)
    key = 0x1234ABCD
    for i in range(len(body)):
        key = key * 0x3D09 & 0xFFFFFFFF
        body[i] = body[i] + (key >> 16) & 0xFF
    return (struct.pack("<4I", 0x004F7359, 0xD, 0, 0) + bytes([0x8B, 0x10, 0xCF, 0x78, 0x18, 0x56]) + bytes(10) +
            struct.pack("<I", 0x1234ABCD) + bytes(body))


def clear_data(clear=(1, 1, 1), tiers=(), shop=()):
    """record.ta2's 0x26 dwords (CLEAR_DATA.md §3.1): the clear bytes of Yunica / Hugo / Toal, cleared tiers as
    (row 0 Very Easy .. 4 Nightmare, column 0 Yunica .. 2 Toal), bought shop items by index (0 Adol Felghana,
    1 Adol Napishtim, 2-4 the Extras, 5-10 Arena Stage 2-7, 11 the School Outfit)."""
    d = [0] * 0x26
    d[0:3] = clear
    for row, col in tiers:
        d[3 + row * 3 + col] = 1
    for i in shop:
        d[0x16 + i] = 1
    return d


def record_table(rows, times):
    """A best-time table's dwords (CLEAR_DATA.md §4; `rows` 15 = record.ta / .tae, 7 = record.ar / .are): 16 blocks
    of `rows` entries {ms, 0xFFFFFFFF, 0xFF | mark << 16}; `times` = {(character, d, row): (ms, mark)} with
    character 0 Adol .. 3 Toal and d 0 Easy .. 3 Nightmare."""
    d = [0, 0xFFFFFFFF, 0x00FF00FF] * (16 * rows)
    for (c, dif, row), (ms, mark) in times.items():
        e = ((c * 4 + dif) * rows + row) * 3
        d[e] = ms
        d[e + 2] = 0xFF | mark << 16
    return d


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
    # re-enters the snapshot's room at its pose (the room root's SaveContData). Issue #77: the death section 5
    # plays to its last frame and stays there (anime_Wait(-1)) — Yunica's fall is tag 307 (her form flag 149),
    # frames 0..9.
    "gameover_retry": dict(
        room="S_10/S_1009/S_1009",
        drive="10:move=1,0,40;70:hp=0;220:ok;260:pos;300:quit",
        expect=["[cont] snapshot S_1009 (0.05, 2.00, 7.80)", "[death] HP 0", "[death] t+30 section 5 tag 307 frame 4",
                "[death] t+90 section 5 tag 307 frame 9", "[gameover] gameover.dds",
                "[gameover] chose retry", "Retry -> S_1009 (0.05, 2.00, 7.80), retries 1",
                "feet (0.055, 2.000, 7.800)"]),
    # ... Hugo's (Pattern(5, 0..11)) and Toal's (Pattern(5, 0..9)): lying on the last frame when the card comes.
    "death_fall_hugo": dict(
        room="S_10/S_1009/S_1009", char="2", drive="70:hp=0;180:quit",
        expect=["[death] HP 0", "[death] t+30 section 5 tag 5 frame 5", "[death] t+90 section 5 tag 5 frame 11",
                "[gameover] gameover.dds"]),
    "death_fall_toal": dict(
        room="S_10/S_1009/S_1009", char="3", drive="70:hp=0;180:quit",
        expect=["[death] HP 0", "[death] t+30 section 5 tag 5 frame 4", "[death] t+90 section 5 tag 5 frame 9",
                "[gameover] gameover.dds"]),
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
    # box, the stun part broken twice, then standing in the arena. The bubble spray hits (EF2 323), the run drops
    # M890 Spawns (the boss level, the chaser first), the laser's terrain-ray bursts hit (EF2 326), the inhale
    # swallows Spawns and the bite (part 2) lands. (The taunt is velagunder_taunt below: since e44f09f the end of
    # a stun no longer leaves her standing on the body.)
    # Re-pinned 2026-10-11 (fx+0x201 read as the bytes have it: the beam's particles trace the room without the boss
    # meshes instead of never meeting the world, issue #66). On the full-log diff the run is the same up to t4207,
    # where a beam particle's own world contact now lands a hit (logged as fx 326 with the beam's power 4.0, rtype 2: 4 damage, a knock-back)
    # in place of two of the burst's ticks; she stands elsewhere for the inhale, so two Spawns are swallowed on one
    # tick (t4537: both lines say "2 so far") and the five are eaten in another order. The bite still lands at t4654.
    "velagunder_attacks": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="15", CLERIA_BOSSLOG="1", CLERIA_DMG="1"),
        drive="10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 1801, 40))
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2000, 3201, 12)) + "3300:at=0,19,180;5500:quit",
        expect=["[boss] Velagunder: HP 700 STR 70 DEF 53 lv 8", "[boss] 31 body entries (820 tris)",
                "fx 323 (atk 70 power 0.0 knock 5.00",
                "M890 #175 spawned", "hp 58 lv 8 mode 1", "fx 326 (atk 70 power 0.0 knock 1.00 rtype 1",
                "fx 326 (atk 70 power 4.0 knock 1.00 rtype 2", "eaten (2 so far)", "eaten (5 so far)",
                "velagunder part 2 (STR 70 power 14.0 rtype 2"]),
    # The taunt (VELAGUNDER.md C10, §5.8: state 8, ACTION_01 — the player grounded on surface 5 (his wood body)
    # with the floor above 4.0, out of his reach). After the first stun is over (t2610: state 0, WAIT) Yunica is
    # put on the top of his right shoulder, on the hull as it is posed now (`at3`: the floor below (2.5, 14, 33),
    # y 11.8), and stays there: the taunt starts within 60 ticks and repeats while she stands on him.
    "velagunder_taunt": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="15", CLERIA_BOSSLOG="1", CLERIA_DMG="1"),
        drive="10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 1801, 40))
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2000, 2541, 12))
              + "2620:at3=2.5,13,33,0;2625:pos;2800:pos;2900:quit",
        # Re-pinned 2026-10-10, twice. The dust puffs (20 draws at every hit the player takes move Velagunder's
        # stream) put the stun 25 ticks later and the placement at t2641; with the blood splats (18 more draws at each
        # of those hits) the stun is where it was before both, and so are the placement (t2620) and the two stands.
        expect=["stun 0 STUNNED", "state 0 sub 1", "t2620 at3=2.5,13,33,0", "feet (3.643, 11.783, 33.000)",
                "state 8 sub 1", "clip ACTION_01", "feet (4.910, 10.521, 32.095)", "state 8 sub 1"]),
    # ... and the kill (Lv 60): the death timeline's end block sets flag 220 and awards the EXP, which releases
    # BATTLE_0.XSO into SENTOUGO.XSO (the stairs rise: stepup_se).
    "velagunder_kill": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="60"),
        drive="10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 1801, 40))
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2000, 3000, 12)) + "3000:at=0,22,180;3500:scripts;"
              + "".join(f"{t}:ok;" for t in range(3540, 3701, 40)) + "3740:scripts;3750:quit",
        expect=["flag 220 = 1", "EXP 86 (lv 8)", "[content] boss_defeated S_1099 flag 220", "script sentougo", "script stepup_se"],
        count={"boss_defeated": 1}),
    # Hugo against Velagunder (VELAGUNDER.md §3.1 / §3.3, EFFECTHIT.md §5.1). His intro S_EVT1099_2 has more pages
    # than Yunica's: the `ok` presses run to t2200 (with the cases' 1800 above a page stays open, the world stays
    # frozen and no shot leaves). From the arena floor at (0, 21), turning to the head (`bossaim`: no move), his
    # shots (EF2 130, side 0) drain the stun part through FUN_004faf20 — with its white number, FUN_004fae00 —
    # until the gauge breaks (t3000). Then from the top of the body (`at3`: the floor below (2.5, 13, 33), 2.6
    # from the core; a player gets there over the right hand's thumb, the arm and the shoulder) they damage the
    # core. A shot dies on any part it touches (the armoured ones for 0), on the hull and in a bubble.
    # Re-pinned 2026-10-10 (FIX_SLOTS.md S3): the intro's scripted hop back (t1060..1117, under its Fix(0)) used to be
    # held on the floor by the frozen snap with the jump still pending, and resumed when the Fix ended — Hugo rose to
    # y 3.10 at t1950 for no reason. He now lands inside the Fix (t1117) and stands at the thaw, so his first shots
    # leave a tick apart from before and the stun breaks 30 ticks later with the body 7 further left: the spot on
    # its top moved from (2.5, 13, 33) to (-4.8, 13, 29.6), the same offset from the core.
    # Re-pinned 2026-10-10 (the dust puffs: 20 draws at every hit the player takes): the stun breaks 60 ticks earlier
    # with the body back in the middle (core (0.10, 10.88, 31.42) at t3210, was (-7.14, 10.78, 27.56)); the spot on
    # its top is (2.5, 13, 33.4), the same offset from the core again.
    "velagunder_hugo": dict(
        room="S_10/S_1099/S_1099", char="2", env=dict(CLERIA_LEVEL="15", CLERIA_BOSSLOG="1", CLERIA_DMG="1"),
        drive="10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 2200, 40)) + "2300:at=0,21,0;"
              + "".join(f"{t}:bossaim;{t + 1}:atk;" for t in range(2302, 3100, 6)) + "3200:at3=2.5,13,33.4,0;"
              + "".join(f"{t}:bossaim;{t + 1}:atk;" for t in range(3205, 3500, 6)) + "3500:quit",
        expect=["[boss] Velagunder: HP 700 STR 70 DEF 53 lv 8", " ffffff\n  fx 130 hits boss part 1 (DEF 53)",
                "stun 0 STUNNED", "t3200 at3=2.5,13,33.4,0", " ffffff\n  fx 130 hits boss part 0 (DEF 53)"]),
    # The stunned Velagunder is climbed on foot (e44f09f: his collision twin is bound to the render bones, and
    # his body has no slope limit while stunned), Toal on Hard: the stun part broken from the floor, then the
    # walk (`goto`) up the hand, the arm and the shoulder to the core, where the sword hits part 0.
    "velagunder_climb_toal": dict(
        room="S_10/S_1099/S_1099", char="3", env=dict(CLERIA_LEVEL="60", CLERIA_BOSSLOG="1", CLERIA_DMG="1"),
        drive="5:diff=3;10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 2200, 40))
              + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2300, 2422, 12)) + "2426:level=9;2429:pos;"
              "2434:goto=-6.6,23.3,80;2584:goto=-6.8,28.5,130;2624:jump;2724:goto=-6.2,30.6,40;2769:goto=-5.6,31.6,40;"
              "2814:goto=-2.0,32.8,300;2884:pos;2885:goto=-1.2,31.5,60;2924:goto=0.3,30.6,4;"
              + "".join(f"{t}:atk;" for t in range(2954, 3124, 14)) + "3134:pos;3224:quit",
        # Re-pinned 2026-10-10 (the dust puffs: 20 draws at every hit the player takes move Velagunder's stream): he
        # meets the swings in another pose and two of the nine miss the stun part (36 of 700 left), so the pilot swings
        # a tenth time (t2409) and everything after it is 12 ticks later. The walk itself is the same — both `goto`
        # ends and the stand on the shoulder are unchanged; the first `pos` (on the floor) and the damage rolls moved.
        # Re-pinned again 2026-10-10 (the blood splats: 18 more draws at each of those hits): three of the swings miss
        # now (35 left after ten), so the pilot swings an eleventh time (t2421) and the walk starts 12 ticks later
        # again. The same route to the same places within 0.04; the two hits on the core are 11 and 11.
        expect=["[boss] Velagunder: HP 875 STR 63 DEF 54 lv 8", "feet (-0.454, 2.000, 27.332)", "stun 0 STUNNED",
                "goto ends at (-6.72,4.96,28.36)", "goto ends at (-2.16,10.53,32.79)", "feet (-2.198, 10.527, 32.687)",
                "hit boss part 0 (DEF 54): 11 dmg, HP 875", "hit boss part 0 (DEF 54): 11 dmg, HP 864"]),
    # Toal's fire skill from the ground (d05cfa5: the player's VM honours anime_Jump in 0x10e / 0x111): section
    # 0x10e lifts him off the floor (y 2 -> 4.368 at t74) and he lands again.
    "toal_ignis_hop": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="8:bracelet=118;9:flag=118,1;10:flag=184,1;40:pos;43:skill+;45:skill-;47:abil;74:pos;100:pos;130:quit",
        expect=[", 2.000, ", "[abil] t47 sec 0x10e mp 2400", ", 4.368, ", ", 2.000, "]),
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
        drive="2:flag=131,1;600:pos;620:pos;700:quit",  # 131 (the brain hold): a Kelemare now swims up and pushes an idle player away (ENEMY_WATER W1 / W2)
        expect=["O2 10 a255 hp 60 sub 1", "O2 0 a255 hp 41 sub 1", "[death] HP 0"]),
    "scales_air": dict(
        room="S_20/S_2008/S_2008",
        drive="2:held=97;3:flag=131,1;700:pos;710:quit", # 131: as drown_no_scales
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
        drive="5:opt=InfiniteOxygen=1;6:opt=InfiniteHP=1;7:held=97;8:killall;20:move=-1,-0.4,75;110:every=12,500,jump;"
              "300:move=-1,0.3,150;460:move=-0.2,1,600;1100:quit",   # killall: as _S2010_ENTRY
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
        absent=["input: gamepad"],   # a drive run opens no physical pad (issue #67); padhold= is the mapper's
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
    # The "looks better" switches (CleriaCore's own, off by default): CLERIA_VOPT sets them from the start, so the
    # room loads its textures' mip chains and the sprites sample nearest; Smooth edges draws the world into its
    # own target at 100% and FXAAs it onto the frame. No GPU validation error ("device: ...").
    "video_looks": dict(
        room="S_10/S_1009/S_1009", env=dict(CLERIA_VOPT="SmoothTextures=1;SmoothEdges=1;CrispSprites=1"),
        drive="10:move=1,0,30;60:vstat;61:vopt=SmoothEdges=0;70:vstat;80:quit",
        expect=["[video] opt SmoothTextures=1", "scale 1.00", "looks: textures 1 edges 1 (target on) crisp 1",
                "looks: textures 1 edges 0"], absent=["device:"]),
    # The frame limiter: FpsCap 30 holds the presented frames at <= 31 a second (the logic keeps 60 Hz).
    # config.exe parity (build/shell_re/CONFIG_PARITY.md): each option changed in mid-session takes effect at once.
    # Video: Texture Filter / the High-Definition preset rebuild the world sampler; Blood is read live; Low-Resolution
    # Textures rebuilds the texture index.
    "config_live_video": dict(
        room="S_10/S_1009/S_1009",
        drive="10:vstat;11:vopt=TextureFilter=0;20:vstat;21:vopt=Preset=3;30:vstat;31:vopt=Preset=1;"
              "32:vopt=BloodyEffectLevel=0;40:vstat;41:vopt=LowResoTexture=1;45:vstat;"
              "46:vopt=MultiSampleType=8;55:vstat;56:vopt=MultiSampleType=0;57:vopt=Mipmap=0;65:vstat;"
              "66:vopt=Preset=2;67:vstat;68:vopt=GlareEffectLevel=0;69:vstat;70:quit",
        expect=["sampler linear x1 blood 2 msaa 1 mips 1 glare 2", "sampler point x1 blood 2", "sampler linear+mip x4 blood 2 msaa 4",
                "sampler linear x1 blood 0 msaa 1", "[video] opt LowResoTexture=1", "[video] low-resolution textures on",
                "(target on) crisp 0 | sampler linear x1 blood 0 msaa 4 mips 1", "blood 0 msaa 1 mips 0",
                "sampler linear+mip x2 blood 0 msaa 1 mips 1 glare 2", "msaa 1 mips 1 glare 0"],
        absent=["device:"]),
    # Input: key preset files (.ky) in the settings folder: Save writes key_00.ky, then key_01.ky; a saved file loads.
    "config_ky_preset": dict(
        room="S_10/S_1009/S_1009", mods=[],
        drive="5:ky=save;6:ky=load,key_00;7:ky=load,nope;8:ky=save;10:quit",
        expect=["[ky] saved key_00.ky", "[ky] load key_00 ok | KEY_ACTION 0x2c KEY_UP 0x48 of 1 files",
                "[ky] load nope failed", "[ky] saved key_01.ky"]),
    # Input: Double-Tap to Dash (the dash machine: two taps of a direction with the dash owned = section 0x10a;
    # off, the same taps only run) and Hold Attack for Skills (the charge runs with the attack button held).
    "config_live_input": dict(
        room="S_10/S_1009/S_1009",
        drive="5:flag=181,1;6:bracelet=116;7:flag=182,1;20:move=1,0,3;26:move=1,0,40;40:dstat;41:pos;80:vopt=DashControl=0;"
              "100:move=1,0,3;106:move=1,0,40;120:dstat;121:pos;170:atk+;200:dstat;201:vopt=AtkBtnMagic=1;230:dstat;"
              "231:atk-;240:quit",
        expect=["[dash] on 1 state 4 latch 1 sec 0x10a", "opts dash 1 hold 0", "sec 0x10a",
                "[dash] on 0 state 0 latch 0 sec 0x1 ", "opts dash 0 hold 0", "sec 0x1 ",
                "| charge 0 lv 0 | opts dash 0 hold 0", "lv 1 | opts dash 0 hold 1"]),
    # Audio: Enable BGMs / Enable SFX switch the mixer at once, both ways.
    "config_live_audio": dict(
        room="S_10/S_1009/S_1009",
        drive="10:vopt=PlayBgm=0;20:vopt=PlayEffect=0;30:vopt=PlayBgm=1;40:vopt=PlayEffect=1;50:quit",
        expect=["[audio] music off, effects on, device 0", "[audio] music off, effects off, device 0",
                "[audio] music on, effects off, device 0", "[audio] music on, effects on, device 0"]),
    # Audio > Other > Clear game progress: Yunica's ending writes record.ta2 (first boot); on the second boot the
    # record is read, cleared in mid-session (the file deleted, the clear bytes 0), and the title entered afterwards
    # has its gates closed -- no restart.
    "config_clear_progress": dict(
        room="S_00/S_0003/S_0003", char="1",
        drive="5:flag=206,1;10:saveload;40:every=15,6000,ok;6000:quit",
        room2="S_10/S_1009/S_1009",
        drive2="5:cleardata=progress;8:cleardata=ta;20:esctap;70:right;80:right;90:right;120:ok;160:up;200:up;230:ok;"
               "260:left;290:ok;400:quit",
        expect=["record.ta2 saved", "==== second boot", "[clear] record.ta2: Yunica 01 Hugo 00 Toal 00",
                "[clear] game progress cleared: 1 file(s) deleted", "[clear] now: clear bytes 00 00 00",
                "[clear] Time Attack records cleared: 0 file(s) deleted, 0 time(s) left",
                "[title] clear data: Yunica 00 Hugo 00 Toal 00, data.nya 0 -> Bonus Modes 0, Toal's bar locked"]),
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
    # Issue #76: a level gained shows the green "Level Up" popup, plays SE 5 and attaches EF2 62 (FUN_00420c40's
    # loop), once per level: Toal's first kills at EXP x50.
    "levelup_popup": dict(
        room="S_10/S_1002/S_1002", char="3",
        drive="8:opt=DamageDealt=10;9:opt=ExpRate=50;10:opt=DamageTaken=0;15:stats;" +
              ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 100, 25)) + ";120:stats;130:quit",
        expect=["stats exp 0 lv 1", "[levelup] lv 2: popup 'Level Up' 00ff00 x0.65, SE 5, EF2 62",
                "t120 stats", " lv 2 hp 65/65"], count={"[levelup]": 1}),
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
    # Issue #68: the sound FUN_00585ff0's tail plays at the pickup is p_takeitem (88) except for the Strength /
    # Defense / MP Drops, which play s_growup0 / 1 / 2 (7 / 13 / 14) [BYTES 0x586591, 0x5865f1, 0x586672]. All
    # are collected on their 51st tick (age > 50), at the pickup's own position.
    "pickup_collect_se": dict(
        room="S_10/S_1009/S_1009", env=dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_SFXLOG="1"),
        drive="20:drop=72;21:drop=74;22:drop=76;23:drop=66;24:drop=120;200:quit",
        expect=["[sfx] t70 7 at", "[sfx] t71 13 at", "[sfx] t72 14 at", "[sfx] t73 88 at", "[sfx] t74 88 at"]),
    # Issue #68: a pickup out of the player's reach (1.9 from his origin) is collected by his sword trail: the tip
    # sphere of the first GoLocusSword within 0.45 of it [BYTES 0x5860bf..0x586139]. Toal stands 2.42 from a
    # resting drop, facing it (270), and swings at t110: collected at t122. Facing away (90) it stays.
    "pickup_sword_collect": dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_SFXLOG="1"),
        drive="20:drop=120;21:at=3.0,7.87,270;101:pickups;110:atk;150:pickups;151:quit",
        expect=["[pickup] id 120 pos (0.58, 2.00, 7.86) ground 2.020 rest 1 age 81", "[sfx] t122 88 at (0.58, 2.00, 7.86)"],
        count={"[pickup] id 120": 1}),
    # Issue #68: a pickup's sparkles are drawn (FUN_00584860's type-0 sprites, PICKUPS.md §6.4): one every 2nd
    # tick while it is not resting, 18 ticks each — 7 live at t34 from the gold dropped at t20, the oldest at
    # age 14 in its third key segment (half-size 0.27 -> 0.18, alpha 127.5 -> 0), pale yellow; none once the
    # last ones of the resting pickup have died (t140).
    "pickup_sparkle": dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="20:drop=120;21:at=3.0,7.87,270;26:pickups;34:pickups;140:pickups;141:quit",
        expect=["[pickup] sparkles 3, the oldest: age 6 at (0.14, 2.80, 7.85) half 0.347 alpha 236.8 tint ffff80",
                "[pickup] sparkles 7, the oldest: age 14 at (0.25, 2.80, 7.91) half 0.240 alpha 85.0 tint ffff80",
                "[pickup] id 120 pos (0.58, 2.00, 7.86) ground 2.020 rest 1 age 120"],
        count={"[pickup] sparkles": 2}),
    "pickup_sword_miss": dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_SFXLOG="1"),
        drive="20:drop=120;21:at=3.0,7.87,90;101:pickups;110:atk;150:pickups;151:quit",
        expect=["[pickup] id 120 pos (0.58, 2.00, 7.86) ground 2.020 rest 1 age 81",
                "[pickup] id 120 pos (0.58, 2.00, 7.86) ground 2.020 rest 1 age 130"], absent=["] 88 at"]),
    # Item 3 (FUN_004fae00): a landed swing pops the white number on the Selnarg (FUN_004fbb40 0x4fd241),
    # the combo line's meter starts (0x76c1a8), and a hit on the player pops the red one (FUN_004fd350
    # 0x4fe120).
    # The HUD's level badges (builder D 0x43ff97..0x440100): the weapon's by its tier ("Lv4" at tier 4, "Max" at 10),
    # an armor's / a footwear's by its strengthened cell ("Lv1", "Max"), red for an item whose count is 0 (the footwear, last); each
    # starts at icon x + 31 - its width (three glyphs of 9: 27).
    "hud_lv_badges": dict(
        room="S_10/S_1002/S_1002", lua='cleria.command("up", function(a) cleria.game.set_upgrade(tonumber(a), 1) end)\n',
        drive="5:tier=4;6:flag=7,1;7:flag=152,7;8:flag=31,1;9:flag=153,31;10:lua=up 31;40:hudequip;"
              "50:tier=10;51:lua=up 7;52:flag=31,0;60:hudequip;70:quit",
        expect=["hud slot 0 badge 'Lv4' x 136", "hud slot 1 badge 'Lv1' x 176", "hud slot 2 badge 'Max' x 216",
                "hud slot 0 badge 'Max' x 136", "hud slot 1 badge 'Max' x 176", "hud slot 2 badge 'Max' red x 216"]),
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
    # S_5005's Crimson Key door (Toal, Hard): its talk marker (Chr_LocateDummy 166 at (84, -8, 0.35)) stands inside the
    # closed door's hull and stays on the floor there (it was pushed onto the door's top, y -1.2, out of the probe's
    # reach: the door never opened). Walking into the door and pressing Action: sealed without the key (102), then
    # UseKey with it -> 448.
    "door_dummy_s5005": dict(
        room="S_50/S_5005/S_5005", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="2:diff=3;5:saveload;6:opt=DamageTaken=0;30:killall;35:actors;40:at3=84,-7.8,-3.5,0;41:move=0,1,40;"
              "91:act;110:every=20,200,ok;200:flagdump;210:flag=102,1;220:act;240:every=20,600,ok;600:flagdump;601:quit",
        expect=["id 166 cast 1 npc hp 25 g1 0 pos (84.0,-8.0,0.3)", "[probe] actor 166 -> open_the_door",
                "t200 flagdump", "[probe] actor 166 -> open_the_door", "t600 flagdump", " 448=1"],
        count={" 448=1": 1}),
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
# deterministic; CLERIA_LEVEL picks how the fight goes. The opponents' stats are FUN_004202e0's at the script's
# level (each brain's INIT re-stat, build/audit/S6099_DUELS.md §2), not Chr_SetStatus's 200 / 300 HP: the first
# `[duel]` line of a case pins them (HP, and `| lv .. max .. atk .. def .. exp ..`).
_S1080 = "21:move=0,-1,300;320:every=20,{end},ok;321:every=7,{end},duel={cast};"
_S3080 = "20:at=-12,36,0;30:move=0,1,30;80:every=20,{end},ok;81:every=7,{end},duel={cast};"
_LOCK = dict(CLERIA_DRIVE_LOCKSTEP="1")
# A title boot's way into the Time Attack stage select as Yunica on Normal: Bonus Modes, Time Attack, the first
# bar, Difficulty's default row. The window is up from about t270; the cases' own presses start at t300.
_TA_YUNICA = "40:down;45:down;50:ok;100:ok;170:ok;240:ok;"
CASES.update({
    # S_1080 Yunica: Vagullion (M460) dies -> Battle_1's Wait_FlagEqualInt(156, 0) -> 230, 311, warp 1.
    "duel_s1080_vagullion": dict(
        room="S_10/S_1080/S_1080", char="1", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=11000, cast=50) + "11000:dflags;11001:quit",
        expect=["[duel] t", "cast 50 hp 1860/1860 player hp 227 | lv 5 max 1860 atk 56 def 43 exp 526",
                "[content] boss_defeated S_1080", "flags 130=0 131=0 133=0 156=0 174=0 230=1 232=0 311=1"],
        count={"boss_defeated": 1}),
    # D1 (build/audit/ROUTE_F13.md): the long Vagullion fight, through phase 4's dissolve / recall /
    # re-scatter cycles and the split below half HP, to its death at about t10900 (Lv 5, his own level, against
    # his retail HP 1860; the shell's Damage taken 0 keeps Yunica up, no flag is forced). Before M970's phase-4 box clamp and the
    # sweep's edge / embedded-face fixes a bat stuck over the railing or in a wall pillar never docked,
    # M460's recall (sub 0x14) waited for it forever and 156 stayed 1 (a softlock).
    # Re-pinned 2026-10-10 (the blood splats' draws from the shared xorshift): the same fight is 770
    # ticks longer (his death at t11556, was t10786), so the run and its last read are at t13500.
    "duel_s1080_vagullion_long": dict(
        room="S_10/S_1080/S_1080", char="1", env=dict(_LOCK, CLERIA_LEVEL="5"),
        drive=_S1080.format(end=13500, cast=50) + "330:opt=DamageTaken=0;7000:dflags;13500:dflags;13501:quit",
        expect=["[access] opt DamageTaken=0", "cast 50 hp 1860/1860", "flags 130=0 131=0 133=0 156=1 174=0 230=0",
                "flags 130=0 131=0 133=0 156=0 174=0 230=1 232=0 311=1"],
        absent=["[death]"]),
    # S_1080 Hugo: Epona (M850, Battle_Hugo) -> the post-duel scene (Mask of Eyes) -> 230, 311.
    "duel_s1080_epona": dict(
        room="S_10/S_1080/S_1080", char="2", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=12000, cast=171) + "12000:dflags;12001:quit",
        expect=["[duel] t", "cast 171 hp 796/796", "| lv 5 max 796.1 atk", "[content] boss_defeated S_1080", "156=0 174=0 230=1 232=0 311=1"],
        count={"boss_defeated": 1}),
    # S_1080 Toal: Galleon (M830, Battle2) stops at HP 1 (his low-HP chain) -> 174 = 1, 230, 311.
    "duel_s1080_galleon": dict(
        room="S_10/S_1080/S_1080", char="3", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=12000, cast=169) + "12000:dflags;12001:quit",
        expect=["[duel] t", "cast 169 hp 592/592", "| lv 5 max 592 atk 58 def 57 exp 744", "[content] boss_defeated S_1080",
                "156=0 174=1 230=1 232=0 311=1"], count={"boss_defeated": 1}),
    # S_1080 is not losable (no 133): a KO is the game over; Retry re-enters at Battle_1's SaveContData
    # (205 = 1), whose root branch restarts the duel: Chr_SetStatus's 200 again, then the INIT re-stat again.
    # Issue #78: the Retry's scene load hides the boss HP gauge (`gauge`: 0 two ticks after it), and the
    # restarted duel's brain shows it again.
    "duel_s1080_gameover": dict(
        room="S_10/S_1080/S_1080", char="1", env=dict(_LOCK, CLERIA_LEVEL="1"),
        drive=_S1080.format(end=4900, cast=50) + "4699:gauge;4700:hp=0;4822:gauge;4894:gauge;4900:quit",
        expect=["cast 50 hp 1860/1860", "gauge 1 id 167", "[death] HP 0", "[gameover] chose retry", ">> entering S_1080",
                "[gameover] Retry -> S_1080", "cast 50 hp 200/200", "gauge 0 id 0", "| lv 5 max 1860 atk 56",
                "gauge 1 id 167"], absent=["[duel] lost", "boss_defeated"]),
    # S_3080 Yunica: Kishgal (M860) ends the fight at half HP in this room -> 174 = 1 -> the Silver
    # Harmonica scene -> 232 = 1 and ChangeScene to S_1000. The half-HP end is read at t8900 (was t8850): a pillar
    # now freezes Yunica once on the way (t8669, KISHGAL_SHIELD.md §4), so the fight ends at t8859, not before t8850.
    # Re-pinned 2026-10-10 (the spark streaks' draws from the shared xorshift at the hits for 0):
    # the same fight ends at t8952, so the half-HP end is read at t9000.
    "duel_s3080_kishgal": dict(
        room="S_30/S_3080/S_3080", char="1", env=dict(_LOCK, CLERIA_LEVEL="28"),
        drive=_S3080.format(end=15000, cast=172) + "9100:dflags;15000:dflags;15001:quit",   # 9100 (was 9000) since FIX_SLOTS S5c: the handlers run under the intro's Fix(0), the RNG is elsewhere at the first exchange and the half-HP end comes at t9038 (was t8845)
        expect=["cast 172 hp 2985/2985", "| lv 19 max 2985 atk 121 def 108 exp 5766", "[content] boss_defeated S_3080",
                "133=1 156=0 174=1", ">> entering S_1000", "232=1"], absent=["[duel] lost"], count={"boss_defeated": 1}),
    # S_3080 Hugo wins against Toal (M820): 174 = 1 -> Battle_Win (its 0.4 slow-mo) -> EV_Feena02_2, where
    # FireBulletEvent fires Shion's six shots (op 0x111) -> S_1000.
    "duel_s3080_toal_win": dict(
        room="S_30/S_3080/S_3080", char="2", env=dict(_LOCK, CLERIA_LEVEL="40"),
        drive=_S3080.format(end=18000, cast=168) + "9400:dflags;18000:dflags;18001:quit",
        expect=["cast 168 hp 3550/3550", "| lv 19 max 3550 atk 119 def 120 exp 2223", "[content] boss_defeated S_3080", "156=0 174=1", "[xso] SetTimeScale 0.40", "[xso] SetTimeScale 1.00",
                "[xso] FireBullet actor 200 kind 2 effect 308 pos (-12.00, -3.00, 33.00) dir (0.00, -0.19, 1.00) speed 0.30",
                "[xso] FireBullet actor 200 kind 2 effect 308", "dir (0.15, -0.17, 1.00) speed 0.50",
                ">> entering S_1000", "232=1"], absent=["[duel] lost"]),
    # S_3080 Hugo loses (a KO under flag 133, FUN_0046f810): no game over, 174 stays 0 -> Battle_Lose ->
    # EV_Feena02_2 -> S_1000. At Lv 20 Toal (ATK 119) knocks him out by himself at about t8360; nothing is forced.
    "duel_s3080_toal_lose": dict(
        room="S_30/S_3080/S_3080", char="2", env=dict(_LOCK, CLERIA_LEVEL="20"),
        drive=_S3080.format(end=16500, cast=168) + "8500:dflags;16500:dflags;16501:quit",
        expect=["cast 168 hp 3550/3550", "[duel] lost: flag 133 set", "133=1 156=0 174=0", "[xso] FireBullet actor 200",
                ">> entering S_1000", "232=1"], absent=["[gameover]", "SetTimeScale 0.40", "boss_defeated"]),
    # S_3080 Toal: Shion (M840, Battle2) -> 174 = 1 -> FireBulletEvent2 (Hugo's shots, one of Shion's) -> S_4104.
    "duel_s3080_shion": dict(
        room="S_30/S_3080/S_3080", char="3", env=dict(_LOCK, CLERIA_LEVEL="32"),
        drive=_S3080.format(end=14000, cast=170) + "14001:quit",
        expect=["cast 170 hp 1492/1492", "| lv 19 max 1492 atk 121 def 118 exp 2223", "[content] boss_defeated S_3080", "[xso] FireBullet actor 197 kind 2 effect 308", "[xso] FireBullet actor 200",
                "[xso] FireBullet actor 197", ">> entering S_4104"], absent=["[gameover]"]),
})

# The floor-6 rival duels, S_6099 (build/audit/S6099_DUELS.md): from the room's event box through Evt_6099_N, the
# fight to the KO (the handler's 174 = 1 / 156 = 0) and Battle_*'s post-scene to 236 = 1. The opponent's stats are
# retail's at the script's level 48. Lv 50, the last weapon tier and the shell's Damage taken 0 (the drive has no
# armour or shield to give; 342 HP does not last against ATK 250); 231 = 1 is the story state after floor 2.
_S6099 = ("5:flag=231,1;8:tier=10;9:opt=DamageTaken=0;40:move=0,1,300;80:every=20,{end},ok;81:every=7,{end},duel={cast};"
          "{end}:dflags;{end2}:flagdump;{end3}:quit")
_F6 = dict(_LOCK, CLERIA_LEVEL="50")
CASES.update({
    # Yunica vs Kishgal (M860): not S_3080, so his INIT sets 179 (EXP + 7500) and he is fought to the KO.
    "duel_s6099_kishgal": dict(
        room="S_60/S_6099/S_6099", char="1", env=_F6,
        drive=_S6099.format(end=10200, end2=10201, end3=10202, cast=172),
        expect=["cast 172 hp 6249/6249 player hp 342 | lv 48 max 6249 atk 251 def 222 exp 21848",
                "flags 130=0 131=0 133=0 156=0 174=1", " 179=1", " 236=1"], absent=["[death]"]),
    # Hugo vs Demon Toal (M820): the scripts set 179 (EXP + 16000); Battle_Hugo clears it again in the post-scene.
    "duel_s6099_toal": dict(
        room="S_60/S_6099/S_6099", char="2", env=_F6,
        drive=_S6099.format(end=8800, end2=8801, end3=8802, cast=168),
        expect=["cast 168 hp 7431/7431 player hp 342 | lv 48 max 7431 atk 247 def 248 exp 21532",
                "flags 130=0 131=0 133=0 156=0 174=1", " 236=1"], absent=["[death]"]),
    # Toal vs Hugo (M810) and his Eyes (M900) under 231: HP x1.25, DEF + 19, ATK - 1.5 + 31 + the aura's 2.5, EXP x4.
    "duel_s6099_hugo": dict(
        room="S_60/S_6099/S_6099", char="3", env=_F6,
        drive=_S6099.format(end=14200, end2=14201, end3=14202, cast=167),
        expect=["cast 167 hp 4186/4186 player hp 342 | lv 48 max 4186.25 atk 249 def 259 exp 22128",
                "[content] boss_defeated S_6099", "flags 130=0 131=0 133=0 156=0 174=1", " 231=1", " 236=1"], absent=["[death]"],
        count={"boss_defeated": 1}),
    # S_5012's spinning floors (s50_yuka_01 / 02, 'LOOP' from the load) carry a rider by their whole matrix
    # (rider.hpp, MOVER.md §3.3). Toal glides from the entry platform onto an arm of the upper floor (the fire
    # skill's air jump and slow fall), stands on it for half a turn (t320..t560: y stays 2.000 while x / z go
    # round its hub (44, 42)), glides off to the far platform, climbs the steps and leaves by the S_5102 door.
    # The floors used to stand still: a rider was carried by translation only and their gaps swept under him.
    "yuka_ride": dict(
        room="S_50/S_5012/S_5012", char="3", env=dict(_LOCK),
        drive="5:opt=InfiniteHP=1;6:killall;7:bracelet=118;8:flag=184,1;20:at3=25,4,27.5,0;240:move=0.81,0.59,70;"
              "241:jump;242:jump;258:skill+;260:skill-;320:pos;440:pos;560:pos;570:move=0.645,0.764,170;571:jump;"
              "572:jump;588:skill+;590:skill-;648:jump;649:jump;745:move=0,1,500;760:every=20,1240,jump;1250:quit",
        # Re-pinned 2026-10-11 (the jump gate reads +0x3a0): the second of the two presses a tick apart (241 / 242, 571 / 572)
        # is an air press and no longer kicks him up a second time, so he lands elsewhere on the arm; the ride is the same.
        expect=["feet (34.344, 2.000, 37.547)", "feet (38.092, 2.000, 50.841)", "feet (51.369, 2.000, 49.654)",
                ">> entering S_5102"], absent=[">> entering S_5009"]),
    # The same carry on S_5005's floor (s50_yuka_00, hub (84, -28)) and S_5012's lower one (hub (66, 20), the
    # other way round): a standing player stays on for more than a full turn.
    "yuka_stand_s5005": dict(
        room="S_50/S_5005/S_5005", char="3", env=dict(_LOCK),
        drive="5:opt=InfiniteHP=1;6:killall;20:at3=86,-8,-28,0;140:pos;540:pos;541:quit",
        expect=["feet (84.359, -10.000, -26.033)", "feet (85.968, -10.000, -28.359)"], absent=[">> entering"]),
    "yuka_stand_s5012_low": dict(
        room="S_50/S_5012/S_5012", char="3", env=dict(_LOCK),
        drive="5:opt=InfiniteHP=1;6:killall;20:at3=73,-4,20,0;140:pos;540:pos;541:quit",
        expect=["feet (70.203, -6.000, 25.598)", "feet (61.797, -6.000, 14.402)"], absent=[">> entering"]),
    # The way across both rooms with NO skill (no bracelet equipped), the double jump alone (flag 163, the Gold
    # Bracelet). The gaps are the meshes' [BYTES of S_5012.YMO / S_5005.YMO / s50_yuka_0x.YMO]: S_5012's entry
    # platform's edge is 21.21 from the upper floor's hub (44, 42) and an arm reaches 12.73 (its corners 13.42), so
    # the gap is 7.8..8.5 whatever the turn; S_5005's south platform is 15.56 from the hub (84, -28) of a floor
    # reaching 8.49 / 8.94: 6.6..7.1 (and that platform stands 4 above its own approach). A plain running jump
    # carries 5.65 (apex 2.63), the double jump up to ~9.5 (apex 5.37): the double jump is the way on, as in the
    # original's own order (the Gold Bracelet is S_4002's). The ways OFF are 5.0..5.7. [H] not timed against the
    # original. Toal, Hard: on at t300 (the windows are ~70 of every 133 ticks), half a turn, off to the north-east
    # platform and up the steps to the S_5102 door; S_5005: on, most of a turn, off north and through door_01
    # (448 set: the Crimson Key used) to S_5008. No ledge recovery fires on these jumps.
    "yuka_cross_s5012": dict(
        room="S_50/S_5012/S_5012", char="3", env=dict(_LOCK),
        drive="2:diff=3;5:opt=InfiniteHP=1;6:killall;7:flag=163,1;241:at3=23,2,21,45;246:move=0.7071,0.7071,100;300:jump;"
              "330:jump;400:pos;560:pos;628:move=0.7071,0.7071,170;634:jump;660:jump;700:pos;720:every=20,790,jump;"
              "800:move=-0.033,0.9995,500;805:every=20,1290,jump;1300:quit",   # 805 (was 801) since the jump gate reads +0x3a0: a press on the landing frames now jumps, and the old phase took him off a step once ([ledge] t1001)
        expect=["feet (33.636, 2.000, 36.923)", "feet (41.689, 2.000, 53.298)", "feet (62.963, 2.476, 55.832)",
                ">> entering S_5102"], absent=[">> entering S_5009", "[ledge]"]),
    "yuka_cross_s5005": dict(
        room="S_50/S_5005/S_5005", char="3", env=dict(_LOCK),
        drive="2:diff=3;3:flag=448,1;4:saveload;5:opt=InfiniteHP=1;6:killall;7:flag=163,1;275:at3=68.5,-10,-43.5,45;"
              "280:move=0.7071,0.7071,80;320:jump;350:jump;420:pos;700:pos;712:move=0,1,52;720:jump;732:jump;790:pos;"
              "800:move=0.2,1,200;806:jump;1010:quit",
        expect=["feet (80.990, -10.000, -34.331)", "feet (86.272, -10.000, -21.375)", "feet (81.618, -10.000, -11.591)",
                ">> entering S_5008"], absent=[">> entering S_5006", "[ledge]"]),
    # ... and without it: the same run and a plain jump fall short of the arm, into the pit ('naraku' -> S_5009).
    "yuka_gap_s5012": dict(
        room="S_50/S_5012/S_5012", char="3", env=dict(_LOCK),
        drive="2:diff=3;5:opt=InfiniteHP=1;6:killall;241:at3=23,2,21,45;246:move=0.7071,0.7071,100;300:jump;330:jump;520:quit",
        expect=[">> entering S_5009"], absent=["[ledge]"]),
    # The integrator's ledge recovery (ledge.hpp, MOVER.md §3.5, FUN_0045a510 0x45b607): Toal runs to S_1009's east
    # wall (x 10.337), is then put outside the room mesh (nothing within 510 under him) and is back on his last
    # safe spot in the same tick; he used to stand in the void at his last floor height.
    "ledge_recover": dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(_LOCK),
        drive="20:move=1,0,100;130:pos;140:at=12.5,7.667,0;141:pos;150:pos;151:quit",
        expect=["feet (10.337, 0.000, 7.667)", "[ledge] t140 no floor under (12.50, 0.00, 7.67): back to (10.34, 0.00, 7.67)",
                "feet (10.337, 0.000, 7.667)", "feet (10.337, 0.000, 7.667)"], count={"[ledge] t": 1}),
    # Issue #5 (build/mech_re/KISHGAL_SHIELD.md §3): Toal vs Kishgal (M860) in S_6080. Toal's Ignis (the fire
    # bracelet 118: EF2 162, one of the brain's 13 breaker ids) overlapping an ice pillar (EF2 0x122) breaks it:
    # SE 424, the pillar killed, EF2 0x123. The fight still ends in the KO.
    "kishgal_pillar_break": dict(
        room="S_60/S_6080/S_6080", char="3", env=_F6,
        drive="5:flag=231,1;6:opt=InfiniteHP=1;7:bracelet=118;8:tier=10;9:flag=184,1;40:move=0,1,300;80:every=20,6000,ok;"
              "81:every=7,6000,duel=172;400:every=50,6000,skill+;403:every=50,6000,skill-;6000:dflags;6001:quit",
        expect=["cast 172 hp 5686/5686", "[m860] t", "pillar broken at (", "[content] boss_defeated S_6080",
                "flags 130=0 131=0 133=0 156=0"]),
    # Issue #5 (§4): the freeze. Toal stands still; a pillar (EF2 0x122, impact 0x123) hits him for a medium
    # reaction (section 0xb; Damage taken x0.3 keeps the hit under half his HP, which would be section 9 and no
    # freeze) -> on Kishgal's next brain tick section 9 through the gated request, EF2 0x11e (286) on him and his
    # x / z pinned for 20 brain ticks.
    "kishgal_freeze": dict(
        room="S_60/S_6080/S_6080", char="3", env=dict(_LOCK, CLERIA_LEVEL="30", CLERIA_DMG="1"),
        drive="5:flag=231,1;6:opt=InfiniteHP=1;7:opt=DamageTaken=0.3;40:move=0,1,300;80:every=20,2600,ok;"
              "81:every=7,2600,duel=172;2816:pos;2823:fxall;2834:pos;2835:quit",
        expect=["reaction section 11", "fx 290 (atk 231 power -2.0", "[freeze] t2814 #202: section 9 taken, pinned at (-0.48, 47.51)",
                "feet (-0.478, 2.000, 47.512)", "sec 0x9", "[fxall] id 286 attach 0 at (-0.48, 3.08, 47.51)",
                "feet (-0.478, 2.000, 47.512)"], count={"[freeze] t": 1}),
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
        # "stats exp 2" (was 3) since ENEMY_WATER.md W1: the sand probe starts 0.25 + 0.7 under the feet for every actor
        # [EXPORT 0045c490.c:137-141] (the enemies' was 0.7), so the Karons count as in the sand sooner and wade slower:
        # one bystander fewer walks into the swings. The kill of #60 and its revival are as before.
        expect=["[revive] #186 cast 60 hp 261 phase 1 7a0 0 gnd 1 t 0.000 y -4.00", "defeated 1", "stats exp 2 lv 1",
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
                "[content] boss_defeated S_4013", "flags 130=0 131=1 133=0 156=0"], count={"boss_defeated": 1}),
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
    # build/mech_re/KEEPOUT_BOXES.md (FUN_0042dc90): a monster cannot stand in an exit volume. S_1004's S_1001 door
    # volume is x -2.6..2.4, z 3.4..15.9: a Riffligan put at its mouth (epos) and knocked north by the player's swings
    # is put back at the volume's face (z 3.40) at the head of each tick it was pushed in, and ends outside.
    "keepout_door": dict(
        room="S_10/S_1004/S_1004",
        env={"CLERIA_KEEPOUT": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:opt=DamageDealt=0.05;20:epos=#189,0,2.9;22:near=#189,1.6;"
              "30:every=20,150,atk;180:actors;181:quit",
        # Re-pinned 2026-10-10 (the dust puffs' 20 draws a hit): the swing effect's particles land other hits (HP 44
        # at the end, was 42); the last push ends at t143 with the Riffligan on the volume's face (z 3.40), where it
        # stands at t180 — it used to have walked back to (0.1, 0.0, 3.0). Outside the volume either way.
        # Re-pinned again 2026-10-10 (the blood splats' 18 draws after a hit): HP 42 again, and it has walked back
        # to (0.1, 0.0, 3.1) by t180.
        expect=["epos #189 (0.00, 0.00, 2.90)", "[keepout] t", "#189 cast 14 (0.00, 1.03, 3.4", "-> (0.00, 1.03, 3.40)",
                "t180 actors", "id 189 cast 14 enemy hp", "pos (0.1,0.0,3.1)"],
        absent=[">> entering"]),
    # build/mech_re/ASO_NOOPS.md §1 (anime_ChangeSpecular, funcid 0x1f): the attack wind-up flash. A Riffligan's section
    # 7 writes its specular tint struct (0 -> 192 grey, step 0.075, count 0): the sprite brightens over 14 ticks
    # (0x0e, 0x1c .. 0xbb) and the struct switches off on the tick t reaches 1.
    "windup_flash": dict(
        room="S_10/S_1004/S_1004",
        env={"CLERIA_SPEC": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:epos=#189,0,1.2;22:near=#189,1.6;120:quit",
        expect=["#189 cast 14 section 7: 000000 -> c0c0c0 step 0.075 count 0", "#189 specular 0e0e0e",
                "#189 specular 818181", "#189 specular bbbbbb"],
        absent=["specular c0c0c0"]),
    # build/mech_re/ASO_NOOPS.md §3 (anime_GenerateDebris, funcid 0x26): a monster's death section throws its five
    # spinning pieces (texture base 0x42) through the pickup spawner: 5 shuffle draws + 5 x 5 = 30 draws of the shared
    # xorshift on the kill's tick, the pieces starting 2 ticks apart.
    "debris_death": dict(
        room="S_10/S_1002/S_1002", env={"CLERIA_DEBRIS": "1", "CLERIA_DUST": "1", "CLERIA_LEVEL": "3"},
        drive="8:opt=DamageDealt=10;10:opt=DamageTaken=0;" +
              ";".join("%d:near=17;%d:atk;%d:pickups" % (t, t + 1, t + 20) for t in range(20, 120, 25)) + ";130:quit",
        # ... and each hit for 1 or more raises FUN_004fab40's five puffs in the cast's colour first (20 draws).
        expect=["hit: 5 puffs at", "size 0.300, 20 draws", "[debris] t", "5 pieces", "30 draws", "[pickup] debris id"]),
    # [cleria.qol] EnemyHealthBars (CleriaCore's own, not retail; src/cleria/enemybar.hpp), mode 1 "After a hit": the
    # first hit on a Riffligan turns its bar on, the second (t52) restarts it, and 120 + 255 steps later it has faded.
    # Mode 0 (the default) prints nothing and draws nothing: every other case runs with it.
    "enemy_bars": dict(
        room="S_10/S_1002/S_1002", env={"CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="5:vopt=EnemyHealthBars=1;8:opt=DamageDealt=0.3;10:opt=DamageTaken=0;20:near=17;21:atk;45:near=17;46:atk;"
              "440:quit",
        expect=["[video] opt EnemyHealthBars=1", "[ebar] t27 bar on #17 (cast 17): 73 / 74", "[ebar] t427 bar off #17 (faded)"],
        count={"[ebar] ": 2}),
    # build/mech_re/GOXX_PARTICLES.md §3.1 (FUN_00584550, the dust puffs). The foot dust: anime_FootSmoke leaves the
    # bit +0x3c0 & 1, FUN_0045c490 takes it on the ground and throws five puffs (20 draws), then waits 5 ticks —
    # Yunica's dash 0x10a (flag 181 + a held button) raises one every 6th tick.
    "foot_dust": dict(
        room="S_10/S_1009/S_1009", env={"CLERIA_DUST": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="5:flag=181,1;8:skill+;10:move=1,0.3,40;60:quit",
        expect=["[dust] t14 foot: 5 puffs at (0.85, 2.18, 8.04) size 0.350, 20 draws", "[dust] t20 foot: 5 puffs",
                "[dust] t26 foot: 5 puffs"],
        absent=["[dust] t15", "[dust] t16", "[dust] t17", "[dust] t18", "[dust] t19"]),
    # ... and the player-hurt puff (FUN_004fd350 0x4fe014 -> FUN_004fab40): five puffs, 20 draws, on the side the
    # hit came from, with every damaging hit (a Riffligan's bite).
    "hurt_dust": dict(
        room="S_10/S_1004/S_1004", env={"CLERIA_DUST": "1", "CLERIA_DMG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="5:opt=InfiniteHP=1;20:epos=#189,0,1.2;22:near=#189,1.6;120:quit",
        expect=["hurt: 5 puffs at", "size 0.300, 20 draws", "[hurt] dmg"]),
    # build/mech_re/GOXX_PARTICLES.md §2.3 / §3.7 (FUN_00589130, the spark streaks, GoXx type 3). A Wilewarer's guard
    # (section 6, the player's swing from the front): FUN_004fbb40 throws three sparks at the guard (4 draws each: 12)
    # and, the hit being for 0, the parry SE and four more thrown back (16); here none of them bounces in its ten
    # pre-rolled quarter-steps (a bounce is one more draw).
    "spark_guard": dict(
        room="S_10/S_1008/S_1008", env={"CLERIA_DUST": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:near=#189,1.4;21:atk;40:near=#189,1.4;41:atk;80:quit",
        expect=["[spark] t42 guard: 3 sparks at (1.06, 1.28, 2.54), 12 draws", "[popup] 0 ffffff",
                "[spark] t42 zero hit: 4 sparks at (1.06, 1.28, 2.54), 16 draws"]),
    # build/mech_re/GOXX_PARTICLES.md §3.3 - §3.5 (FUN_0058a5f0 / FUN_0058a900, the blood splats; the decals are kept,
    # simulated and drawn). A hit a monster survives: three splats in its colour on its next tick (18 draws); the
    # kill: three more at the death (18), before the debris; each debris piece leaves a drop where it bounces (4
    # draws, two a piece at most).
    "blood_splats": dict(
        room="S_10/S_1002/S_1002", env={"CLERIA_DUST": "1", "CLERIA_DEBRIS": "1", "CLERIA_LEVEL": "3", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="8:opt=DamageDealt=10;10:opt=DamageTaken=0;20:near=17;21:atk;45:near=17;46:atk;140:quit",
        expect=["[blood] t27 hit: 3 splats ff7c0000 at (30.50, -2.87, -36.94), 18 draws, 3 decals",
                "[blood] t52 death: 3 splats ff7c0000", "18 draws, 6 decals", "[debris] t61", "30 draws",
                "debris 44: a drop ff7c0000", "4 draws"],
        count={"a drop ff7c0000": 10}),
    # ... and every hit the player takes, at BloodyEffectLevel 2: the dust puff, then three splats of 0xff550a00.
    "blood_hurt": dict(
        room="S_10/S_1004/S_1004", env={"CLERIA_DUST": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="5:opt=InfiniteHP=1;20:epos=#189,0,1.2;22:near=#189,1.6;120:quit",
        expect=["hurt: 5 puffs at", "20 draws", "hurt: 3 splats ff550a00", "18 draws"]),
    # build/mech_re/LAUNCH_STATE.md §0 / §5 step 2: the stun is reachable in play. Yunica's down thrust (section 0x10b,
    # attack kind 2: Attack while falling, no stick) deals damage -> the victim's event bit 0x2000 -> the action
    # player's roll on its next tick (30 %; 60 % with 0x4000) -> phase 3 for 99 ticks, the stars, section 0x13b.
    # Re-timed 2026-10-11 (the jump gate reads +0x3a0, not the section): the jump is pressed at t + 2, not t + 1 — a
    # press one tick after the last thrust's recovery ends is still an air press (+0x3a0 is 3 through 0x10b's
    # Movable(0) and is rewritten on the first Movable tick) and was refused. The first thrust now stuns (t65, was the
    # fifth at t304); launch_juggle moves its three swings after that stun (the monster put on open floor first) and
    # launch_flyer's lines come one tick later; every number of the mechanics is as before.
    "stun_thrust": dict(
        room="S_10/S_1004/S_1004", env={"CLERIA_DMG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:opt=DamageDealt=0.2;20:epos=#189,0,1.2;" +
              ";".join("%d:near=#189,0.8;%d:jump;%d:atk" % (t, t + 2, t + 24) for t in range(30, 400, 60)) + ";420:quit",
        expect=["hit Riffligan", "[stun] t65 #189 cast 14 stunned for 98 ticks (events 2000)"]),
    # LAUNCH_STATE.md §1 / §2: the knock-up and the juggle. The Riffligan stunned as above (t304); a ground swing on it
    # launches it (+0x4d0 = 1, t 0.175), the next two juggle it in the air (t 0.15 - 0.0125 n: 0.1375, 0.125), each
    # with the damage x 1.2 truncated (5 -> 6, 6 -> 7); it lands, the count is cleared, and the hit at t203 is plain.
    "launch_juggle": dict(
        room="S_10/S_1004/S_1004", env={"CLERIA_DMG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:opt=DamageDealt=0.2;20:epos=#189,0,1.2;" +
              "30:near=#189,0.8;32:jump;54:atk;70:opt=DamageDealt=0.6;86:epos=#189,0,-1.6;92:near=#189,1.2;93:atk;"
              "110:near=#189,1.2;111:atk;128:near=#189,1.2;129:atk;202:near=#189,1.2;203:atk;233:quit",
        expect=["[stun] t65 #189 cast 14 stunned", "[launch] t94 #189 launched: count 1, t 0.1750, stun 69, dmg 5 -> 6",
                "[launch] t112 #189 juggled: count 2, t 0.1375, stun 51, dmg 5 -> 6",
                "[launch] t130 #189 juggled: count 3, t 0.1250, stun 33, dmg 6 -> 7", "hit Riffligan (lv1 DEF 43): 7 dmg",
                "hit Riffligan (lv1 DEF 43): 4 dmg"],
        count={"[launch]": 3}),
    # ... and a flyer (a Baocar, +0x398): never launched; a hit while it is stunned adds 12 ticks to the stun (39 -> 51).
    "launch_flyer": dict(
        room="S_10/S_1002/S_1002", env={"CLERIA_DMG": "1", "CLERIA_DRIVE_LOCKSTEP": "1", "CLERIA_LEVEL": "3"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:opt=DamageDealt=0.2;" +
              ";".join("%d:near=#186,0.8;%d:jump;%d:atk" % (t, t + 2, t + 24) for t in range(30, 200, 60)) + ";230:quit",
        expect=["[stun] t125 #186 cast 43 stunned for 98 ticks", "[launch] t184 #186 flyer: count 0, t 0.0000, stun 51, dmg 2 -> 2"]),
    # A Venueba's slime puddles (M200.md §3.7b / §3.8, BULLETS.md §3.3; FUN_0058a5f0 with flag B, 6 draws a puddle; kept,
    # not drawn): one near it every 10th tick of its hit reaction, and the puddles of its glob (kind 0x29) where it lands.
    "slime_puddles": dict(
        room="S_20/S_2017/S_2017", env={"CLERIA_DUST": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:opt=DamageDealt=0.1;20:seen=#24,4;60:every=40,600,near=#24,1.4;"
              "61:every=40,600,atk;700:quit",
        expect=["[blood] t71 slime: 1 splats ff22cc44 at (1.71, 1.33, 17.98), 6 draws",
                "glob: 2 splats ff22cc44 at (1.29, -0.04, 17.96), 12 draws"]),
    # build/audit/REACQUIRE.md §4.1: four pins of what the original's own rules do (so that nobody "fixes" them away).
    # The teleport sweep (§1.1): a Riffligan re-acquires at the first [ai] sample after each return; the placement
    # commands re-seed the ledge guard (§1.3), so no [ledge] snaps the player back to the old spot.
    "reacq_teleport": dict(
        room="S_10/S_1004/S_1004",
        env={"CLERIA_AI": "1", "CLERIA_LOS": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#189,4;230:at=0.05,-0.35,0;530:seen=#189,3;"
              "770:seen=#189,6;1010:quit",
        expect=["seen #189 placed", "[ai] #189 cast 14 phase 2", "t230 at=", "[los] t231 #189 cast 14 2->1",
                "[ai] #189 cast 14 phase 1", "seen #189 placed", "[ai] #189 cast 14 phase 2", "t540",
                "seen #189 placed", "[ai] #189 cast 14 phase 2", "t780"],
        absent=["[ledge]"]),
    # C1: the Riffligan below S_1004's 2-high platform loses a player 5.8 u away on the platform's side wall (the
    # keep, FUN_00475c00), wanders, and sees the player again from further out. The second drop of the run is the
    # leash of its neighbour #187 (D3, 0x47a39e): 12.5 from its anchor, more than 4 from the target, a clear line.
    "keep_ledge": dict(
        room="S_10/S_1004/S_1004",
        env={"CLERIA_AI": "1", "CLERIA_LOS": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#189,4;150:move=-1.000,0.023,420;"
              "580:move=1.000,-0.023,480;1290:quit",
        expect=["[los] t167 #189 cast 14 2->1", "dist 5.84 blocked 1", "[ai] #189 cast 14 phase 1",
                "[ai] #189 cast 14 phase 2", "[los] t971 #187 cast 14 2->1", "dist 9.98 blocked 0"]),
    # C2: a Jiamobre at the top of its jump is 10.03 above a player in S_3003's pit: over the vertical limit of
    # 10 + H/2, dropped with a clear line (blocked 0).
    "keep_dy_limit": dict(
        room="S_30/S_3003/S_3003",
        env={"CLERIA_AI": "1", "CLERIA_LOS": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#35,4;150:move=0.988,0.152,420;"
              "580:move=-0.988,-0.152,480;1290:quit",
        expect=["[los] t798 #35 cast 35 2->1", "blocked 1", "[los] t894 #195 cast 35 2->1", "dist 10.92 blocked 0"]),
    # C4: a far-frozen Hekat-Baocar (25 u or more: the ASO step only) keeps phase 2 across the absence and attacks
    # at once on the return; the [ai] log says "frozen" for it instead of printing nothing.
    "far_freeze_keeps_phase": dict(
        room="S_60/S_6050/S_6050",
        env={"CLERIA_AI": "1", "CLERIA_LOS": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;20:seen=#75,4;230:at=0,-32,0;530:seen=#75,3;770:seen=#75,6;"
              "1010:quit",
        expect=["[ai] #75 cast 75 phase 2", "t230 at=", "[ai] #75 cast 75 phase 2 frozen (25 u or more) t240",
                "frozen (25 u or more) t480", "seen #75 placed", "[ai] #75 cast 75 phase 2 sub 0 act 0x1", "sec 7",
                "t540"],
        absent=["[los] t", "[ai] #75 cast 75 phase 1"]),
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
    # Nygtilger (boss02, build/boss_re/NYGTILGER.md) from the room's retry start, to the kill (flag 221). His plasma
    # balls are bullet kind 0x24 (36), moved by FUN_0059fa30 in bullet.cpp (build/mech_re/BULLET_KINDS.md §5): glued to
    # a segment for 60 calls, drifting to a point that homes on the player until 300, then the strike (SE 221 / 222 /
    # 223 in turn, effect 347) — or ended early with the carrier or inside 8.05 of the pillar.
    # His poison balls are kind 0x23 (35), moved by FUN_0059e860 (BULLET_KINDS.md §3) since 2026-10-10: carried by the
    # dead segment for 60 ticks (3 draws a tick each), then flying, bouncing, lying on the arena floor until the
    # poof at age 590 (SE 219); the player's body pushes one that lies in his way.
    "nygtilger_fight": dict(
        room="S_20/S_2099/S_2099",
        env={"CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=40;9:flag=205,1;10:saveload;40:every=20,700,ok;"
              "720:every=30,5000,boss2;726:every=30,5000,atk;800:every=50,4200,bullets;5000:quit",
        expect=["[boss02] Nygtilger: pool 7000 (head 700, segments 350) lv 16", "[bullet] kind 36 effect 348",
                "a plasma ball strikes (SE 221)", "a plasma ball strikes (SE 222)", "a plasma ball strikes (SE 223)",
                "[bullet] kind 35 effect 343", " carried", "[bullet] kind 35 effect 343", " flying",
                "a poison ball poofs at", "a poison ball is pushed by the player", "flag 221 = 1",
                "[content] boss_defeated S_2099 flag 221"]),
    # Khonsclard (boss04, build/boss_re/KHONSCLARD.md): the intro (START), flag 156, two pods rise; breaking
    # them downs the boss and opens the core (jump attacks); every 20 % adds a pod; the kill writes 223 and
    # the EXP, and the 1/340 fade destroys the boss ~339 frames later. Level 50 and Damage x10 keep it short.
    # Re-timed 2026-10-10 (BOSS_HULLS step 3: his hull and the pods' are bound to the render bones). The core and the
    # fangs are solid where they are drawn, so `boss4` now stands the player on the downed core (feet y 5.1 .. 8.2,
    # was 3.54 on the base piece with the core's hull hanging at y 13 .. 18) and more swings land per knock-down: at
    # Damage dealt 10 he died in band 4 (t2058) and never reached band 5. And a risen pod's hull is on the pod, so it
    # pushes the player off the spot `boss4` picks inside it (up to 2.7 from the pod: some swings fall short). At
    # Damage dealt 8 the fight has every event again, in the same order: band 5 from t3390, the kill at t4730 (was
    # t2890 at 10), destroyed at t5069 — hence the longer run. The knock-down counter is caught by the 30-tick status
    # line at 11 (was at 1): "down 11 ".
    "khonsclard_fight": dict(
        room="S_40/S_4099/S_4099",
        env={"CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=8;20:at=-2.8,23.3,0;"
              "40:every=20,700,ok;720:every=40,5400,boss4;728:every=40,5400,atk;5400:quit",
        expect=["[boss04] Khonsclard: body 3000, pods 600", "clip START fr", "f156 1", "states 00", "down 11 ",
                "band 2", "band 5", "flag 223 = 1", "EXP 328", "destroyed"]),
    # The world-anchored mini gauges (build/mech_re/MINI_GAUGE.md; the shared store 0x76c1cc.., worldgauge.hpp): a hit
    # pod writes gauge 0 on Normal (the host used to drop it), and the HUD tick's 255 countdown turns it off after
    # the last hit. Gelaldy's arm (S_3099 from its retry start) writes the same record.
    "mini_gauge_khonsclard": dict(
        room="S_40/S_4099/S_4099",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=30;20:at=-2.8,23.3,0;40:every=20,700,ok;"
              "720:every=40,900,boss4;728:every=40,900,atk;1200:quit",
        expect=["[boss04] Khonsclard: body 3000, pods 600", "mini gauge 0 on: 599 / 600", "mini gauge 0 off"]),
    "mini_gauge_gelaldy": dict(
        room="S_30/S_3099/S_3099",
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=30;9:flag=205,1;10:saveload;40:every=20,700,ok;"
              "720:every=40,800,boss3;728:every=40,800,atk;1100:quit",
        expect=["[boss03] Gelaldy: body 1500, arms 800", "mini gauge 0 on: ", " / 800", "mini gauge 0 off"]),
    # Gelaldy's whole fight (build/boss_re/GELALDY*.md; src/cleria/boss03), from the room's retry start (flag 205 + a
    # reload: the root script places the player and starts the battle) as Yunica, with real swings: `boss3` stands
    # her by an open part (an unguarded arm, then the core once it is open) and Attack is pressed. The arms are
    # broken (RDAMAGE / LDAMAGE), the teeth open and the core is cut (body 1500 -> 1149 -> 795 -> 465 -> 111), the
    # magma and a Perrie on the way, the sweep (LNAGI) and the roll (RROLL), then the death and flag 222.
    # Written before BOSS_HULLS step 4 (the hull bound to the render bones), on the rest-pose hull.
    # Re-pinned 2026-10-10 with step 4: the arms' and hands' hulls follow the drawn ones (they stayed 10 .. 17 away,
    # at rest), so `boss3` stands her ON the slammed hand (feet y 1.59, was -1.50 on the ring) and the fight runs
    # mirrored: the left arm is broken first (LDAMAGE, then RDAMAGE), two swings reach the open core per window
    # (body 1500 -> 792 -> 459 -> 129, three windows instead of four; the first cut now lands after the first
    # Perrie, t1492, where it came before it), the sweep is the right arm's (RNAGI); the
    # same beats in the same order — arms, teeth, core, magma, a Perrie, the sweep, the roll, the death at t4739
    # (was t4938).
    "gelaldy_fight": dict(
        room="S_30/S_3099/S_3099",
        env={"CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=3;9:flag=205,1;10:saveload;"
              "40:every=20,700,ok;720:every=40,5100,boss3;728:every=40,5100,atk;5100:quit",
        expect=["[boss03] Gelaldy: body 1500, arms 800", "clip RPUNCHI", "clip LDAMAGE", "clip RDAMAGE", "clip TEETH1",
                "Perrie spawned", "body 792/1500", "body 459/1500", "clip RNAGI1", "clip RROLL", "clip DEAD",
                "flag 222 = 1"]),
    # ... and as Hugo: the mode-0 scan's sub-part pass runs for his shots' particles too (view_fx_mode0.inl), so the
    # pods and the core take them (they passed through the boss before).
    "khonsclard_hugo": dict(
        room="S_40/S_4099/S_4099", char="2",
        env={"CLERIA_BOSSLOG": "1", "CLERIA_DMG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=10;20:at=-2.8,23.3,0;"
              "40:every=20,700,ok;720:every=40,3700,boss4;728:every=40,3700,atk;3700:quit",
        expect=["[boss04] Khonsclard: body 3000, pods 600", "f156 1", "fx 130 hits khonsclard part", "states 00",
                "fx 130 hits khonsclard part 0 (DEF 175)", "flag 223 = 1", "destroyed"]),
    # FUN_004faf20's tail on the other bosses' parts (part_hit_feedback, view_game.inl): Toal's swing on a risen pod
    # (part 1, +0x138 clear) draws the white damage number, as on Velagunder's parts (the SE is not audible here).
    "boss_part_feedback": dict(
        room="S_40/S_4099/S_4099", char="3",
        env={"CLERIA_DMG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="2:diff=3;3:saveload;4:opt=DamageTaken=0;6:level=45;7:tier=8;20:at=-2.8,23.3,0;40:every=20,700,ok;"
              "720:every=12,1100,boss4;721:every=12,1100,atk;1100:quit",
        expect=["[boss04] Khonsclard: body 3750, pods 900", "boss4 -> part 1", " ffffff",
                "hit khonsclard part 1 (DEF 175)"]),
    # A Retry inside the fight (Toal, Hard): the scene load builds the boss again (boss_load, view_game.inl) — Hard's
    # body 3750 / pods 900 once more, the pods located and rising again, and the fight can be won (223). The brain
    # used to outlive the reload: its six pods were gone with the old visit and never rose, so nothing could be
    # damaged after a Retry (states 004444 for good); bosses 1-3 and 5 went on from the HP they had.
    # Re-pinned 2026-10-10 (the dust puffs' draws from the shared xorshift move the fight's course): in the new course band 5's waves pushed Toal off the last pod before the swing 8 ticks after
    # the placement landed, and the body regenerated for good (640 -> 1865 by t7000). The pilot's swing now comes 5
    # ticks after the placement; the fight is won again (223).
    "khonsclard_retry": dict(
        room="S_40/S_4099/S_4099", char="3",
        env={"CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="2:diff=3;3:saveload;6:level=50;7:opt=DamageDealt=10;8:opt=DamageTaken=0;20:at=-2.8,23.3,0;"
              "40:every=20,700,ok;720:every=40,1000,boss4;728:every=40,1000,atk;1000:opt=DamageTaken=1;1001:hp=0;"
              "1160:ok;1170:opt=DamageTaken=0;1180:every=20,1500,ok;1500:every=40,5200,boss4;"
              "1505:every=40,5200,atk;5200:quit",
        expect=["[boss04] Khonsclard: body 3750, pods 900", "[death] HP 0", "[gameover] Retry -> S_4099",
                "[boss04] Khonsclard: body 3750, pods 900", "states 00", "band 2", "flag 223 = 1"]),
    # Pictimos (PICTIMOS.md): walk into BATTLE.XSO's event box, the 700-frame intro (RAKKATYUU / TYAKUTI), the
    # fight from BATTLE_START, ZOROMEKA's Chryolos below half HP (m220_boss), the guts at HP 0 with live
    # summons (survives at 1, absorbs them), the death: EXP, flag 224 at b4 500 and the destroy.
    # Re-pinned 2026-10-09: the guts used to come from one Chryolos happening to outlive him. The pickup fixes
    # (the drops leave the enemy's origin: other RNG draws) moved the fight to a path where all 27 were dead by
    # then (no guts, a plain death). The drive now raises Damage dealt at t2180, in the middle of ZOROMEKA, so he
    # falls with 12 of 20 alive: the guts (body 1 -> 481 absorbed), then the kill.
    # Re-pinned 2026-10-10 (the dust puffs' draws from the shared xorshift): he falls with 10 of 20 alive and
    # absorbs body 1 -> 401. Same sections, the guts, the kill.
    # Re-pinned again 2026-10-10 (the blood splats' draws): 11 of 20 alive, body 1 -> 441.
    # Re-pinned 2026-10-10 (BOSS_HULLS step 2: his hull is bound to the render bones, so it is on the drawn body
    # and no longer strewn over the arena — the player stood on a stray piece at y 2.09 at t1170 and lost swings).
    # Same sections in the same order, the guts, the kill; the early swings land (body 3592 at t1230, was 3796),
    # he falls with 13 of 20 alive and absorbs body 1 -> 521; EXP at t2616 (was t2808), flag 224 at t3114 (t3306).
    "pictimos_fight": dict(
        room="S_50/S_5099/S_5099",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=2;8:tier=10;10:move=0,1,200;"
              "40:every=20,1000,ok;1030:every=40,4100,boss5;1038:every=40,4100,atk;2180:opt=DamageDealt=20;4100:quit",
        expect=["[boss05] Pictimos: body 4000, STR 220 lv 40", "sect RAKKATYUU", "sect TYAKUTI", "f156 1",
                "sect ZOROMEKA", "sect DEAD", "body 1/4000 guts 1 waves 0 summons 13/20", "body 521/4000 guts 1",
                "EXP (lv 40)", "flag 224 = 1", "destroyed"]),
    # ... and as Hugo: his shots' particles reach Pictimos's parts (part 1 takes the damage, the DEF-999 ones none).
    "pictimos_hugo": dict(
        room="S_50/S_5099/S_5099", char="2",
        env={"CLERIA_BOSSLOG": "1", "CLERIA_DMG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=2;8:tier=10;10:move=0,1,200;"
              "40:every=20,1000,ok;1030:every=40,4100,boss5;1038:every=40,4100,atk;4100:quit",
        expect=["[boss05] Pictimos: body 4000, STR 220 lv 40", "f156 1", "fx 130 hits pictimos part 1 (DEF 230)",
                "EXP (lv 40)", "flag 224 = 1"]),
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
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.45;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss6;5700:quit",
        # Re-pinned 2026-10-10 (the dust puffs: every hit on Zava takes 20 draws of the shared xorshift): the same
        # fight mirrored — she mounts head a first (orbs 4/0, thrown off it at t1884), the floor cast sends head b
        # diving (move 11), the second mount is head b (0/4, thrown off at t3927); the kill is at t4102 (was t3830)
        # and the heads are gone by t4628, so the run is 400 ticks longer.
        # Re-pinned again 2026-10-10 (the blood splats: 18 more draws after every hit she survives). Phase 2 starts at
        # t1708, and at Damage dealt 0.5 she survived the second fusion break with 428 HP
        # and went on to the big attack; at 0.45 the course is the one above again (the floor cast at t3056 with
        # 2067 HP, the kill at t4900 after the second break), with head a's bite on the way (it used to be seen in
        # zava_big_attack, whose course has none now): the two expectations moved here.
        # Re-pinned 2026-10-10 (BOSS_HULLS step 5: the heads' hulls are bound to the render bones — they sat ~3.5 under
        # the drawn heads: in S_6082 at t600 head a is drawn at y 6.41 .. 13.09, its hull was y 2.87 .. 11.02 and is
        # y 6.71 .. 12.44). The player is pushed out differently beside a head (first at t390), so the same fight
        # runs mirrored again, and sooner: phase 2 at t1189 with 4036 HP (was t1708, 4038), she mounts head b first
        # (orbs 0/4, thrown off it at t1942), the floor cast sends head a diving (move 11), the second mount is head
        # a (4/0, thrown off at t3948), the kill at t4172 (was t4900), the heads gone by t4698. Every beat of the old
        # list is here with a and b exchanged; Zava still lands at y 6.83.
        # Re-pinned 2026-10-11 (fx+0x201 as the bytes have it, issue #66: the heads' flagged effects and bullets trace
        # the room without the boss meshes; before, their particles never met the world). First difference on the
        # full-log diff at t~400: a 0-damage tick of a flagged effect no longer reaches the player, so the pilot
        # stands 0.6 away and the fourth knock-down comes one round later. Same course, same order (head b first,
        # then a), about 110 ticks later: phase 2 at t1298 with 4046 HP (was t1189, 4036), thrown off head b at
        # t2050, the floor cast at t2564, thrown off head a at t4060, the kill at t4237 (was t4172).
        expect=["[boss06] Zava's heads: a (-39.46,6.10,30.00) b (-28.82,6.10,30.00), HP 5000 STR 232 DEF 320",
                "BossCam 1", "Zava phase 1 action 1 hp 6000/6000", "jump under Zava (act 4)", "second jump",
                "air attack", "y 13.00, falling, hits left 0", "Zava phase 1 action 11", "y 6.83, grounded",
                "Zava phase 1 action 0", "Zava phase 2 action 0 hp 4046/6000", "BossCam 3", "move 9", "BossCam 4",
                "move 10", "riding, hits left 0, ride count 1, lives 0/4", "head b move 12 (pattern 2, lives 3)",
                "lives 0/2", "lives 0/1",
                "head b move 8 (pattern 2, lives 0)", "Zava is thrown off head b",
                "grounded, hits left 0, ride count 1, lives 0/0", "Zava phase 2 action 7", "Zava phase 2 action 8",
                "head a move 11", "Zava phase 2 action 12", "Zava phase 2 action 0", "head a move 9",
                "riding, hits left 4, ride count 1, lives 4/0", "holds the player", "lets the player go",
                "Zava is thrown off head a", "BossCam 6", "EXP (lv 45)",
                "flag 225 = 1", "move 15", "head b destroyed", "head a destroyed"],
        absent=["direct damage ATK 267"]),
    # ... under 25 % at her re-position the big attack instead (action 20, both heads dive = move 17, the pull to
    # the centre, the rings, the blast at frame 360): the pilot runs to the arena's edge while she casts, then holds
    # the stick outward and keeps jumping (the pull is 0.3 grounded, 0.1 airborne), and the blast's direct hit (within
    # 2.0 of the centre) does not land. A head's bite on the way: mashing Attack ends the hold (8 press ticks).
    "zava_big_attack": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        # Re-pinned 2026-10-10 (the dust puffs: every hit on Zava takes 20 draws of the shared xorshift). At Damage
        # dealt 0.6 the new course brought her to the re-position with 30 % (no big attack: action 8), so the drive
        # deals 0.7: 227 HP there, action 20. The fight is mirrored (head a first, then b), and the head's bite comes
        # on the way to the first mount (t1672) instead of the second.
        # Re-pinned again 2026-10-10 (the spark streaks: the one hit for 0 on her takes 16 draws): at 0.7 she chose the
        # move 12 over the big attack at the re-position (265 HP); at 0.75 the big attack is back (515 HP, t2251), with
        # two bites on the way to the first mount and the kill at t3736 — the run ends at t4000.
        # Re-pinned a third time 2026-10-10 (the blood splats: 18 more draws after every hit she survives): head b
        # first again, the big attack at t2219 (532 HP), the kill at t3590. No head bites the pilot on this course:
        # the bite's two expectations moved to zava_fight, where it is seen.
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.75;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss6;4000:quit",
        expect=["Zava phase 2 action 0", "head b move 9", "Zava is thrown off head b", "Zava phase 2 action 7",
                "Zava phase 2 action 20", "head a move 17", "head b move 17", "Zava phase 2 action 12",
                "Zava phase 2 action 0", "head a move 9", "Zava is thrown off head a", "flag 225 = 1"],
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
    # Dalles (build/boss_re/DALLES*.md; src/cleria/boss07, apps/cleria-view/view_boss07.inl). The room's own retry
    # start: flag 205 = 1 and a reload make the root script place the player at (0, 0, -14.5), Mesh_Locate boss07 and
    # run Battle (flag 156 at once; it then waits for 226) — the first-visit intro EV_7080 is some 1,700 frames. The
    # fight is played the way a player plays it: drive `boss7` only ever stands the player on the arena floor and
    # presses Attack / the Skill button / the Skill 1-3 keys, so every hit goes through the input path and the
    # engine's damage formula against the three GoXx parts. What a save at this point has and the direct room start
    # lacks is set by hand: the three bracelets (flags 116..118) at level 3 (flags 182..184), Skill 1 equipped, the
    # level / weapon tier; DamageTaken=0 + InfiniteHP keep the run alive, InfiniteMP removes the wait for MP between
    # casts (a barrier needs 25 hit frames of its skill), DamageDealt=1.5 scales the length.
    # Phase 0: the bare body (every class x 0.6), the hand attacks, the first bar ends: max HP +1250, the refill.
    # Phase 1: the barriers W -> F -> E -> W, each broken by 25 hit frames of its own skill class (Yunica: Skill 1 =
    # bracelet 0x74 = class 0 passes W, Skill 3 = 0x76 = class 1 passes F, Skill 2 = 0x75 = class 2 passes E; a plain
    # swing is refused: the clank), the 599-frame windows (closed early by the 34 % damage cap), the second bar ends.
    # Phase 2: the wing, the dark barrier (nothing passes), both options destroyed -> it falls (window 699), the
    # options revive, the capture circle (SIS_LAZER) takes the player, the other dimension (pinned at y 495, KIAI ->
    # METEOR) and the return, the options again, the death: flag 226, the EXP, the destroy 600 frames later, and
    # Battle's ChangeScene to S_7000.
    "dalles_fight": dict(
        room="S_70/S_7080/S_7080",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1"},
        drive="3:opt=InfiniteMP=1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=1.5;8:tier=10;"
              "9:flag=205,1;10:saveload;11:flag=116,1;12:flag=117,1;13:flag=118,1;14:flag=182,3;15:flag=183,3;"
              "16:flag=184,3;17:bracelet=116;60:boss7;7800:quit",
        expect=["[boss07] Dalles: HP 2500 STR 255, parts DEF 255 pool 25000, options 1000",
                "the fight starts at (0.00,4.00,0.00)", "target the core with the sword", "clip N_SHOOT_R (variant 0)",
                "bullet kind 0x2 effect 409", "the swing hits part 0 (class 3)", "clip N_SHOOT_L (variant 0)",
                "bullet kind 0x2 effect 398", "mode 0 -> 1", "clip N2W_CHANGE", "barrier W up",
                "(class 3): 0 dmg (clank), barrier 1", "the Skill button (bracelet 0x74", "(class 0): ",
                "phase 1 (max HP 3750)", "impact 404", "a pillar bullet", "barrier W broken (window 599)",
                "target the core with the sword", "the window closed", "barrier F up",
                "the Skill 3 key (bracelet 0x76 for barrier 3)", "(class 1): ", "bullet kind 0x2 effect 403",
                "barrier F broken (window 599)", "barrier E up", "the Skill 2 key (bracelet 0x75 for barrier 2)",
                "(class 2): ", "bullet kind 0x4 effect 400", "barrier E broken (window 599)", "CamSlot 4",
                # Re-pinned with the W attack's fix (issue #66): effect 402's delayed root em9 no longer fires an
                # impact-404 pillar bullet in mid-air at each option (its world-contact latch is the FX call's
                # origin, view_boss07.inl) — two bullets and two RNG draws fewer, so the run after it differs: the
                # second bar now ends inside the E window (no second W barrier), the dark barrier falls twice
                # before the capture and a third time after the return. `count` pins what is left of the W attack:
                # one landing per option, two world-colliding particles each (em11 and em12, both impact 404).
                # Re-pinned 2026-10-10 (the dust puffs' draws from the shared xorshift move the fight's
                # course): the same events in another order — the capture and the other dimension now come
                # under the FIRST dark barrier (t3208..3859), before both options are down; the barrier then falls
                # (t3970), stands again with the options revived, and falls twice more before the death (t5265).
                # Re-pinned 2026-10-10 (BOSS_HULLS step 6: the body's `core` and `cover_r` entries and the two options'
                # hulls are bound to the render bones — the option's hull turns with its bone, 1.8 tall instead of
                # 3.2, the core's sits 0.24 higher). The fight differs from t78 on (the pilot's place beside the body)
                # and again has the same events in another order: the first dark barrier falls with option R at t3084,
                # three ticks BEFORE the capture (t3087; the other dimension t3176..3738), it stands again with the
                # options revived (t4010), and falls twice more before the death at t5037 (was t5265).
                "barrier D up", "phase 2 (max HP 4250)", "target option", "bullet kind 0x25 effect 54",
                "clip SIS_LAZER_START", "option L destroyed", "the capture circle at", "CamSlot 3",
                "barrier D broken (window 699)", "option R destroyed", "target the core with the sword",
                "the player is captured", "CamSlot 5", "every particle removed", "mode 0 -> 4", "clip KIAI_START",
                "the player is pinned at (0.0,495.0,-5.0)", "clip METEOR", "the player is returned to (0.0,7.5,-15.0)",
                "mode 4 -> 0", "barrier D up", "option R revived (1000)", "option L revived (1000)",
                "barrier D broken (window 699)",
                "flag 226 = 1", "EXP (lv 49)", "the death starts", "[content] boss_defeated S_7080 flag 226", "[boss07] t",
                "destroyed", ">> entering S_7000"], count={"boss_defeated": 1, "impact 404 at": 4}),
    # Darm (build/boss_re/DARM*.md; src/cleria/boss08, apps/cleria-view/view_boss08.inl). Toal's last fight, from the
    # room's own retry start: flag 205 = 1 and a reload make the root script Mesh_Locate boss08 (its ctor builds the
    # tail, the core, the arms and the floor s7099yuka), place the player at (0, 0, -10) and run Battle (flag 156 at
    # once; it then waits for 227) — the first-visit intro Event_7099 is some 1,300 frames. The fight is played the
    # way a player plays it: drive `boss8` only ever stands the player on a resting floor tile beside its target and
    # presses Attack / Jump / the stick, so every hit goes through the input path and the engine's damage formula
    # against the nine GoXx parts. What a save at this point has and the direct room start lacks is set by hand: the
    # level / weapon tier and the double jump (flag 163); DamageTaken=0 + InfiniteHP keep the run alive (the swings
    # still knock the player about and off the floor: the 220-frame fall sequence), DamageDealt=2 lets one 600-frame
    # exposure cross its 20 % band (a limb hit is 1 by the min-1 rule either way: 34 part-frames empty a limb).
    # Forms Normal / Low / High: the limbs are struck until the group that holds the core has faded -> DOWN, the
    # core sinks to y 1.5 and takes the real damage for 600 frames; the band is capped (the refill), the floor
    # re-arranges into the next form (640 frames). Four bands, then OnBack: the core on the tail's eye behind the
    # barrier (60 hits of 1), the tentacles' override tracks (Stab / Pull / Beat), the 900-frame window, the kill:
    # flag 227 = 1, Battle's flag 156 = 0 and Battle_End (its windows confirmed by `oktapf`), 227 = 2 releases the
    # death timeline: the body fades out, ChangeScene to S_7000.
    "darm_fight": dict(
        room="S_70/S_7099/S_7099",
        env={"CLERIA_CHAR": "3", "CLERIA_BOSSLOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=2;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss8;61:every=15,17000,oktapf;17000:quit",
        expect=["[boss08] Darm: core HP 9999 STR 259 DEF 275, parts DEF 275 pool 99990",
                "the fight starts, the body at (0.00,0.00,8.50)", "target part 5 from the floor",
                "the swing hits part 5 (DEF 998, class 3): 2 dmg", "clip 36_DoubleLariat", "effect 512",
                "target part 7 from the floor", "CamSlot 5 (1.000)", "the player fell off the floor", "effect 517",
                "the player is put back at (0.0,19.0,-6.0)", "bullet(s) kind 0x25 effect 467", "target part 2 from the floor",
                "MatMode 1: black (Down)", "DOWN: the core is exposed (weak point the belly)", "clip 37_NormalDown",
                "target the core 0", "the swing hits the core 0 (DEF 275, class 3)", "the exposure ends, core hp",
                "band 1 (the refill starts", "CamSlot 6 (0.020)", "MatMode 2: the outline (form change)",
                "form Normal -> Low", "motion s7099yuka f", "the floor moves: over to", "the refill ends at hp",
                "motion s7099yuka c", "CamSlot 2 (0.025)", "MatMode 0: lit", "settled in Low", "clip 38_LowDown",
                "band 2 (the refill starts", "form Low -> Normal", "settled in Normal", "clip 04_RightHandSwing",
                "effect 451", "effect 490", "band 3 (the refill starts", "form Normal -> High", "motion s7099yuka e",
                "CamSlot 3 (0.025)", "settled in High", "with a jump", "clip 39_HighDown", "band 4 (the refill starts",
                "form High -> OnBack", "the barrier is up", "motion s7099yuka j", "CamSlot 4 (", "settled in OnBack",
                # Re-pinned 2026-10-11 (the jump gate reads +0x3a0): the pilot's jumps take on other ticks, so the OnBack
                # phase runs another course with the same events — the camera at the settle (CamSlot 4 (0.025), not the
                # (1.000) of a stab that catches him), the tentacle tracks in the order Beat, Stab, Pull (was Stab,
                # Pull, Beat), the barrier broken before the first stab.
                "(20_Beat)", "the swing hits the core 0 (DEF 998, class 3): 2 dmg",
                "bullet(s) kind 0x25 effect 484", "released from Bone_", "barrier 59",
                "the barrier breaks (window 900)", "the swing hits the core 0 (DEF 295, class 3)",
                "the window closed: the barrier is back", "the barrier is up", "(24_StabInTheGround)",
                "(25_PullFromTheGround)", "flag 215 = 0",
                "the kill (core hp", "motion boss08 30_OnBackWait", "flag 227 = 1", "[content] boss_defeated S_7099 flag 227",
                "the death timeline starts", "the body is gone (alpha 0)", ">> entering S_7000"],
        count={"boss_defeated": 1}),
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
    # ROUTE_F47.md B1: the floor-6 mirrors are hidden by their room roots (Mesh_SetVisible 0) and keep their
    # touch volume. Toal with the Rod (98) walks into S_6009's mirror_01 -> S_6014, then into mirror_09 -> S_6082.
    "mirror_chain": dict(
        room="S_60/S_6009/S_6009", char="3",
        drive="5:flag=98,1;20:at3=-10,22,143,0;40:move=0,1,120;420:at3=-0.1,4,106,0;440:move=0,1,120;800:quit",
        expect=["[touch] mirror_01 -> s_mirror01", ">> entering S_6014", "[touch] mirror_09 -> s_mirror09",
                ">> entering S_6082"]),
    # ROUTE_F47.md B2: S_6009's updraft EVT_UP2 (ChrAllInEventBox_SetForce with fy 0.2, run by the root once
    # the ward flag 475 is set) lifts the player from the y 8 floor; walking on lands on the y 16 ledge.
    "updraft_s6009": dict(
        room="S_60/S_6009/S_6009", char="3",
        drive="5:flag=475,1;10:saveload;40:at3=-10,8,96,0;150:move=0,1,60;260:pos;270:quit",
        expect=["t260 pos", "16.000, 103."]),
    # ROUTE_F47.md R6: S_4015's two unnamed s40step_02 platforms play their section 0 ('Default') from the load
    # (the YMO loader's section 0, FUN_005f31d0) — the only way across the y 26 gaps to switch_01. Object #6
    # (SOB index 6) slides from x -68.8 to -55.2 and carries Toal standing on it (-68.0 -> -67.3 in 40 ticks).
    "platform_s4015": dict(
        room="S_40/S_4015/S_4015", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="5:opt=DamageTaken=0;20:at3=-68,26.2,38,0;21:mesh=#6;60:pos;240:mesh=#6;250:quit",
        expect=["mesh #6 frame (-68.80, 25.00, 39.00)", "feet (-67.312, 26.000, 38.000)",
                "mesh #6 frame (-55.18, 25.00, 39.00)"]),
    # ... and the whole way on foot (Toal, Hard, the double jump): from the y 26 ledge onto the platform at its east
    # end, a double jump over each spiked rod as it rides west under them (toge_01 / toge_02, @Touch_Toge), the jump
    # north onto the narrow path, the slope down to switch_01 (451, StepMove_02), back the same way (a single jump
    # over the last rod), the lift step_01 at its top (flag 450 set by hand: switch_00 and the ride up are the
    # same mechanism), the east ledge, step_00 from 27 up to 48, the hop onto the y 49 curb and the stairs to S_4019.
    "route_s4015": dict(
        room="S_40/S_4015/S_4015", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive=("2:flag=450,1;3:diff=3;4:saveload;5:flag=163,1;6:level=27;7:opt=DamageTaken=0;200:at3=-51.6,26,38,180;"
               "234:move=-1,0,16;252:move=-1,0,16;252:jump;266:jump;315:move=1,0,10;345:move=-1,0,5;345:jump;"
               "359:jump;432:move=0,1,38;432:jump;480:move=-1,0,138;620:move=0,-1,112;741:move=-1,0,7;755:move=0,1,6;"
               "1300:move=0,1,109;1415:move=1,0,137;1440:every=22,1500,jump;1560:move=1,0,6;1630:move=0,-1,34;"
               "1630:jump;1685:move=1,0,6;1685:jump;1699:jump;1748:move=-1,0,20;1769:move=1,0,2;1769:jump;"
               "1852:move=1,0,22;1852:jump;1902:move=0,1,10;1915:move=1,0,34;1988:move=1,0,75;2030:jump;"
               "2075:move=1,1,12;2541:move=0,1,24;2541:jump;2895:move=0,1,170;2955:jump;2995:jump;"
               "3100:quit"),
        expect=["[plate] switch_01 -> turnswitch01", ">> entering S_4019", "arrival @ (0.0,-1.5,-3.0) [SEN]"],
        absent=["@touch_toge"]),
    # ROUTE_F47.md R6: S_6004's way up to the S_6052 door (Toal, Hard, the double jump). Both plates (466 / 467: the
    # four falling steps appear), then on foot from the y 6 terrace: the 8 diamond, the 12 and 16 terraces, step_01
    # and step_02 over to the inner 18, down to the west 14, the leap over the hub to the east 14, 18, 22, step_04
    # and step_03 onto the y 24 ledge, the ramp to the door.
    "route_s6004": dict(
        room="S_60/S_6004/S_6004", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive=("2:diff=3;3:flag=163,1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=44;20:killall;"
               "30:obj=switch02,0;370:killall;371:obj=switch01,0;710:killall;715:at3=-66,6,72,0;725:pos;"
               "727:move=0.7071,0.7071,80;752:jump;819:pos;820:move=-0.8717,0.4900,87;832:jump;848:jump;919:pos;"
               "920:move=0.9983,-0.0578,124;968:jump;984:jump;1056:pos;1057:move=1.0000,0.0083,223;1076:jump;"
               "1092:jump;1140:jump;1190:jump;1206:jump;1292:pos;1293:move=0.6197,-0.7848,66;1371:pos;"
               "1372:move=1.0000,0.0062,137;1406:jump;1422:jump;1521:pos;1522:move=0.5789,0.8154,101;1541:jump;"
               "1557:jump;1635:pos;1636:move=-0.1725,0.9850,90;1653:jump;1669:jump;1738:pos;"
               "1739:move=-0.9996,-0.0285,248;1770:jump;1786:jump;1834:jump;1885:jump;1901:jump;1999:pos;"
               "2000:move=-1.0000,0.0036,92;2104:pos;2105:move=0.0023,1.0000,440;2557:pos;2578:quit"),
        expect=["[plate] switch02 -> turnswitch02", "[plate] switch01 -> turnswitch01", "[plate] step_01 -> @fall_001",
                "[plate] step_02 -> @fall_001", "[plate] step_04 -> @fall_001", "[plate] step_03 -> @fall_001",
                ">> entering S_6052"]),
    # ... and its box_02 platform (y 18, walled 6 high on three sides; the fourth is the y 24 ledge's face): a double
    # jump at the face gets out. The engine's sphere rides over an edge below its centre; the view's capsule stood
    # 0.6 above the feet in the air too and stopped the jump (apex 5.37) 0.03 short: there was no way out.
    "ledge_s6004": dict(
        room="S_60/S_6004/S_6004", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="2:diff=3;3:flag=163,1;6:level=44;20:killall;30:at3=-67,18,97,0;40:move=1,0,110;46:jump;62:jump;150:pos;160:quit",
        expect=["t150 pos", "feet (-50.041, 24.000, 97.0"]),
    # Issue #58: S_3004's three unnamed s30step0 lifts play their section 0 too (y -7 <-> 0, 80 ticks at the top of a
    # 320-tick cycle; they stood at y 0 for good before). Toal, Hard, no fire skill and no torch lit: from the S_3003
    # door over lift #1 at its top (a jump on, a jump off), the two 2-wide gaps and the edge at z 29 to S_3006.
    "lift_s3004": dict(
        room="S_30/S_3003/S_3003", char="3", env=dict(CLERIA_DRIVE_LOCKSTEP="1"),
        drive="2:diff=3;5:opt=DamageTaken=0;6:level=22;10:exit=0;40:move=0.2,1,90;200:move=1,0,3;262:move=1,0,100;275:jump;"
              "322:jump;370:move=1,-0.3,66;445:move=0,1,230;461:jump;563:jump;620:jump;690:move=1,0.1,42;740:move=0,1,80;840:quit",
        expect=[">> entering S_3004", "arrival @ (0.0,0.0,-10.0) [SEN]", ">> entering S_3006"]),
    # ROUTE_F47.md B3: the last script of the game. S_0003 with 206 == 1 runs Toal's Clear_Event_2, whose
    # ChangeScene('Title\') is the title itself (it used to resolve to "(none)" and leave a black screen).
    "ending_to_title": dict(
        room="S_00/S_0003/S_0003", char="3",
        drive="5:flag=206,1;10:saveload;40:every=15,6000,ok;6000:quit",
        # On the way its EventXX(4) sets the system ini's GameFirstPlay and Toal's clear flag 299 (FUN_0058f210 case 4).
        # ... and (CLEAR_DATA.md §3.2) Toal's clear byte and the Normal tier byte, saved to record.ta2 in the save folder;
        # the title that follows reads the live bytes: Bonus Modes is on its list.
        expect=[">> entering S_0003", "[clear] EventXX(4): GameFirstPlay = 1, clear flag 299 = 1",
                "[clear] clear bytes Yunica 00 Hugo 00 Toal 01, 1 cleared tier(s); record.ta2 saved",
                "ChangeScene 'Title\\' -> the title", "Return to Title: back to the title",
                "[title] clear data: Yunica 00 Hugo 00 Toal 01, data.nya 0 -> Bonus Modes 1, Toal's bar locked",
                "[title] t60 fader 0.00 list 1 book 0"],
        files=["record.ta2"]),
    # CLEAR_DATA.md: the clear data survives the process. First boot: Yunica's ending (Normal) writes record.ta2 into
    # the save folder. Second boot, another room, the same folder: the record is read back before the title, flags
    # 297..299 are seeded from it (a new game: min(byte, 1)), and the title's gates open — Bonus Modes, and Toal's bar
    # in Character Select with no [cleria.qol] UnlockCharacters override.
    "clear_second_boot": dict(
        room="S_00/S_0003/S_0003", char="1",
        drive="5:flag=206,1;10:saveload;40:every=15,6000,ok;6000:quit",
        room2="S_10/S_1009/S_1009",
        drive2="5:flagdump;20:esctap;70:right;80:right;90:right;120:ok;160:up;200:up;230:ok;260:left;290:ok;400:quit",
        expect=["[clear] EventXX(4): GameFirstPlay = 1, clear flag 297 = 1",
                "[clear] clear bytes Yunica 01 Hugo 00 Toal 00, 1 cleared tier(s); record.ta2 saved",
                "==== second boot", "[clear] record.ta2: Yunica 01 Hugo 00 Toal 00, 1 cleared tier(s)",
                " 297=1", "[game] Return to Title: back to the title",
                "[title] clear data: Yunica 01 Hugo 00 Toal 00, data.nya 0 -> Bonus Modes 1, Toal's bar open"],
        files=["record.ta2"]),
    # BONUS_MODES.md §1, the SP bonus at a New Game: a process booted on the title (no room) with a record.ta2 of
    # three clears and two cleared tiers (Yunica Normal, Toal Hard). New Game -> Yunica -> Difficulty Normal starts
    # S_0000 with flags 297..299 = 1, whose router runs GohoubiMenu ("SP Bonus Acquisition"); [Enable] = Get_Bonus
    # = EventXX(13): 2 x 10,000 SP into a wallet that starts at 0.
    "title_new_game_sp_bonus": dict(
        room=None, records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0), (3, 2)])},
        title="40:ok;100:ok;170:ok", drive="5:flagdump;60:scripts;120:ok;200:scripts;220:quit",
        expect=["[clear] record.ta2: Yunica 01 Hugo 01 Toal 01, 2 cleared tier(s)",
                "[title] clear data: Yunica 01 Hugo 01 Toal 01, data.nya 0 -> Bonus Modes 1, Toal's bar open",
                " 297=1 298=1 299=1", "script GohoubiMenu",
                "[clear] EventXX(13): SP bonus 20000 (2 cleared tier(s)) -> SP 20000", "script Prologue_Event_1"]),
    # ... and with two clears (Yunica, Hugo) there is no menu: S_0000's three tests are nested. Toal's bar is open
    # (down, down), and his new game goes straight to its prologue.
    "title_new_game_no_sp_bonus": dict(
        room=None, records={"record.ta2": clear_data((1, 1, 0), tiers=[(2, 0), (2, 1)])},
        title="40:ok;100:down;110:down;120:ok;190:ok", drive="5:flagdump;60:scripts;120:ok;200:quit",
        expect=["[title] clear data: Yunica 01 Hugo 01 Toal 00, data.nya 0 -> Bonus Modes 1, Toal's bar open",
                " 150=3 ", " 297=1 298=1", "script Prologue_Event_0"],
        absent=[" 299=", "GohoubiMenu", "EventXX(13)"]),
    # BONUS_MODES.md §4, the Bonus Shop: three clears, data.nya, 505,000 Arena SP in record.ar2. Title -> Bonus Modes
    # -> Bonus Shop; Adol (Felghana Version) for 500,000 through its confirm window; the list comes back with the
    # three Extras, each "Insufficient SP." at 5,000 left; Cancel closes. Both record files are written at the
    # purchase, and a second boot reads the balance and the purchase back.
    "title_bonus_shop": dict(
        room=None, env=dict(CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)]), "record.ar2": [505000]},
        title="40:down;45:down;50:ok;100:down;105:down;110:ok;210:down;220:ok;290:ok;400:down;410:down;420:ok;490:ok;"
              "620:esc;700:quit",
        drive="10:quit", room2="S_10/S_1009/S_1009", drive2="10:quit",
        expect=["[clear] record.ar2: Arena SP 505000",
                "[title] clear data: Yunica 01 Hugo 01 Toal 01, data.nya 1 -> Bonus Modes 1", "[shop] opened: Arena SP 505000",
                "[shop] bought Adol (Felghana Version) for 500000: Arena SP 5000; record.ar2 + record.ta2 saved",
                "[shop] closed", "==== second boot", "[clear] record.ar2: Arena SP 5000",
                "[clear] record.ta2: Bonus Shop purchases 0x001"],
        count={"[shop] bought": 1}),
    # STAGE_SELECT.md, the Time Attack stage select from a title boot: three clears, a record.ta with five of Yunica's
    # times. Title -> Bonus Modes -> Time Attack -> Yunica -> Normal opens the stage select (13 rows, the cursor on
    # the Boss Rush); Cancel (up from row 0) writes record.ta and record.tae. The second boot reads the five times
    # back, and Audio > Clear Time Attack records deletes both files and empties the tables.
    "title_time_attack_select": dict(
        room=None,
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)]),
                 "record.ta": record_table(15, {(1, 1, 0): (754321, 0), (1, 1, 1): (61234, 0), (1, 1, 2): (3723004, 1),
                                                (1, 0, 1): (59983, 0), (1, 2, 5): (100000, 0)})},
        title="40:down;45:down;50:ok;100:ok;170:ok;240:ok;300:shot=stage;310:up;320:ok;400:quit",
        drive="10:quit", room2="S_10/S_1009/S_1009", drive2="5:cleardata=ta;10:quit",
        expect=["[clear] record.ta: 5 time(s)", "[title] Time Attack: Character Select",
                "[stage] Time Attack stage select: Yunica, column 1, 13 row(s), cursor 0", "shot stage.ppm",
                "[stage] Cancel: record.ta + record.tae saved", "==== second boot", "[clear] record.ta: 5 time(s)",
                "[clear] Time Attack records cleared: 2 file(s) deleted, 0 time(s) left"]),
    # BONUS_MODES.md §2, a Boss Rush from a title boot (Yunica, Normal, row 0): the new-game init in the mode enters
    # S_1080 at stage 1, whose flag-193 block runs @Flag_Inv (preset 1: SetLevel(5)) and Battle_1. Vagullion falls to
    # the duel pilot (the shell's one-hit kills), flag 156 drops, and 181 ticks later the controller records the
    # stage and enters stage 2, S_1099 (preset 2: level 8), where the Velagunder pilot (`boss` + `atk`, the shell's
    # Damage dealt x10) wins again: stage 3 is S_2080 at level 12 with its duel armed. No boss_defeated reaches the mods.
    "time_attack_boss_rush": dict(
        room=None, env=_LOCK, records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)])},
        title=_TA_YUNICA + "300:ok",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;12:opt=DamageDealt=10;30:every=7,200,duel=50;300:every=12,2000,boss;"
              "301:every=12,2000,atk;2400:flagdump;2410:scripts;2415:dflags;2420:quit",
        expect=["[stage] row 0 (Boss Rush): the run starts in S_1080", "[script] SetLevel(5): HP 83", "cast 50 hp 1860/1860",
                "stage 1 (Vagullion) won in", "(a record)", "[run] next stage 2 -> S_1099", ">> entering S_1099",
                "[script] SetLevel(8): HP 100", "stage 2 (Velagunder) won in", "[run] next stage 3 -> S_2080",
                ">> entering S_2080", "[script] SetLevel(12): HP 123", " 193=1 194=1 195=3", "script Battle_0", " 156=1 "],
        absent=["boss_defeated S_", "[gameover]", "the run ends"]),
    # A single stage (row 1, Vagullion) to its end: the stage's record, the run ends (record.ta + .tae written), the
    # title comes back on the stage select with the cursor on the row and the new time in the pulse (the shot);
    # Cancel writes the tables again, and a second boot reads the one time back.
    "time_attack_stage_record": dict(
        room=None, env=_LOCK, records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)])},
        title=_TA_YUNICA + "300:down;310:ok;420:shot=back;440:esc;500:quit",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;30:every=7,200,duel=50",
        room2="S_10/S_1009/S_1009", drive2="10:quit",
        expect=["[stage] row 1 (Vagullion): the run starts in S_1080", "stage 1 (Vagullion) won in", "(a record)",
                "[run] the run ends: record.ta + record.tae saved", "[game] Return to Title",
                "[stage] Time Attack stage select: Yunica, column 1, 13 row(s), cursor 1", "shot back.ppm",
                "[stage] Cancel: record.ta + record.tae saved", "==== second boot", "[clear] record.ta: 1 time(s)"],
        absent=["[run] next stage", "boss_defeated S_"], files=["record.ta", "record.tae"]),
    # BONUS_MODES.md §2.5, the game over in a run: no gameover.dds, the same two rows. Retry reloads the stage's room
    # from the continue snapshot its battle script took (the loadout runs again, retries 1, the stage and the mode
    # kept); Return to Title leaves the run with no record and reopens the stage select.
    # The pause book does not open in a run (`esctap` at t350 [BYTES 0x410728]: with the book up the presses below would go to it).
    "time_attack_gameover": dict(
        room=None, env=_LOCK, records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)])},
        title=_TA_YUNICA + "300:ok;430:esc;470:quit",
        drive="10:opt=OneHitKill=1;30:every=7,200,duel=50;350:esctap;400:hp=0;560:ok;700:flagdump;800:hp=0;960:down;970:ok",
        expect=[">> entering S_1099", "[death] HP 0", "[gameover] Retry / Return to Title (a run: no card)", "[gameover] chose retry",
                ">> entering S_1099", "[gameover] Retry -> S_1099", "retries 1", "[script] SetLevel(8): HP 100",
                " 193=1 194=1 195=2 196=1", "[death] HP 0", "[gameover] chose title", "[game] Return to Title",
                "[stage] Time Attack stage select: Yunica, column 1, 13 row(s), cursor 0", "[stage] Cancel"],
        absent=["the run ends", "stage 2 (Velagunder) won"]),
    # ARENA.md, the Arena loop from a title boot (Yunica, Normal, Stage 1 "Wailing Blue"; 3,400 Arena SP in record.ar2):
    # the launch enters S_9109, InitArena places her at the start (the snapshot's pose) at level 1, ArenaMain's 13
    # 0x18E ops spawn its 8 waves (104 monsters) as the pilot clears them (one-hit kills; a `killall` every 700 ticks
    # for what the ground pilot cannot reach), 0x18F banks the SP held (Normal: x3) into the wallet and sets the
    # record; the title comes back on the stage select with the time (the shot). Cancel, then the Bonus Shop: Arena
    # Stage 2 for 5,000 of the 5,002. A second boot reads the balance and the purchase back.
    "arena_stage1_bank_shop": dict(
        room=None, env=dict(_LOCK, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)]), "record.ar2": [3400]},
        title="1:keys;40:down;45:down;50:ok;100:down;110:ok;190:ok;270:ok;520:ok;640:shot=back;660:esc;720:down;730:ok;830:ok;"
              "900:ok;1000:esc;1060:quit",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;130:every=9,3000,nearest=1.2;133:every=9,3000,atk;700:every=700,3000,killall",
        room2="S_10/S_1009/S_1009", drive2="10:quit",
        expect=["[title] Arena Mode: Character Select", "[stage] Arena stage select: Yunica, column 1, 1 row(s), cursor 0",
                "[stage] row 0 (Wailing Blue): the run starts in S_9109", "[script] SetLevel(1): HP 60",
                "[cont] snapshot S_9109 (-0.09, 2.00, 41.98)", "[arena] Arena(cast 14, level 0, count 0): 8 spawned",
                "[arena] Arena(cast 53, level 8, count 0): 8 spawned", "[arena] ArenaSave: SP 1602 banked, Arena SP 5002; record.ar2 saved",
                "stage 0 (Wailing Blue) cleared in 1581 frames = 00:26:350 (a record)",
                "[arena] the run ends: record.ar + record.are saved", "[game] Return to Title",
                "[stage] Arena stage select: Yunica, column 1, 1 row(s), cursor 0", "shot back.ppm",
                "[stage] Cancel: record.ar + record.are saved", "[shop] opened: Arena SP 5002",
                "[shop] bought Arena Stage 2 for 5000: Arena SP 2; record.ar2 + record.ta2 saved",
                "==== second boot", "[clear] record.ar2: Arena SP 2"],
        count={"[arena] Arena(": 13}, absent=["[gameover]"], files=["record.ar", "record.are", "record.ar2"]),
    # ARENA.md §5.1, the game over in an Arena run (Hugo, Stage 1): the pilot earns SP in the first waves; the pause
    # book does not open (`esctap`); at the death no card, Retry / Return to Title. Retry is InitArena's snapshot: the
    # room again, SP 0, level 1, the clock from 0, wave 1 (the first 0x18E a second time), retries 1. Return to Title
    # banks nothing (the wallet's 7 is read back by the second boot) and reopens the stage select.
    "arena_gameover": dict(
        room=None, char="2", env=dict(_LOCK, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)]), "record.ar2": [7]},
        title="1:keys;40:down;45:down;50:ok;100:down;110:ok;158:down;190:ok;270:ok;520:ok;650:esc;700:quit",
        drive="10:opt=OneHitKill=1;130:every=9,290,nearest=1.2;133:every=9,290,atk;295:stats;296:esctap;300:hp=0;460:ok;600:stats;"
              "601:flagdump;800:hp=0;960:down;970:ok",
        room2="S_10/S_1009/S_1009", drive2="10:quit",
        expect=["[stage] Arena stage select: Hugo", "the run starts in S_9109", "[arena] Arena(cast 14, level 0, count 0): 8 spawned",
                "t295 stats", "[death] HP 0", "[gameover] Retry / Return to Title (a run: no card)", "[gameover] chose retry",
                ">> entering S_9109", "[gameover] Retry -> S_9109 (-0.09, 2.00, 41.98), retries 1", "[script] SetLevel(1): HP 60",
                "[arena] Arena(cast 14, level 0, count 0): 8 spawned", "stats exp 0 lv 1", " sp 0", " 193=1 194=1 196=1",
                "[death] HP 0", "[gameover] chose title", "[game] Return to Title", "[stage] Arena stage select: Hugo",
                "[stage] Cancel", "==== second boot", "[clear] record.ar2: Arena SP 7"],
        absent=["ArenaSave", "the run ends", "[book]"]),
    # ARENA.md §2.3, a late stage's entry and its SP scale (Toal, Hard, Stage 6 "Demonic Core"): level 41, the first
    # wave's two ops (Liliborad x4 at 41, Bazeraena x6 at 42); a kill credits x5 on Hard (FUN_004fa390's mode-2 term).
    "arena_stage6_entry_hard": dict(
        room=None, char="3", env=dict(_LOCK, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[5, 6, 7, 8, 9]), "record.ar2": [0]},
        title="1:keys;40:down;45:down;50:ok;100:down;110:ok;158:down;166:down;190:ok;250:down;270:ok;"
              "432:down;434:down;436:down;438:down;440:down;520:ok;700:quit",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;100:actors;130:every=9,400,nearest=1.2;133:every=9,400,atk;410:stats;420:quit",
        expect=["[stage] Arena stage select: Toal, column 2, 6 row(s), cursor 0", "[stage] row 5 (Demonic Core): the run starts in S_9159",
                "[script] SetLevel(41): HP 290", "[arena] Arena(cast 79, level 41, count 4): 4 spawned",
                "[arena] Arena(cast 71, level 42, count 6): 6 spawned", "t410 stats"]),
    # ARENA.md §2.4, Stage 7 "???" on the Pikkartons' and the Pikkardon's own brains (M320.md / M330.md; they ran
    # Riffligan's): level 60, two ops of eight Pikkartons, then the one Pikkardon with its boss gauge; the pilot's
    # one-hit kills clear it and the SP is banked.
    "arena_stage7": dict(
        room=None, env=dict(_LOCK, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[5, 6, 7, 8, 9, 10]), "record.ar2": [0]},
        title="1:keys;40:down;45:down;50:ok;100:down;110:ok;190:ok;270:ok;432:down;434:down;436:down;438:down;440:down;442:down;"
              "520:ok;700:quit",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;130:every=9,3000,nearest=1.2;133:every=9,3000,atk;300:every=5,3000,gauge",
        expect=["[stage] row 6 (???): the run starts in S_9159", "[script] SetLevel(60): HP 400",
                "[arena] Arena(cast 36, level 0, count 0): 8 spawned", "[arena] Arena(cast 36, level 0, count 0): 8 spawned",
                "[arena] Arena(cast 37, level 0, count 1): 1 spawned", "gauge 1 id 37 hp 15200/15200",
                "[arena] ArenaSave: SP 168474 banked", "stage 6 (???) cleared in 717 frames", "[arena] the run ends"],
        count={"[arena] Arena(": 3}),
    # DIFFICULTY.md G2 / G8 on Hard (diff=3, then a reload so the room spawns under it): a regular Rif's 60 HP
    # is 72 and the Normal 112 is 134 (FUN_004202e0's x1.2); the hit that takes 22 on Normal takes
    # 22 * 1.5 + 1 = 34 (FUN_004fa740's tail).
    "diff_hard": dict(
        room="S_10/S_1001/S_1001",
        drive="3:diff=3;4:saveload;20:actors;30:hurt=60;40:quit",
        expect=["id 14 cast 14 enemy hp 72", "id 19 cast 19 enemy hp 134", "Chr_Damage player atk 60 -> HP 26"]),
    # ... and Very Easy (G1: flags 143 AND 203): HP x0.75 = 45, the same hit halved to 11.
    "diff_very_easy": dict(
        room="S_10/S_1001/S_1001",
        drive="3:diff=0;4:saveload;20:actors;30:hurt=60;35:flagdump;40:quit",
        expect=["id 14 cast 14 enemy hp 45", "Chr_Damage player atk 60 -> HP 49", " 143=1 ", " 203=1"]),
    # ... and Normal stays: 60 HP, the hit takes 22. A kill credits the victim's +0xc4 SP (4 for a level-1
    # Rif, FUN_004202e0 0x420505..) through FUN_004fa390: four kills, +16.
    "diff_normal_kill_sp": dict(
        room="S_10/S_1001/S_1001",
        drive="4:saveload;8:opt=OneHitKill=1;20:actors;21:stats;30:hurt=60;31:opt=DamageTaken=0;"
              "41:every=12,300,nearest=;42:every=12,300,atk;400:stats;410:quit",
        expect=["id 14 cast 14 enemy hp 60", "id 19 cast 19 enemy hp 112", "hp 60/60 sp 0",
                "Chr_Damage player atk 60 -> HP 38", "defeated 1 (6 left)", "hp 38/60 sp 18"]),   # 18 (a new game starts at 0 SP) since FIX_SLOTS S6 (was 16): a 2-SP drop in reach is collected under the window's Fix(0) held from t98
    # STATUS_AILMENTS.md (issue #60): poison 900 ticks, a 1-HP tick each 120 (max(1, 60 / 150)) with the magenta
    # number; heavy walks (3.36 in 60 ticks, a run does 9.4); confuse reverses the stick (back to the start);
    # curse lands and the Blue Necklace (accessory 55) ends it on the next tick.
    "ailments": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="10:ail=1;11:ailstate;135:ailstate;140:ail=8;141:pos;142:move=1,0,60;203:pos;204:ail=0x10;"
              "205:move=1,0,60;266:pos;270:ail=0x20;271:ailstate;272:acc=55;274:ailstate;300:quit",
        expect=["[ail] t10 poison for 900 ticks", "mask 0x01 timers 899 0 0 0 fx 1000 hp 60", "[popup] 1 ff00ff",
                "mask 0x01 timers 775 0 0 0 fx 1000 hp 59", "heavy for 300 ticks", "feet (0.055, 2.000, 7.800)",
                "feet (3.415, 2.000, 7.800)", "confuse for 600 ticks", "feet (0.055, 2.000, 7.800)",
                "curse for 1200 ticks", "mask 0x39 timers 639 169 533 1199 fx 1111", "curse ends",
                "mask 0x19 timers 636 166 530 0 fx 1110"]),
    # ... and they ride the save record (0x76c15c..0x76c16c = g_flags 528..532, inside the 0x1fc8 block): poison
    # and curse are still on, with their timers and effects, after a save + load (they were cleared: mask 0x00).
    "ailments_saveload": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="10:ail=1;11:ail=0x20;12:ailstate;20:saveload;21:ailstate;60:ailstate;70:quit",
        expect=["mask 0x21 timers 898 0 0 1199 fx 1001 hp 60", "[drive] loaded S_1009",
                "mask 0x21 timers 889 0 0 1190 fx 1001 hp 60", "mask 0x21 timers 850 0 0 1151 fx 1001 hp 60"]),
    # The Boost gauge 0x76c1b4 is in the block too: a load brings back the saved 12486 (it kept the 14878 of the
    # moment of the load).
    "boost_saveload": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="10:boost=12486;11:abil;12:csave=0;20:boost=14878;21:abil;30:cload=0;60:abil;70:quit",
        expect=["[abil] t11 sec 0x2 mp 3000 lag 3000 boost 12486", "[abil] t21 sec 0x2 mp 3000 lag 3000 boost 14878",
                "[abil] t60 sec 0x2 mp 3000 lag 3000 boost 12486"]),
    # BOOST_SKILLS.md F10: Toal's Boost-end effect 172 is the player's (side 0). It used to spawn on the enemy
    # side and take 2 HP off Toal himself (hp 60 -> 58 by t127).
    "toal_boost_end_fx": dict(
        room="S_10/S_1009/S_1009", char="3", env={"CLERIA_DMG": "1"},
        drive="3:flag=187,1;12:boost=30000;20:boostkey;21:abil;100:boost=30;110:abil;180:stats;190:quit",
        expect=["boosting 1 iframes 59", "[abil] t110", "boosting 0", "t180 stats", "hp 60/60"],
        absent=["fx 172 ("]),
    # BOOST_SKILLS.md §3.5: Toal's Terra (bracelet 117, section 0x10c) pulls the Riffligan 6 units away to the
    # point 2.0 in front of him (from (-36.5, 6.6) to (-40.4, 5.5); Toal stands at (-42.26, 4.98)), where the
    # skill's two hits land; each landed hit gives +500 Boost (1089 = 2 x 500 + the passive fill).
    "toal_terra_pull": dict(
        room="S_10/S_1001/S_1001", char="3",
        drive="5:opt=InfiniteHP=1;6:flag=187,1;8:bracelet=117;9:flag=117,1;10:flag=183,1;40:nearest=6;42:actors;"
              "43:skill+;45:skill-;47:abil;95:abil;96:actors;120:quit",
        expect=["id 187 cast 14 enemy hp 60 g1 0 pos (-36.5,-4.0,6.6)", "[abil] t47 sec 0x10c mp 2300",
                "[abil] t95 sec 0x2 mp 2300 lag 2400 boost 1089", "combo 2/",
                "id 187 cast 14 enemy hp 39 g1 0 pos (-40.4,-4.0,5.5)"]),
    # BOOST_SKILLS.md §3.3: a hit inside Toal's Ignis (bracelet 118, 0x10e) while its Marker is 0 costs 200 MP
    # and is a GUARD (0 damage); with under 200 MP the hit is x 0.75 (the 21 becomes 15) and gives 16 i-frames.
    "toal_ignis_armor": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="8:bracelet=118;9:flag=118,1;10:flag=184,1;43:skill+;45:skill-;47:abil;48:hurt=60;50:abil;130:mp=700;"
              "131:skill+;133:skill-;135:mp=100;136:hurt=60;138:abil;200:quit",
        expect=["[abil] t47 sec 0x10e mp 2400", "[popup] 0 ff0000", "Chr_Damage player atk 60 -> HP 60",
                "[abil] t50 sec 0x10e mp 2200", "iframes 15", "[popup] 15 ff0000", "Chr_Damage player atk 60 -> HP 45",
                "[abil] t138 sec 0x10e", "iframes 15"]),
    # BOOST_SKILLS.md §3.6: Ignis cast in the air (after a double jump) falls slowly: 40 ticks after the cast
    # Toal is still 3 units above the floor (y 2.0) he would have reached in ~25.
    "toal_ignis_slow_fall": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="8:bracelet=118;9:flag=118,1;10:flag=184,1;11:flag=163,1;40:jump;52:jump;60:pos;61:skill+;63:skill-;"
              "64:abil;80:pos;100:pos;130:quit",
        expect=["feet (0.055, 6.766, 7.800)", "[abil] t64 sec 0x10e mp 2400", "feet (0.055, 7.213, 7.800)",
                "feet (0.055, 6.003, 7.800)"]),
    # BLESSINGS.md §3.1 (issue #45): the standing HP regeneration of a flag-209 room (S_1100): after 300 idle
    # ticks, max HP / 60 every 24 (30 -> 32 by t340, 34 by t400); nothing in S_1009 (no flag 209, no accessory).
    "standing_regen": dict(
        room="S_11/S_1100/S_1100", char="3",
        drive="10:hp=30;340:stats;400:stats;410:quit",
        expect=["hp 32/60", "hp 34/60"]),
    # ... with the blessing bit 0 (flag 217 = 1): the delay 150 and the period 12 (34 by t200, 51 by t400).
    "standing_regen_blessed": dict(
        room="S_11/S_1100/S_1100", char="3",
        drive="5:flag=217,1;10:hp=30;200:stats;400:stats;410:quit",
        expect=["hp 34/60", "hp 51/60"]),
    "standing_regen_none": dict(
        room="S_10/S_1009/S_1009", char="3",
        drive="10:hp=30;400:stats;410:quit",
        expect=["hp 30/60"]),
    # BOOST_SKILLS.md §2.5: Hugo's super (the second Boost press with flag 188) arms a 420-tick barrier: a hit
    # 100 ticks after the press is a GUARD (0 damage), a hit past the timer takes HP.
    "hugo_super_barrier": dict(
        room="S_10/S_1009/S_1009", char="2",
        drive="3:flag=187,1;4:flag=188,1;12:boost=30000;20:boostkey;90:boostkey;91:abil;190:hurt=60;191:stats;"
              "520:hurt=60;521:stats;530:quit",
        expect=["[abil] t91 sec 0x12c", "[popup] 0 ff0000", "Chr_Damage player atk 60 -> HP 60", "t191 stats",
                "Chr_Damage player atk 60 -> HP 38", "t521 stats"]),
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


# Looping SEs (build/audit/SOUNDS.md root cause 2). A record with the EFFECTDEF `loop` flag plays until it is
# stopped; the scene entry FUN_00431840 fades every SE but the `manual` records out over 1.5 s (FUN_005ce430(0),
# 0x431ca6). The `[sfx] loops:` line lists the looping ids whenever the set changes (CLERIA_SFXLOG).
CASES.update({
    # S_4004's script plays 618 env/v_sand_stream and never stops it: the door to S_4001 does, 90 ticks on,
    # while S_4001's own 751 event/e_l4_07 starts.
    "sfx_loop_scene_stop": dict(
        room="S_40/S_4004/S_4004", env=dict(_LOCK, CLERIA_SFXLOG="1"),
        drive="200:exit=0;320:quit",
        expect=["[sfx] t1 loops: 618\n", ">> entering S_4001", "[sfx] t201 loops: 618 751\n", " loops: 751\n"]),
    # The charge level-up SEs (FUN_0046cf20's tail, 0x46d2c4..0x46d308): level 1 plays 11 s_chargemax, level 2
    # the character's own (Toal: 793 s_thor_max). `[se]` = a 2-D play request (CLERIA_SFXLOG).
    "sfx_charge_levels": dict(
        room="S_10/S_1002/S_1002", char="3", env=dict(_LOCK, CLERIA_SFXLOG="1"),
        drive="8:bracelet=118;9:flag=118,1;10:flag=184,1;30:skill+;200:skill-;230:quit",
        expect=["t30 skill+", "[se] play 11 level 1.000", "[se] play 793 level 1.000", "t200 skill-"],
        count={"[se] play 11 ": 1, "[se] play 793 ": 1}),
    # Surface footsteps of a scripted actor with Chr_FootSound on (FUN_0045c490's countdown, FUN_0045d600): the
    # S_1080 duel's opponent walks in on stone, one step every 21 ticks, the four variants 44..47 in turn, at her.
    "sfx_npc_footsteps": dict(
        room="S_10/S_1080/S_1080", char="2", env=dict(_LOCK, CLERIA_LEVEL="30", CLERIA_SFXLOG="1"),
        drive="21:move=0,-1,300;320:every=20,1200,ok;1210:quit",
        expect=["[sfx] t1040 44 at (-0.00, -18.00, -27.44)", "[sfx] t1061 45 at", "[sfx] t1082 46 at",
                "[sfx] t1103 47 at", "[sfx] t1124 44 at"]),
    # 603 env/v_cave is a manual record: it plays on through the door, and S_2000's script stops it (Se_Stop).
    "sfx_loop_manual_bed": dict(
        room="S_20/S_2001/S_2001", env=dict(_LOCK, CLERIA_SFXLOG="1"),
        drive="200:exit=0;290:loops;420:quit",
        expect=["[sfx] t1 loops: 603\n", ">> entering S_2000", "[drive] t290 loops: 603\n", " loops:\n"]),
})

# Playtest 1, effects (build/audit/PLAYTEST_1_FX.md).
CASES.update({
    # The Defense / MP drops' auras (FUN_00463f40: EF2 0x25 adol_defup, 0x26 adol_mpup), attached at the player's
    # origin; one spawn each, alive at t400, killed with the buff 1800 ticks after its drop (FUN_00462c40).
    "fx_buff_auras": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="20:drop=74;300:drop=76;400:fxall;2300:fxall;2310:quit",
        expect=["[buff] aura 37 on", "[buff] aura 38 on", "t400 fxall",
                "[fxall] id 37 attach 2", "rel (0.00, 0.00, 0.00) bias 0.00 #buff_def",
                "[fxall] id 38 attach 2", "#buff_mp", "t2300 fxall"],
        count={"[buff] aura 37 on": 1, "[buff] aura 38 on": 1, "#buff_def": 1, "#buff_mp": 1}),
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
              "95:near=17;96:atk;120:pickups;150:at=34.3,-38.5,0;175:fxall;180:quit",
        # Re-pinned 2026-10-09: the drops leave the enemy's ORIGIN (enemy+0x50, 0x4785b5), not its feet, so at t120
        # the recovery drop is still in the air (it rested on the floor by then) and is collected there.
        # Re-pinned 2026-10-10, twice. anime_GenerateDebris: cast 0x11's death section throws its debris 9 ticks after
        # the kill (30 draws at t110), so the drops' later bounce rolls differ. The dust puffs (FUN_00584550): each of
        # the four hits takes 20 draws before the kill's drop rolls, so the same five drops (66, 120 x 3, 121) leave
        # with other launch rolls: the recovery drop at t120 is at (32.16, -3.24, -37.99), was (32.30, -3.39, -37.96),
        # and the first collected pickup's z_item.eft at (31.86, -3.95, -37.68), was (32.13, -3.59, -37.54).
        # Re-pinned a third time 2026-10-10 (the blood splats: 18 draws on the tick after each hit a monster survives
        # and 18 at its death, before the debris): the recovery drop now flies to (34.45, -38.53), out of reach of
        # where the last `near` left Yunica, so the drive walks her there (`at` at t150) and reads the effects at t175.
        expect=["[pickup] id 66 pos (33.02, -3.15, -38.35) ground -4.078 rest 0", "sprite (33.02, -2.91, -38.45)",
                "shadow y -4.007", "[fxall] id 63 attach 2 at", "rel (0.00, -1.00, 0.00) bias 5.00",
                "[fxall] id 72 attach 0 at (33.77, -3.26, -38.45)"]),
    # 4. Eft_LocateChr (0x118) under a Fix: the attach reads the actor's current +0x20, so the smith
    # script's player light follows the player the opening moved (it sat at the world origin).
    "fx_locate_under_fix": dict(
        room="S_10/S_1000/S_1000", env=_LOCK,
        drive="20:run=TalkC260_Weapon_Sub;60:rep=30,40,ok;699:pos;700:fx;710:quit",
        expect=["feet (0.000, -2.997, -23.557)", "'Player_Light' id 50 alpha 1.000 -> 1.000 attach 1 at (0.00, -1.92, -23.56)",
                "'Player_Light2' id 63 alpha 1.000 -> 1.000 attach 1 at (0.00, -1.92, -23.56)"]),
})

# The Hugo playtest batch of 2026-10-08 (issues #69..#75).
CASES.update({
    # #74: a Fix does not stop the player's motion or the GoPopups (FUN_0046f980 zeroes the input and still
    # runs the mover; FUN_00565170 has no fix gate). Hugo's second shot (0x103) and the first shot's damage
    # number are live when @ItemCannotUse takes its Fix: the section plays out to idle, the number ages out.
    "fix_motion_popups": dict(
        room="S_10/S_1002/S_1002", char="2", env=_LOCK,
        drive="20:near=17;21:atk;36:atk;39:run=@itemcannotuse;40:scripts;41:pos;150:scripts;151:pos;160:quit",
        expect=["t40 scripts", "frozen 1 fix 0/1 popups 1", "sec 0x103", "t150 scripts", "frozen 1 fix 0/1 popups 0",
                "sec 0x2"]),
    # build/mech_re/FIX_SLOTS.md S0: the two fix slots, reported. @ItemCannotUse holds slot 1 (it stops the monsters
    # too), the blessing shop @GrowMenu slot 0 (the controls only).
    "fix_slots_reported": dict(
        room="S_10/S_1002/S_1002", char="2", env=_LOCK,
        drive="20:run=@itemcannotuse;22:scripts;60:quit",
        expect=["[fix] t", "slot 1 taken: 0/1", "frozen 1 fix 0/1"], absent=["slot 0 taken"]),
    "fix_slots_reported_0": dict(
        room="S_10/S_1004/S_1004", env=_LOCK,
        drive="20:run=@growmenu;22:scripts;60:quit",
        expect=["[fix] t", "slot 0 taken: 1/0", "frozen 1 fix 1/0"], absent=["slot 1 taken"]),
    # FIX_SLOTS.md S2, issue #70: Hugo's Eyes of Fact are handler 83, which stops on slot 1 only. S_1013's
    # s_Evt1013_2 holds Fix(0) while the wind pulls Hugo from z -1.27 to the pit centre (0, 0, 8): his two eyes
    # (cast 176) go with him (they stood at z -2.1 for the whole scene). Under @ItemCannotUse's Fix(1) they hold.
    "eyes_follow_fix0": dict(
        room="S_10/S_1013/S_1013", char="2", env=_LOCK,
        drive="20:move=0,1,200;100:every=20,700,ok;290:actors;590:actors;595:pos;700:quit",
        expect=["slot 0 taken: 1/0", "t290 actors", "id 176 cast 176 npc hp 1 g1 0 pos (0.7,0.2,-1.8)",
                "id 186 cast 176 npc hp 1 g1 0 pos (-0.9,0.2,-1.8)", "t590 actors",
                "id 176 cast 176 npc hp 1 g1 0 pos (0.8,0.2,7.3)", "id 186 cast 176 npc hp 1 g1 0 pos (-0.8,0.2,7.3)",
                "8.000) O2"]),
    "eyes_hold_fix1": dict(
        room="S_10/S_1002/S_1002", char="2", env=dict(_LOCK, CLERIA_AI="1"),
        drive="10:move=1,0,20;36:actors;39:run=@itemcannotuse;40:scripts;150:actors;160:quit",
        expect=["t36 actors", "id 176 cast 176 npc hp 1 g1 0 pos (1.9,0.2,-1.4)", "slot 1 taken: 0/1", "frozen 1 fix 0/1",
                "t150 actors", "id 176 cast 176 npc hp 1 g1 0 pos (1.9,0.2,-1.4)", "id 195 cast 176 npc hp 1 g1 0 pos (1.9,0.2,0.1)"],
        absent=["[ai] #176"]),      # no handler tick of an eye at t60 / t120, under the Fix(1) taken at t39
    # FIX_SLOTS.md S3: a jump caught by a Fix goes on. Hugo jumps at t30; @ItemCannotUse takes slot 1 at t39 with him
    # 2 up and rising: he rises on (2.339, 2.528), falls, lands at t65 and the jump section 3 plays out to idle.
    # (The frozen branch used to snap him to the floor at once and hold section 3 for the whole Fix.)
    "fix_airborne_lands": dict(
        room="S_10/S_1002/S_1002", char="2", env=_LOCK,
        drive="30:jump;38:pos;39:run=@itemcannotuse;41:pos;50:pos;60:pos;100:pos;160:quit",
        expect=["t38 pos", "feet (-0.005, 2.002, -0.640)", "sec 0x3", "slot 1 taken: 0/1", "t41 pos",
                "feet (-0.005, 2.339, -0.640)", "t50 pos", "feet (-0.005, 2.528, -0.640)", "t60 pos",
                "feet (-0.005, 1.823, -0.640)", "[fix] t65 the player lands under the Fix at y -0.048 (section 0x3)",
                "t100 pos", "feet (-0.005, -0.098, -0.640)", "sec 0x2"]),
    # #72: after the S_1080 Hugo duel Epona's hurt timer (+0x238) runs out under the scene's Fix (it stood at
    # 1.2: the hurt specular stayed on, a red Epona), and her exit run plays the run section 1 (tag 302): the
    # locomotion change takes the motion back from the Chr_Animate(201, 3) of her leap (FUN_0045f4d0).
    "duel_s1080_epona_exit": dict(
        room="S_10/S_1080/S_1080", char="2", env=dict(_LOCK, CLERIA_LEVEL="30"),
        drive=_S1080.format(end=7400, cast=171) + "4701:actors;7221:chr=201;7421:quit",
        expect=["t4701 actors", "[actor] id 201 cast 171 npc hp 0 g1 0 pos (0.2,-18.0,-9.7) hurt 0.000",
                "[chr] 201", "run 1 pin -1/0 motion -1 aso 1/302/"]),
    # #75: S_1013 without the Blue Necklace (Hugo). s_Evt1013_2 holds Fix(0) while EvtWindArizigo's
    # ChrAll_SetPointForce (0x15f) pulls the player to the pit centre (0, 0, 8): the push is applied under
    # the Fix (FUN_0045a510 has no fix gate on +0x5d0). He stood at z -1.27 for the whole scene.
    "pull_s1013_no_necklace": dict(
        room="S_10/S_1013/S_1013", char="2", env=_LOCK,
        drive="20:move=0,1,200;100:every=20,700,ok;300:pos;595:pos;700:quit",
        expect=["t300 pos", "-1.269) O2", "t595 pos", "8.000) O2", ">> entering S_1009"]),
})

# The mod API added at level 4 (LUA_API.md §11), each proven through a generated Lua mod (`lua`: its main.lua,
# manifest ApiVersion 3: an older mod keeps running and sees the new calls).
_LUA_EV = ('for _, k in ipairs({"boss_defeated", "item_used", "door_opened"}) do cleria.events.on(k, function(e) '
           'cleria.log(k .. " room " .. e.room .. " flag " .. tostring(e.flag) .. " item " .. tostring(e.item) .. '
           '" script " .. tostring(e.script)) end) end\n')
_LP = "[lua:test.lua] "
_LUA_SAVE = ('cleria.command("sv", function(a) cleria.log("save " .. a .. " -> " .. tostring(cleria.game.save(tonumber(a)))) end)\n'
             'cleria.command("put", function(a) cleria.save.set("n", tonumber(a)) end)\n'
             'cleria.command("fl", function(a) cleria.log("flag " .. a .. " = " .. cleria.game.flag(tonumber(a)) .. " n " .. '
             'tostring(cleria.save.get("n"))) end)\n'
             'cleria.events.on("save", function(e) cleria.log("saved slot " .. e.slot) end)\n')
_LUA_PROFILE = ('cleria.log("boot " .. tostring(cleria.save.set_profile("../run 7")) .. " " .. cleria.save.profile())\n'
                'cleria.command("sp", function(a) cleria.log("set [" .. a .. "] -> " .. tostring(cleria.save.set_profile(a)) .. '
                '" is " .. cleria.save.profile()) end)\n')
# The blessing shop's hooks (API 5, LUA_API.md §5.1): every price answered 7, the rows re-labelled, the effect
# suppressed, and the purchase reported.
_LUA_SHOP = ('cleria.log("api " .. cleria.api_version)\n'
             'cleria.shop.add_filter(function(q)\n'
             '  if q.kind == "price" then cleria.log("price " .. q.index .. " sub " .. q.sub .. " was " .. q.value .. " in " .. '
             'q.script) return {action = "replace", value = 7} end\n'
             '  if q.kind == "row" and q.value >= 0 then return {action = "replace", text = "Check #" .. q.index .. '
             '" - [SP:]" .. q.value} end\n'
             '  if q.kind == "buy" then return "suppress" end\n'
             'end)\n'
             'cleria.events.on("blessing_bought", function(e) cleria.log("bought " .. e.index .. " price " .. e.price .. '
             '" suppressed " .. tostring(e.suppressed) .. " flag217 " .. cleria.game.flag(217) .. " sp " .. cleria.player.sp() '
             '.. " room " .. e.room) end)\n')
CASES.update({
    # The `blessing` case's walk to the cleansed statue's @GrowMenu, one row further down: "Increase stationary
    # heal rate" (@Grow00, 1000 SP), with 250 SP in the wallet. The mod prices it at 7 (the row, the script's
    # 0x61 test and its 0x69 deduction), so it is bought: 243 SP left, blessing_bought (index 0, price 7), and
    # the bit of flag 217 stays clear because the mod suppressed op 0xAF.
    "lua_blessing_shop": dict(
        room="S_10/S_1009/S_1009", lua=_LUA_SHOP,
        drive=CLEANSE + ";200:sp=250;220:act;290:down;300:ok;390:down;400:ok;480:ok;570:ok;650:ok;730:ok;820:esc;"
              "835:stats;836:flagdump;840:quit",
        expect=[_LP + "api 6", _LP + "price 0 sub 221 was 1000 in @GrowMenu", _LP + "price 0 sub 97 was 1000 in @Grow00",
                _LP + "price 0 sub 105 was 1000 in @Grow00", "[content] blessing_bought 0 for 7 SP (effect suppressed)",
                _LP + "bought 0 price 7 suppressed true flag217 0 sp 243 room S_1009", "sp 243"],
        absent=[" 217="], count={_LP + "bought": 1}),
    # boss_defeated as a script gets it: a floor boss's flag 0 -> 1 carries the flag (S_1099, 220); a fight
    # that ends by flag 156 going 1 -> 0 has none; 156 cleared in another room than it was set is not a win.
    "lua_boss_defeated": dict(
        room="S_10/S_1009/S_1009", lua=_LUA_EV,
        drive="60:flag=220,1;80:flag=156,1;100:flag=156,0;120:flag=156,1;130:exit=0;400:flag=156,0;460:quit",
        expect=["[lua] test.lua: started (main.lua)", "[lua:test.lua] boss_defeated room S_1009 flag 220",
                "[lua:test.lua] boss_defeated room S_1009 flag nil", ">> entering S_10"],
        count={"[lua:test.lua] boss_defeated": 2}),
    # item_used: the Panacea used from the book's Item page (flag 228 = the page's cursor, cell 2 = item 0x59;
    # OK on the tile, OK on the Use popup's Yes).
    "lua_item_used": dict(
        room="S_10/S_1009/S_1009", lua=_LUA_EV,
        drive="5:flag=89,2;6:flag=228,2;20:esctap;70:right;130:ok;190:ok;260:ok;330:quit",
        expect=["[content] item_used 0x59 (Celcetan Panacea)",
                _LP + "item_used room S_1009 flag nil item 89 script nil"],
        count={_LP + "item_used": 1}),
    # door_opened: S_1005's UseKey (the Bronze Key door) stores the door's flag 315; a second run of the script
    # on the open door is not an opening.
    "lua_door_opened": dict(
        room="S_10/S_1005/S_1005", lua=_LUA_EV,
        drive="60:flag=99,1;80:run=UseKey;200:run=UseKey;300:quit",
        expect=["[content] door_opened S_1005 flag 315", _LP + "door_opened room S_1005 flag 315 item nil script usekey"],
        count={_LP + "door_opened": 1}),
    # cleria.game.save(slot): refused in the room's first second, without the controls (185 = 0), for a slot
    # outside 0..63 and when dead; else the statue save (the continue snapshot, the slot, the sidecar, `save`).
    # A load of that slot brings back the flags and the mod's storage of the moment of the save.
    "lua_save": dict(
        room="S_10/S_1009/S_1009", lua=_LUA_SAVE,
        drive="20:lua=sv 8;100:flag=185,0;105:lua=sv 8;110:flag=185,1;120:flag=89,3;125:lua=put 7;130:lua=sv 8;"
              "140:lua=sv 64;150:flag=89,5;155:lua=put 9;160:cload=8;230:lua=fl 89;240:hp=0;300:lua=sv 8;310:quit",
        expect=[_LP + "save 8 -> false", _LP + "save 8 -> false", "sidecar yso_08.cleria written", _LP + "saved slot 8",
                _LP + "save 8 -> true", _LP + "save 64 -> false", "event load", _LP + "flag 89 = 3 n 7", "[death] HP 0",
                _LP + "save 8 -> false"],
        count={"-> true": 1, "[cont] snapshot S_1009": 4}),      # the room root's, the save's, the load's and its root's
    # Save profiles (LUA_API.md §6.1). The mod's main chunk runs before a game is bound, so its set_profile is
    # taken (the hostile name becomes one folder name); in the room every set_profile is refused and changes
    # nothing. The save lands in profiles/<name>/ (`cprofile`: files in the normal folder / in the active one)
    # and loads back from there.
    "lua_save_profile": dict(
        room="S_10/S_1009/S_1009", lua=_LUA_SAVE + _LUA_PROFILE,
        drive="20:lua=sp other;25:lua=sp;30:cprofile;120:flag=89,3;125:lua=put 7;130:lua=sv 8;135:cprofile;"
              "150:flag=89,5;160:cload=8;230:lua=fl 89;240:quit",
        expect=['[save] profile ".._run_7" -> ', _LP + "boot true .._run_7", _LP + "set [other] -> false is .._run_7",
                _LP + "set [] -> false is .._run_7", 'cprofile ".._run_7" normal 0 active 0', _LP + "save 8 -> true",
                'cprofile ".._run_7" normal 0 active 2', "event load", _LP + "flag 89 = 3 n 7"],
        count={"[save] profile": 1}),
    # The same without a mod: CLERIA_SAVE_PROFILE names the profile for the whole run.
    "save_profile_env": dict(
        room="S_10/S_1009/S_1009", env=dict(CLERIA_SAVE_PROFILE="AP seed/1"),
        drive="20:cprofile;120:flag=89,3;130:csave=3;135:cprofile;150:flag=89,5;160:cload=3;240:quit",
        expect=['[save] profile "AP_seed_1" -> ', os.path.join("profiles", "AP_seed_1"), 'cprofile "AP_seed_1" normal 0 active 0',
                'cprofile "AP_seed_1" normal 0 active 1', "event load"],
        count={"[save] profile": 1}),
})

# Mod API 6 (LUA_API.md §5.2, §4.1): the EXP / SP / damage / drop / pickup filters, enemy_killed and
# pickup_collected, the stat modifiers, add_exp and spawn_pickup, through a generated Lua mod. `lua=on <name>`
# turns one of its rules on, so a case shows the game's own numbers first where it needs them.
_LUA_API6 = r"""
cleria.log("api " .. cleria.api_version .. " combat " .. type(cleria.combat.add_filter))
local on, M = {}, 100
local function f(x) return string.format("%.4g", x) end
cleria.command("on", function(a) on[a] = true end)
cleria.game.exp_factor(function() return on.x100 and M or 1 end)
-- the Archipelago rule of API6_FOR_AP.md: with an EXP multiplier M, a kill gives at least M / 2
cleria.combat.add_filter("exp", function(q)
    cleria.log("exp " .. q.source .. " cast " .. tostring(q.cast) .. " lv " .. q.level .. " vs " .. q.enemy_level .. " base " ..
               f(q.base) .. " scaled " .. f(q.scaled) .. " rate " .. f(q.rate) .. " factor " .. f(q.factor) .. " floored " ..
               f(q.floored) .. " combo " .. q.combo .. " value " .. f(q.value) .. " room " .. q.room)
    if on.floor and q.source == "kill" then return math.max(q.value, M / 2) end
    if on.noexp then return "suppress" end
end)
cleria.combat.add_filter("sp", function(q)
    cleria.log("sp " .. q.source .. " base " .. q.base .. " value " .. q.value .. " cast " .. tostring(q.cast))
    if on.sp77 then return {action = "replace", value = 77} end
    if on.nosp then return "suppress" end
end)
cleria.combat.add_filter("damage", function(q)
    cleria.log("damage " .. q.dir .. " cast " .. tostring(q.cast) .. " lv " .. tostring(q.level) .. " atk " .. f(q.atk) ..
               " power " .. f(q.power) .. " def " .. f(q.def) .. " formula " .. q.formula .. " value " .. q.value)
    if on.chip and q.dir == "dealt" then return 1 end
    if on.tank and q.dir == "taken" then return 3 end
end)
cleria.combat.add_filter("drop", function(q)
    cleria.log("drop " .. q.item .. " " .. q.name .. " slot " .. q.slot .. " cast " .. q.cast .. " lv " .. q.level)
    if on.nodrop then return "suppress" end
    if on.gem then return {action = "replace", item = 0x42, value = 7} end
end)
cleria.combat.add_filter("pickup", function(q)
    cleria.log("pickup " .. q.item .. " " .. q.name .. " what " .. q.what .. " value " .. f(q.value) .. " ticks " .. q.ticks)
    if on.p123 then return 123 end
    if on.nopick then return "suppress" end
    if on.buff and q.what == "atk_buff" then return {action = "replace", value = 2, ticks = 600} end
end)
cleria.events.on("enemy_killed", function(e)
    cleria.log("killed cast " .. e.cast .. " lv " .. e.level .. " exp " .. f(e.exp) .. " sp " .. e.sp .. " room " .. e.room ..
               string.format(" at %.1f %.1f %.1f", e.x, e.y, e.z))
end)
cleria.events.on("pickup_collected", function(e)
    cleria.log("collected " .. e.item .. " what " .. e.what .. " value " .. f(e.value) .. " ticks " .. e.ticks ..
               " suppressed " .. tostring(e.suppressed))
end)
cleria.command("st", function()
    local s = cleria.player.stats()
    local a, m = cleria.player.modifier("atk")
    cleria.log("stats lv " .. s.level .. " hp " .. s.hp .. "/" .. s.max_hp .. " (" .. f(s.base_max_hp) .. ") atk " .. f(s.atk) ..
               " (" .. f(s.base_atk) .. ") def " .. f(s.def) .. " (" .. f(s.base_def) .. ") exp " .. f(s.exp) .. "/" .. f(s.exp_next) ..
               " mp " .. f(s.mp) .. "/" .. f(s.max_mp) .. " boost " .. f(s.boost) .. " tier " .. s.weapon_tier .. " sp " .. s.sp ..
               " buffs " .. f(s.atk_buff) .. ":" .. s.atk_buff_ticks .. " " .. f(s.def_buff) .. ":" .. s.def_buff_ticks .. " " ..
               f(s.mp_buff) .. ":" .. s.mp_buff_ticks .. " atk mod " .. a .. " x" .. m)
end)
cleria.command("mod", function(a)
    local stat, add, mul = a:match("(%S+) (%S+) (%S+)")
    cleria.player.set_modifier(stat, tonumber(add), tonumber(mul))
end)
cleria.command("xp", function(a) cleria.player.add_exp(tonumber(a)) end)
cleria.command("spawn", function(a)
    local id, v = a:match("(%S+) (%S+)")
    local s = cleria.player.stats()
    cleria.log("spawn " .. id .. " -> " .. tostring(cleria.game.spawn_pickup(tonumber(id), s.x, s.y + 1, s.z,
                                                                             tonumber(v) >= 0 and {value = tonumber(v)} or nil)))
end)
"""
_NOSTOP = ["script stopped", "Mod stopped"]
_K17 = ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(40, 140, 25))
CASES.update({
    # The Archipelago rule (API6_FOR_AP.md): Hard, level 13 against a level-3 M_130 (cast 17; 10 levels over: 33 / 1.65^10
    # x 0.6666 = 0.147), EXP x100 by exp_factor. The engine floors after the multiplier: 14, x1.01 for the combo =
    # 14.14 (the user's "16" at x100). The "exp" filter's math.max(q.value, M / 2) makes it 50.
    "lua_exp_floor": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="5:diff=3;6:level=13;8:opt=DamageDealt=10;10:opt=DamageTaken=0;12:lua=on x100;13:lua=on floor;14:stats;" + _K17 +
              ";150:stats;160:quit",
        expect=[_LP + "api 6 combat function", "stats exp 21936 lv 13",
                _LP + "exp kill cast 17 lv 13 vs 3 base 33 scaled 0.1471 rate 1 factor 100 floored 14 combo 1 value 14.14 room S_1002",
                _LP + "killed cast 17 lv 3 exp 50 sp 4 room S_1002 at 30.5 -2.9 -36.9", "stats exp 21986 lv 13"],
        absent=_NOSTOP, count={_LP + "exp kill": 1, _LP + "killed": 1}),
    # The same kill with no multiplier and no rule: the engine's own "at least 1" (x1.01), passed through.
    "lua_exp_plain": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="5:diff=3;6:level=13;8:opt=DamageDealt=10;10:opt=DamageTaken=0;14:stats;" + _K17 + ";150:stats;160:quit",
        expect=["stats exp 21936 lv 13", "scaled 0.1471 rate 1 factor 1 floored 1 combo 1 value 1.01 room S_1002",
                _LP + "killed cast 17 lv 3 exp 1.01 sp 4", "stats exp 21937 lv 13"],
        absent=_NOSTOP),
    # The "sp" filter on a kill's SP (4 -> 77) and on a gold piece (the "pickup" filter first: 1 -> 123, then
    # "sp": -> 77); the "exp" filter's "suppress": the kill credits no EXP. 0 + 77 + 77 = 154.
    "lua_sp_filter": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="6:level=13;8:opt=DamageDealt=10;10:opt=DamageTaken=0;12:lua=on sp77;13:lua=on noexp;14:lua=on p123;15:stats;" + _K17 +
              ";150:stats;160:quit",
        expect=["stats exp 21936 lv 13 hp 129/129 sp 0", _LP + "sp kill base 4 value 4 cast 17",
                _LP + "killed cast 17 lv 3 exp 0 sp 77", _LP + "pickup 120 1G what sp value 1 ticks 0",
                _LP + "sp pickup base 123 value 123 cast nil", _LP + "collected 120 what sp value 77 ticks 0 suppressed false",
                "stats exp 21936 lv 13 hp 129/129 sp 154"],
        absent=_NOSTOP),
    # The "damage" filter both ways. The player's hit: the formula's 2, x10 by the Cheats = 20 (HP 74 -> 54), then
    # answered 1 (54 -> 53). A hit on the player (`hurt=60`: ATK 60 against DEF 38 = 22): answered 3, HP 60 -> 57.
    "lua_damage_filter": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="8:opt=DamageDealt=10;20:near=17;21:atk;60:lua=on chip;61:lua=on tank;70:near=17;71:atk;110:near=17;120:hurt=60;"
              "130:stats;140:quit",
        expect=[_LP + "damage dealt cast 17 lv 3 atk 46 power 0 def 53 formula 2 value 20", "near cast 17 id 17 hp 54 ",
                _LP + "damage dealt cast 17 lv 3 atk 46 power 0 def 53 formula 2 value 20", "near cast 17 id 17 hp 53 ",
                _LP + "damage taken cast nil lv nil atk 60 power 0 def 38 formula 22 value 22", "stats exp 0 lv 1 hp 57/60"],
        absent=_NOSTOP),
    # set_modifier: ATK (46 + 10) x 2 = 112, max HP 60 + 50, DEF x 0.5; kept through a level-up (add_exp: level 2,
    # (49 + 10) x 2 = 118, max HP 65 + 50, the level-up's full heal) and a save + load; the hits use them (the
    # formula's 77 with ATK 118; 32 taken with DEF 20.5); (0, 1) removes them and HP falls back under the max.
    "lua_stat_modifier": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="10:lua=st;20:lua=mod atk 10 2;21:lua=mod max_hp 50 1;22:lua=mod def 0 0.5;30:lua=st;40:lua=xp 400;50:lua=st;"
              "60:saveload;80:lua=st;90:near=17;91:atk;120:hurt=60;130:lua=mod atk 0 1;131:lua=mod max_hp 0 1;132:lua=mod def 0 1;"
              "140:lua=st;150:quit",
        expect=[_LP + "stats lv 1 hp 60/60 (60) atk 46 (46) def 38 (38) exp 0/150 mp 3000/3000 boost 0 tier 0 sp 0 buffs 0:0 0:0 0:0 atk mod 0.0 x1.0",
                _LP + "stats lv 1 hp 60/110 (60) atk 112 (46) def 19 (38) exp 0/150", "atk mod 10.0 x2.0",
                "[levelup] lv 2", _LP + "stats lv 2 hp 115/115 (65) atk 118 (49) def 20.5 (41) exp 400/605.1",
                _LP + "stats lv 2 hp 115/115 (65) atk 118 (49) def 20.5 (41) exp 400/605.1",
                _LP + "damage dealt cast 17 lv 3 atk 118 power 0 def 53 formula 77 value 77",
                _LP + "damage taken cast nil lv nil atk 60 power 0 def 20.5 formula 32 value 32",
                _LP + "stats lv 2 hp 65/65 (65) atk 49 (49) def 41 (41)", "atk mod 0.0 x1.0"],
        absent=_NOSTOP),
    # The "drop" filter replaces the monster's gold with Recovery Drops worth 7 HP (the pickup's own value: the
    # "pickup" query already reads 7); spawn_pickup: a Strength Drop whose step and timer the "pickup" filter
    # replaces (+2 ATK for 600 ticks), a Recovery Drop made worth 9, and a gold piece the filter suppresses (taken,
    # no SP); pickup_collected for each.
    "lua_drop_pickup": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="6:hp=10;8:opt=DamageDealt=10;10:opt=DamageTaken=0;12:lua=on gem;" + _K17 + ";150:lua=st;160:pickups;200:lua=st;"
              "210:lua=on buff;220:lua=spawn 0x48 -1;300:lua=st;310:lua=spawn 0x42 9;390:lua=on nopick;400:lua=spawn 0x78 -1;"
              "480:lua=st;490:quit",
        expect=[_LP + "drop 120 1G slot 1 cast 17 lv 3", _LP + "stats lv 1 hp 10/60", "[pickup] id 66 pos",
                _LP + "pickup 66 Recovery Drop 1 what hp value 7 ticks 0", _LP + "collected 66 what hp value 7 ticks 0 suppressed false",
                _LP + "stats lv 1 hp 24/60", _LP + "spawn 0x48 -> true",
                _LP + "pickup 72 Strength Drop 1 what atk_buff value 0.5 ticks 1800",
                _LP + "collected 72 what atk_buff value 2 ticks 600 suppressed false", _LP + "stats lv 1 hp 24/60 (60) atk 48 (48)",
                " buffs 2:5", _LP + "pickup 66 Recovery Drop 1 what hp value 9 ticks 0", _LP + "spawn 0x78 -> true",
                _LP + "pickup 120 1G what sp value 1 ticks 0", _LP + "collected 120 what sp value 0 ticks 0 suppressed true",
                _LP + "stats lv 1 hp 33/60 (60) atk 48 (48) def 38 (38) exp 50.96/150 mp 3000/3000 boost 0 tier 0 sp 4 "],
        absent=_NOSTOP + ["[pickup] id 120"]),
    # A boss part (boss_part_feedback's fight: Toal on a risen Khonsclard pod): the same filter, without a cast;
    # answered 1, the part loses 1 a hit.
    "lua_damage_boss": dict(
        room="S_40/S_4099/S_4099", char="3", lua=_LUA_API6,
        env={"CLERIA_DMG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="2:diff=3;3:saveload;4:opt=DamageTaken=0;6:level=45;7:tier=8;10:lua=on chip;20:at=-2.8,23.3,0;40:every=20,700,ok;"
              "720:every=12,1100,boss4;721:every=12,1100,atk;1100:quit",
        expect=["[boss04] Khonsclard: body 3750, pods 900", "boss4 -> part 1", _LP + "damage dealt cast nil lv nil atk 246 power 3 def 175 formula 144 value 144",
                "hit khonsclard part 1 (DEF 175): 1 dmg, part HP 899 body 3750"],
        absent=_NOSTOP),
    # "suppress" on every drop: the kill leaves debris only.
    "lua_drop_suppress": dict(
        room="S_10/S_1002/S_1002", lua=_LUA_API6,
        drive="8:opt=DamageDealt=10;10:opt=DamageTaken=0;12:lua=on nodrop;" + _K17 + ";150:pickups;155:stats;160:quit",
        expect=[_LP + "drop 120 1G slot 1 cast 17 lv 3", "[pickup] debris", "stats exp 51 lv 1 hp 60/60 sp 4"],
        absent=_NOSTOP + ["[pickup] id ", _LP + "pickup", _LP + "collected"]),
})

# The screen effects (build/render_re/SCREEN_EFFECTS.md; CLERIA_SCRFX_LOG=1 prints each write and each first draw).
_VELA = ("10:at=0,15,180;" + "".join(f"{t}:ok;" for t in range(60, 1801, 40))
         + "".join(f"{t}:boss;{t + 1}:atk;" for t in range(2000, 3201, 12)) + "3300:at=0,19,180;")
CASES.update({
    # ZoomBlur (§7): Velagunder's intro ramps it up by XSO 0xce (0.01, +0.005: the first drawn frame has TFACTOR
    # alpha floor(0.015 x 256) = 3, screen centre), then (0.38, -0.0012); the bite is the native write
    # (0.4, -0.02) with FUN_005e0590's centre at the projected mouth (alpha floor(0.38 x 256) = 97).
    # Re-pinned 2026-10-10 (the blood splats: 18 draws at every hit the player takes move Velagunder's stream): the
    # same bite 3 ticks earlier (t3806) from another spot, the mouth projected at x 0.526 (was 0.487), y 0.317.
    "zoom_blur": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="15", CLERIA_SCRFX_LOG="1"),
        drive=_VELA + "4600:quit",
        expect=["[zoomblur] t1051 ZoomBlur(0.0100, 0.0050)", "draw strength 0.0150 alpha 3 taps 3 centre (0.500, 0.500)",
                "ZoomBlur(0.3800, -0.0012)", "ZoomBlur(0.4000, -0.0200)", "draw strength 0.3800 alpha 97 taps 3 centre (0.5"]),
    # The boss hit flash (§3.2, FUN_00504140): the stun part's damage sets obj+0x2a0 = 1.65, the brain's tail decays
    # it once (1.575) before the draw, whose additive colour is (107, 55, 19) / 255 x 0.5 x (1.575 - 0.65).
    # (t2040, was t2034: since e44f09f the first hit on the re-bound hull lands 6 ticks later; the values are the same.)
    "boss_hit_flash": dict(
        room="S_10/S_1099/S_1099", env=dict(CLERIA_LEVEL="15", CLERIA_SCRFX_LOG="1"),
        drive=_VELA + "2050:quit",
        expect=["[hitflash] t2040 boss01 +0x2a0 1.57 -> +(0.194, 0.100, 0.034)"]),
    # The GUARD white flash (§3.1): the specular tint struct +0x260 set to white -> 0 at 0.05 a tick by FUN_004fd350's
    # guard branches — here Toal's Ignis MP armour (toal_ignis_armor's hit at t48) and Hugo's super barrier.
    "guard_white_flash": dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(CLERIA_SCRFX_LOG="1"),
        drive="8:bracelet=118;9:flag=118,1;10:flag=184,1;43:skill+;45:skill-;48:hurt=60;80:quit",
        expect=["[tint] t48 player specular 0xffffff -> 0, step 0.050", "[popup] 0 ff0000"]),
    # Boost READY's white flash (FUN_0046cc10, step 0.075): `boost+=` adds through the real gate (add_boost), so the
    # gauge reaching 30000 is the READY event — the flash, once; a second add on the full gauge is refused.
    "boost_ready_flash": dict(
        room="S_10/S_1009/S_1009", char="1", env=dict(CLERIA_SCRFX_LOG="1"),
        drive="3:flag=187,1;12:boost=29900;13:abil;20:boost+=300;21:abil;25:boost+=300;40:quit",
        expect=["[abil] t13 sec 0x2 mp 3000 lag 3000 boost 29901", "[tint] t20 player specular 0xffffff -> 0, step 0.075",
                "[abil] t21 sec 0x2 mp 3000 lag 3000 boost 30000"],
        count={"[tint] t": 1}),
    "guard_white_flash_barrier": dict(
        room="S_10/S_1009/S_1009", char="2", env=dict(CLERIA_SCRFX_LOG="1"),
        drive="3:flag=187,1;4:flag=188,1;12:boost=30000;20:boostkey;90:boostkey;190:hurt=60;200:quit",
        expect=["[tint] t190 player specular 0xffffff -> 0, step 0.050", "[popup] 0 ff0000"]),
    # The scene light's colour (§4.3) multiplies every material's diffuse and the actor sprites: Darm's state
    # machine sets it by FUN_00443ea0 — 0xffaa8080 in the normal stance (the fight's first frames), 0xffaaaa80 next.
    "scene_light": dict(
        room="S_70/S_7099/S_7099", env={"CLERIA_CHAR": "3", "CLERIA_SCRFX_LOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=2;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss8;61:every=15,2900,oktapf;2900:quit",
        expect=["[light] t11 the scene light's colour (0.667, 0.502, 0.502)",
                "[light] t2838 the scene light's colour (0.667, 0.667, 0.502)"]),
    # The frame hold (§7.3): Zava's death (zava_fight's run) writes 0x76a548 / 0x76a54c = 1.0 / -0.025 with
    # 0x7b2488 = 1: that frame is copied and drawn over the live scene, first at floor(0.975 x 256) = 249.
    # Re-pinned 2026-10-10 (the dust puffs, as zava_fight): her death is at t4102, was t3830; the run ends at t4400.
    # ... and again (the blood splats, as zava_fight: Damage dealt 0.45): her death is at t4900; the run ends at t5200.
    "frame_hold": dict(
        room="S_60/S_6082/S_6082", env={"CLERIA_CHAR": "1", "CLERIA_SCRFX_LOG": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.45;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss6;5200:quit",
        expect=["FrameHold(1.0000, -0.0250)", "[framehold] t", "frame copied, drawn at alpha 249"]),
})

# The second builder's batch of 2026-10-11 (FIX_SLOTS.md S4.., ENEMY_WATER.md): one block.
_GODSPEED = "5:killall;8:bracelet=116;9:flag=116,1;10:flag=182,1;30:at=20,-37,90;"
CASES.update({
    # FIX_SLOTS.md S4: the player's hit scan and her section's Generate* events run under either fix slot. Yunica
    # swings at t21, 2.0 from a Selnarg; @ItemCannotUse takes slot 1 at t23, before the section arms anything. The
    # swing's locus is still made and still lands (2 dmg, HP 74 -> 72) on the monster the Fix(1) holds in place.
    # (Nothing was armed under a Fix before: the swing played out and hit nothing.)
    "fix_swing_hits": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="20:near=17;21:atk;23:run=@itemcannotuse;40:scripts;41:actors;150:scripts;160:quit",
        expect=["slot 1 taken: 0/1", "[popup] 2 ffffff", "t40 scripts", "frozen 1 fix 0/1 popups 1", "t41 actors",
                "id 17 cast 17 enemy hp 72 g1 0 pos (30.4,-4.1,-37.3)", "t150 scripts", "frozen 1 fix 0/1 popups 0"]),
    # FIX_SLOTS.md S5a (REACQUIRE.md D2): g_flags[131] holds every handler 3..93 but the eyes', not Epona's alone
    # [EXPORT 0047b7f0.c:73-117]. A Selnarg (handler 6) engaged at t20 starts its lunge; 131 = 1 at t100: no brain
    # and no action player (its distance stays the 7.46 of t100 while it ends the lunge 3.03 away, and it stands at
    # (26.6, -36.1) from t180 to t239 — it used to come on to 26.0 and strike again); 131 = 0 at t240: the brain is back.
    "flag131_holds_brain": dict(
        room="S_10/S_1002/S_1002", env=dict(_LOCK, CLERIA_AI="1"),
        drive="5:opt=InfiniteHP=1;20:near=17;21:move=-1,0,40;100:flag=131,1;101:actors;239:actors;240:flag=131,0;300:quit",
        expect=["dist 7.46 head 3.04 culled 0 sec 7 mov 0.00 hp 74.0 mk 1 at (26.6,-36.1) alpha 1.00 t180", "t239 actors",
                "id 17 cast 17 enemy hp 74 g1 0 pos (26.6,-4.1,-36.1)", "dist 3.03 head 3.04 culled 0 sec 2 mov 1.00 hp 74.0 mk 0 at (26.6,-36.1) alpha 1.00 t240"]),
    # FIX_SLOTS.md S5b: under slot 1 a stopped handler does not step its ASO either [EXPORT 0047b7f0.c:31-124]. The
    # Selnarg running at Yunica when @ItemCannotUse takes slot 1 holds its run frame (section 1, tag 1, frame 3) for
    # the whole Fix; it used to be put on its idle section and animated (2/2/0 at t41, 2/2/7 at t100).
    "fix1_handler_holds_frame": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="20:near=17;39:run=@itemcannotuse;41:chr=17;100:chr=17;101:scripts;160:quit",
        expect=["slot 1 taken: 0/1", "[chr] 17", "motion -1 aso 1/1/3", "[chr] 17", "motion -1 aso 1/1/3", "frozen 1 fix 0/1"]),
    # FIX_SLOTS.md S6: the GoXx pool runs under a Fix. A gold piece dropped at t20 goes on falling under the Fix(1)
    # taken at t23 (it hung at age 3 for the whole Fix) and is collected under it when Toal is put by it at t90
    # (SP 0 -> 1, its popup): the pickup's step and collect read no fix counter [EXPORT 00584f90.c, 005856c0.c,
    # 00585ff0.c].
    "drop_falls_fix1": dict(
        room="S_10/S_1009/S_1009", char="3", env=_LOCK,
        drive="20:drop=120;21:at=6.0,7.87,270;23:run=@itemcannotuse;26:pickups;60:pickups;90:at=0.6,7.87,270;100:pickups;"
              "101:stats;102:scripts;110:quit",
        expect=["slot 1 taken: 0/1", "[pickup] id 120 pos (0.13, 2.67, 7.81) ground 2.020 rest 0 age 6",
                "[pickup] id 120 pos (0.47, 2.07, 7.85) ground 2.020 rest 0 age 40", "t100 pickups", "hp 60/60 sp 1",
                "frozen 1 fix 0/1 popups 1"],
        count={"[pickup] id 120": 2}),
    # The regression of 2026-10-11 ("mobs are all immobile"): g_flags[131] left set by a duel / scene script held
    # every monster of the following rooms, and of a save written with it, once S5a made every handler honour it.
    # The scene load clears 131 and 134 [EXPORT 00431260.c:141-142]: set stale in S_1002, it is 0 after the door into
    # S_1003, and 0 again after a save / load made with it set (the load is a scene load); S_1003's monsters then
    # move (#43 went from z -6.4 to -9.1 between t181 and t300; held, it stood at its spawn (-28.4, 0.0, -11.0)).
    "flag131_cleared_by_scene_load": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="5:opt=InfiniteHP=1;6:flag=131,1;8:dflags;10:exit=0;100:dflags;110:flag=131,1;111:saveload;170:dflags;"
              "180:nearest=6;181:actors;300:actors;301:quit",
        expect=["131=1", ">> entering S_1003", "t100 dflags", "131=0", ">> entering S_1003", "t170 dflags", "131=0",
                "t181 actors", "id 43 cast 43 enemy hp 86 g1 0 pos (-28.4,0.2,-6.4)", "t300 actors",
                "id 43 cast 43 enemy hp 86 g1 0 pos (-27.4,0.2,-9.1)"]),
    # S_5013's seven falling steps (@Fall_001: on Normal and above a touched step sinks 0.025 a tick for 10 ticks,
    # 0.05 for 10, loses its collision at tick 20 and is back at tick 120), crossed with real inputs: Toal on Hard
    # with the double jump and the dash, one placement at the start of the run, then a double tap and the stick held
    # toward -x, and a jump press ONE tick after every landing (a second press 16 ticks later = the double jump
    # over the long gaps). The jump gate reads the locomotion state +0x3a0, not the section [EXPORT 00465170.c:243,
    # 1939-1940]: one grounded tick after a landing still counts as air, the next jumps. CleriaCore tested
    # "section != 3" and so swallowed every press during the jump section's landing frames (4 ticks for Toal):
    # this run fell into the pit after step_03 (feet (-79.155, -8.000, 42.368) at t470).
    "s5013_falling_steps": dict(
        room="S_50/S_5013/S_5013", char="3", env=_LOCK,
        drive="3:diff=3;5:opt=InfiniteHP=1;6:flag=163,1;11:flag=181,1;30:at=-10,42,270;38:move=-1,0,3;44:move=-1,0,420;"
              "94:jump;131:jump;147:jump;181:jump;197:jump;228:jump;264:jump;280:jump;316:jump;332:jump;375:jump;391:jump;"
              "470:pos;480:quit",
        expect=["[plate] step_00 -> @fall_001", "[plate] step_01 -> @fall_001", "[plate] step_03 -> @fall_001",
                "[plate] step_04 -> @fall_001", "[plate] step_05 -> @fall_001", "[plate] step_06 -> @fall_001",
                "t470 pos", "feet (-90.371, 0.000, 42.000)"]),
    # The kerb ride (SWEEP_PUSHOUT.md §0.2-0.4; kerb_ride in physics_player.hpp): on the ground the engine's sphere
    # rides over an edge below its centre while the lift outruns the settle's pull — about 0.7 at a run. In S_5013
    # Toal lands on step_01 and walks on (no jump): 18 ticks later it has sunk 0.6 under step_02, and he rides up onto
    # step_02 (y -2.595 at t226 on the sunk step, -2.078 at t234 on step_02). The stand-in's limit of 0.6 stopped him
    # at x -39.55 and he went down with step_01.
    "s5013_step_to_neighbour": dict(
        room="S_50/S_5013/S_5013", char="3", env=_LOCK,
        drive="3:diff=3;5:opt=InfiniteHP=1;6:flag=163,1;30:at=-10,42,270;38:move=-1,0,900;102:jump;152:jump;168:jump;"
              "226:pos;234:pos;240:quit",
        expect=["[plate] step_01 -> @fall_001", "feet (-39.526, -2.595, 42.000)", "[plate] step_02 -> @fall_001",
                "feet (-40.668, -2.078, 42.000)"]),
    # ... and a kerb of 1.0 is still a wall: on Easy the steps do not fall (FALL_001's flag-143 branch), and Toal
    # walking on step_02 against step_03 (one unit up) stalls a sphere's reach from its face, lifted 0.011 — the
    # ride's equilibrium (it used to stand 0.45 from it, the capsule's radius).
    "kerb_1u_is_a_wall": dict(
        room="S_50/S_5013/S_5013", char="3", env=_LOCK,
        drive="3:diff=1;5:opt=InfiniteHP=1;30:at3=-41,0,42,270;40:move=-1,0,120;60:pos;150:pos;160:quit",
        expect=["feet (-43.333, -1.989, 42.000)"], count={"feet (-43.333, -1.989, 42.000)": 2}),
    # ENEMY_WATER.md W1 / W2: in a flooded room (mode 2: S_2010) every actor is submerged, and the integrator slows
    # its xz step x 0.8 with no cast test [BYTES 0x45a82a..0x45a87d]. The Kelemare wandering +z at its 0.0366 a tick
    # is at z 5.5 at t20 and 8.5 at t120: 3.0 in 100 ticks (it was 5.7 -> 9.3, 3.6, at land speed).
    "kelemare_flooded": dict(
        room="S_20/S_2010/S_2010", env=_LOCK,
        drive="5:opt=InfiniteHP=1;6:opt=InfiniteOxygen=1;20:actors;120:actors;121:quit",
        expect=["t20 actors", "id 25 cast 25 enemy hp 125 g1 0 pos (-2.3,0.0,5.5)", "t120 actors",
                "id 25 cast 25 enemy hp 125 g1 0 pos (-2.3,0.0,8.5)"]),
    # ENEMY_WATER.md W4 (M510.md §3.2): a Mobre cannot swim. Put in S_2003's pool (surface 6.28 over a floor at 5.0:
    # submerged) with the player in sight on the bank, it flails (section 4) and loses floor(139 / 10) = 13 HP on every
    # 30th water tick, with the white number: 139 -> 126 by t55, 113 by t85.
    "mobre_pool": dict(
        room="S_20/S_2003/S_2003", env=_LOCK,
        drive="5:opt=InfiniteHP=1;6:opt=DamageTaken=0;19:at3=-12,8,36.33,270;20:epos=#55,-20,36.33;21:actors;55:actors;"
              "85:actors;90:quit",
        expect=["id 55 cast 55 enemy hp 139", "[popup] 13 ffffff", "t55 actors", "id 55 cast 55 enemy hp 126",
                "[popup] 13 ffffff", "t85 actors", "id 55 cast 55 enemy hp 113"]),
    # FIX_SLOTS.md S8: a death stops nothing but the doors — the world runs on through the 100 dead ticks and under the
    # game-over menu [EXPORT 0046fba0.c:77-82; 00427820.c:41-47]. The Selnarg engaged at t20 goes through with its lunge
    # while Yunica lies dead (HP 0 at t60): from (31.0, -36.5) at t61 and t120 to (26.6, -36.1) at t200, under the
    # menu. It used to stand at (31.0, -36.5) until the Retry.
    "death_world_runs": dict(
        room="S_10/S_1002/S_1002", env=_LOCK,
        drive="20:near=17;21:move=-1,0,30;60:hp=0;61:actors;120:actors;200:actors;230:ok;260:pos;300:quit",
        expect=["[death] HP 0", "t61 actors", "id 17 cast 17 enemy hp 74 g1 0 pos (31.0,-4.1,-36.5)", "[gameover] gameover.dds",
                "t200 actors", "id 17 cast 17 enemy hp 74 g1 0 pos (26.6,-4.1,-36.1)", "[gameover] chose retry"]),
    # The floor entry's no-jump byte (+0x25c record +0x89): a jump press on the ground is refused while it is set
    # [EXPORT 00465170.c:234-240], and one ctor in the exe sets it — the Zava heads', on their collision entry 0
    # [EXPORT 0053bbb0.c:247-254]. Head a at rest in S_6082: standing on its entry 0 (the low surface at y ~7, inside
    # the head: placed from y 8.5) the press does nothing; on its top (another entry, y 10.76) she jumps.
    # [H] which twin mesh is entry 0 follows the collision file's node order (ZAVA_D.md U-D4).
    "zava_head_no_jump": dict(
        room="S_60/S_6082/S_6082", env=_LOCK,
        drive="5:opt=InfiniteHP=1;6:opt=DamageTaken=0;30:at3=-39.46,8.5,29.2,0;38:floor;39:pos;40:jump;46:pos;"
              "60:at3=-38.66,16,30,0;68:floor;69:pos;70:jump;76:pos;90:quit",
        expect=["floor 'boss06a' nojump 1", "feet (-39.460, 7.108, 29.098)", "[jump] t40 refused", "feet (-39.461, 7.069, 28.978)",
                "floor 'boss06a' nojump 0", "feet (-38.662, 10.763, 29.838)", "feet (-38.662, 12.413, 29.838)"],
        count={"[jump] t": 1}),
    # FIX_SLOTS.md S5c [LIKELY, not retail-proven; RETAIL_ANSWERS.md A1]: the monsters' handlers test slot 1 alone, so
    # under a Fix that holds slot 0 only they run on. S_1004's Riffligan #189 seen from 6 u, then the blessing shop
    # @GrowMenu (slot 0): it comes on from z -1.8 to -7.8 and stops 2.02 from Yunica (it stood at (44.9, -1.8)).
    # Under @ItemCannotUse (slot 1) it holds where it was.
    "fix0_enemy_runs": dict(
        room="S_10/S_1004/S_1004", env=dict(_LOCK, CLERIA_AI="1"),
        drive="5:opt=InfiniteHP=1;20:seen=#189,6;22:run=@growmenu;23:actors;119:actors;239:actors;241:quit",
        expect=["slot 0 taken: 1/0", "id 189 cast 14 enemy hp 60 g1 0 pos (44.9,0.0,-1.8)",
                "[ai] #189 cast 14 phase 2 sub 0 act 0x400 steer 0 mode 2 spd 0.0487 dist 4.69",
                "id 189 cast 14 enemy hp 60 g1 0 pos (44.5,-2.0,-6.6)", "dist 2.02", "id 189 cast 14 enemy hp 60 g1 0 pos (44.4,-2.0,-7.8)"]),
    "fix1_enemy_holds": dict(
        room="S_10/S_1004/S_1004", env=dict(_LOCK, CLERIA_AI="1"),
        drive="5:opt=InfiniteHP=1;20:seen=#189,6;22:run=@itemcannotuse;23:actors;119:actors;121:quit",
        expect=["slot 1 taken: 0/1", "t23 actors", "id 189 cast 14 enemy hp 60 g1 0 pos (44.9,0.0,-1.8)", "t119 actors",
                "id 189 cast 14 enemy hp 60 g1 0 pos (44.9,0.0,-1.8)"], absent=["[ai] #189"]),
    # FIX_SLOTS.md S7, row 20 [LIKELY, not retail-proven, as S5c]: the EF2 particles' hit test needs the actor-stop
    # byte clear — slot 1 alone [EXPORT 005645b0.c:109-110]. Hugo fires at a Selnarg at t21; under @GrowMenu (slot 0,
    # taken at t22) the shot lands (2 dmg, HP 74 -> 72: it flew on and hit nothing before), under @ItemCannotUse
    # (slot 1) it flies through.
    "fix0_particle_hits": dict(
        room="S_10/S_1002/S_1002", char="2", env=_LOCK,
        drive="5:opt=InfiniteHP=1;20:near=17;21:atk;22:run=@growmenu;23:actors;100:actors;101:scripts;102:quit",
        expect=["slot 0 taken: 1/0", "id 17 cast 17 enemy hp 74", "[popup] 2 ffffff", "t100 actors",
                "id 17 cast 17 enemy hp 72", "frozen 1 fix 1/0"]),
    "fix1_particle_no_hit": dict(
        room="S_10/S_1002/S_1002", char="2", env=_LOCK,
        drive="5:opt=InfiniteHP=1;20:near=17;21:atk;22:run=@itemcannotuse;23:actors;100:actors;101:scripts;102:quit",
        expect=["slot 1 taken: 0/1", "t100 actors", "id 17 cast 17 enemy hp 74 g1 0 pos (30.4,-4.1,-37.3)",
                "frozen 1 fix 0/1"], absent=["[popup]"]),
    # Hugo and Yunica on floors 4-5 (build/audit/ROUTE_F47_HUGO.md / ROUTE_F47_YUNICA.md): where their path is not
    # Toal's. Jenocres as Hugo (s_EvtZaba_Hugo -> Battle_Hugo), jenocres_fight's own drive.
    "jenocres_hugo": dict(
        room="S_40/S_4013/S_4013", char="2",
        drive="4:opt=DamageTaken=0;20:every=20,4300,ok;4380:actors;4390:opt=OneHitKill=1;"
              "4400:every=15,4700,near=40;4401:every=15,4700,atk;4700:actors;5000:dflags;5001:scripts;5002:quit",
        expect=["[actor] id 168 cast 40 enemy hp 5988", "M360 death section 5", "[actor] id 168 cast 40 enemy hp 0",
                "[content] boss_defeated S_4013", "flags 130=0 131=1 133=0 156=0", "script Battle_Hugo"],
        count={"boss_defeated": 1}),
    # ROUTE_F47 R7: Pictimos' second round, Hugo only. The first kill (224) starts Battle_Hugo_1's revive scene
    # (3160 ticks, Confirm only: the pilot would hold him off the scene's walk), Battle_Hugo_2 sets 156 again, the
    # second kill sets 224 again and AfterBattle_Hugo_2 runs. [found] the mods' boss_defeated fires for each round.
    "pictimos_hugo_round2": dict(
        room="S_50/S_5099/S_5099", char="2", env={"CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=2;8:tier=10;10:move=0,1,200;"
              "40:every=20,1000,ok;1030:every=40,2600,boss5;1038:every=40,2600,atk;2600:every=20,5760,oktapf;5765:scripts;"
              "5766:dflags;5770:every=40,9000,boss5;5778:every=40,9000,atk;9000:every=20,9400,oktapf;9400:scripts;9401:quit",
        expect=["flag 224 = 1", "slot 0 released: 0/0", "t5765 scripts", "script Battle_Hugo_2 pc 35",
                "flags 130=0 131=0 133=0 156=1", "flag 224 = 1", "t9400 scripts", "script AfterBattle_Hugo_2"],
        count={"flag 224 = 1": 2, "boss_defeated": 1}),      # one event a boss (issue #91)
    # Floors 6-7 and the ending as Hugo and Yunica (ROUTE_F47_HUGO.md / ROUTE_F47_YUNICA.md §3).
    # Zava as Hugo: the Retry start runs Battle_Hugo; zava_fight's pilot wins with his shots (225, both heads).
    "zava_hugo": dict(
        room="S_60/S_6082/S_6082", env={"CLERIA_CHAR": "2", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.45;8:tier=10;9:flag=205,1;10:saveload;"
              "11:flag=163,1;60:boss6;6400:scripts;6401:quit",
        expect=["Zava's heads: a (-39.46,6.10,30.00)", "Zava phase 2 action 0", "flag 225 = 1",
                "[content] boss_defeated S_6082 flag 225", "head b destroyed", "head a destroyed", "script Battle_Hugo"]),
    # Dalles as Hugo (his last boss): dalles_fight's drive; 226, then S_7000's Evt_7000_Hugo_2 (the ending chain).
    "dalles_hugo": dict(
        room="S_70/S_7080/S_7080", env={"CLERIA_CHAR": "2", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="3:opt=InfiniteMP=1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=1.5;8:tier=10;"
              "9:flag=205,1;10:saveload;11:flag=116,1;12:flag=117,1;13:flag=118,1;14:flag=182,3;15:flag=183,3;"
              "16:flag=184,3;17:bracelet=116;60:boss7;9000:scripts;9001:quit",
        expect=["[boss07] Dalles: HP 2500", "the fight starts at", "flag 226 = 1", "[content] boss_defeated S_7080 flag 226",
                ">> entering S_7000", "script Evt_7000_Hugo_2"]),
    # S_6080's duel, walked into. Hugo: Kishgal (cast 172) -> demo4 -> After_Move_Hugo: the Rod (98) and 235.
    "duel_s6080_hugo": dict(
        room="S_60/S_6080/S_6080", char="2", env=dict(_LOCK, CLERIA_LEVEL="50"),
        drive="1:opt=CutsceneSkip=3;8:tier=10;9:opt=DamageTaken=0;40:move=0,1,300;80:every=20,9000,oktapf;"
              "81:every=7,9000,duel=172;9000:scripts;9001:flagdump;9002:quit",
        expect=["[content] boss_defeated S_6080", "ChangeScene 'demo4\\' -> S_6080", "t9000 scripts", "frozen 0 fix 0/0",
                " 98=1 ", " 235=1 "], count={"boss_defeated": 1}),
    # Yunica: demo4 first, then Epona (cast 171), 235; the Rod only from TalkC960 (cast 161) afterwards.
    "duel_s6080_yunica": dict(
        room="S_60/S_6080/S_6080", char="1", env=dict(_LOCK, CLERIA_LEVEL="50"),
        drive="1:opt=CutsceneSkip=3;8:tier=10;9:opt=DamageTaken=0;40:move=0,1,300;80:every=20,4800,oktapf;"
              "81:every=7,4800,duel=171;4849:flagdump;4850:near=161;4860:act;4870:every=20,7000,oktapf;7000:scripts;"
              "7001:flagdump;7002:quit",
        expect=["ChangeScene 'demo4\\' -> S_6080", "[content] boss_defeated S_6080", " 98=-1 ", " 235=1 ",
                "[probe] actor 161 -> talkc960", "t7000 scripts", "frozen 0 fix 0/0", " 98=1 "],
        count={"boss_defeated": 1}),
    # The gap-closing pass (ROUTE_F47_HUGO.md / _YUNICA.md §"triggers"): each scene fired by standing the character in
    # its own .SEV event box (drive `boxes` lists them), then played with Confirm. Where the room root starts the
    # scene's script only behind story flags of earlier rooms, the script is started by the drive and the box does
    # the rest (marked "run=").
    # S_4014's Zelkaron ward as Yunica (R8's third gate; 241 and the ring 94 set as the route has them): the three
    # warded Zelkarons (cast 82) killed -> KekkaiZako_01 -> 393 and Item_Get: 93 = 1 (what the evil door asks for).
    "route_s4014_yunica_ward": dict(
        room="S_40/S_4014/S_4014", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=InfiniteHP=1;1:opt=DamageTaken=0;2:level=50;3:tier=10;5:flag=241,1;6:flag=94,1;"
              "7:saveload;40:opt=OneHitKill=1;50:every=12,1500,nearest;51:every=12,1500,atk;1510:every=10,3000,oktapf;"
              "3000:scripts;3001:flagdump;3002:quit",
        expect=["slot 1 taken: 0/1", "t3000 scripts", "frozen 0 fix 0/0", " 93=1 ", " 393=1 "]),
    # S_5102 as Hugo, from fresh flags: EVT_5102 -> Ev_5102_Hugo_1 -> the Black Pearl (106) and 269.
    "route_s5102_hugo_pearl": dict(
        room="S_51/S_5102/S_5102", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at3=25,0.8,20,0;40:every=10,3000,oktapf;3000:scripts;3001:flagdump;3002:quit",
        expect=["slot 0 taken: 1/0", "slot 0 released: 0/0", "t3000 scripts", "frozen 0 fix 0/0", " 106=1 ", " 269=1 "]),
    # ... as Yunica (run=): the box EV_Yunica -> Ev_5102_Yunica_1 (239); the Pearl only from talking to Hugo (cast 167,
    # Talk_M810) afterwards.
    "route_s5102_yunica_pearl": dict(
        room="S_51/S_5102/S_5102", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;10:run=Ev_5102_Yunica_1;20:at3=9,0.6,30,0;40:every=10,1900,oktapf;1905:flagdump;1910:near=167;"
              "1920:act;1930:every=10,3200,oktapf;3200:scripts;3201:flagdump;3202:quit",
        expect=["slot 0 taken: 1/0", " 106=-1 ", " 239=1 ", "[probe] actor 197 -> talk_m810", "t3200 scripts",
                "frozen 0 fix 0/0", " 106=1 "]),
    # S_5080 as Hugo (run= for the second): EVT_5080 -> 268, then EVT_5080_2 -> the Mantid Medallion (82) and 271.
    "route_s5080_hugo_medallion": dict(
        room="S_50/S_5080/S_5080", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at3=-68,2.7,12,0;40:every=10,3700,oktapf;3700:flagdump;3710:run=s_Evt5080_Hugo_2;"
              "3720:at3=-70,2.6,26.5,0;3730:every=10,7200,oktapf;7200:scripts;7201:flagdump;7202:quit",
        expect=["slot 0 taken: 1/0", " 82=-1 ", " 268=1 ", "run s_Evt5080_Hugo_2 -> started", "t7200 scripts",
                "frozen 0 fix 0/0", " 82=1 ", " 271=1 "]),
    # S_5014 as Hugo (run=): EVT_5014 -> s_Evt5014_Hugo (Dalles), 272.
    "route_s5014_hugo": dict(
        room="S_50/S_5014/S_5014", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;10:run=s_Evt5014_Hugo;20:at3=73.6,4.2,-25,0;40:every=10,5900,oktapf;5900:scripts;5901:flagdump;5902:quit",
        expect=["slot 0 taken: 1/0", "slot 0 released: 0/0", "t5900 scripts", "frozen 0 fix 0/0", " 272=1 "]),
    # S_4005's Karon event as Hugo (R1): 180 == 6 exactly, Battle_Hugo goes on.
    "karon_event_hugo": dict(
        room="S_40/S_4005/S_4005", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:every=10,1650,ok;1710:opt=OneHitKill=1;1720:every=12,2400,nearest;1721:every=12,2400,atk;"
              "2400:scripts;2401:flagdump;2402:quit",
        expect=["slot 0 released: 0/0", "slot 0 taken: 1/0", "t2400 scripts", "script Battle_Hugo", " 180=6 "]),
    # S_7000 from fresh flags, each character: Event_7000 -> Evt_7000_<name>_1 -> S_7080 (Dalles' room).
    "route_s7000_hugo": dict(
        room="S_70/S_7000/S_7000", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at3=0,7.4,-27.8,0;40:every=10,9000,oktapf;9000:scripts;9001:quit",
        expect=["slot 0 taken: 1/0", ">> entering S_7080", "script EV_7080"]),
    "route_s7000_yunica": dict(
        room="S_70/S_7000/S_7000", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at3=0,7.4,-27.8,0;40:every=10,9000,oktapf;9000:scripts;9001:quit",
        expect=["slot 1 taken: 0/1", ">> entering S_7080", "script EV_7080"]),
    # The whole ending from the Dalles kill, one run a character: 226 -> S_7000 -> demo5 -> S_7002 -> S_0003 -> demo7 ->
    # the clear write -> the title.
    "ending_chain_hugo": dict(
        room="S_70/S_7080/S_7080", env={"CLERIA_CHAR": "2", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="1:opt=CutsceneSkip=3;3:opt=InfiniteMP=1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=1.5;"
              "8:tier=10;9:flag=205,1;10:saveload;11:flag=116,1;12:flag=117,1;13:flag=118,1;14:flag=182,3;15:flag=183,3;"
              "16:flag=184,3;17:bracelet=116;60:boss7;5200:every=10,20600,oktapf;20600:quit",
        expect=["flag 226 = 1", ">> entering S_7000", "ChangeScene 'demo5\\' -> S_7002", ">> entering S_0003",
                "ChangeScene 'demo7\\' -> S_0003", "[clear] EventXX(4): GameFirstPlay = 1, clear flag 298 = 1",
                "record.ta2 saved", "ChangeScene 'Title\\' -> the title", "[title] clear data: Yunica 00 Hugo 01 Toal 00"]),
    "ending_chain_yunica": dict(
        room="S_70/S_7080/S_7080", env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="1:opt=CutsceneSkip=3;3:opt=InfiniteMP=1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=1.5;"
              "8:tier=10;9:flag=205,1;10:saveload;11:flag=116,1;12:flag=117,1;13:flag=118,1;14:flag=182,3;15:flag=183,3;"
              "16:flag=184,3;17:bracelet=116;60:boss7;7900:every=10,23300,oktapf;23300:quit",
        expect=["flag 226 = 1", ">> entering S_7000", "ChangeScene 'demo5\\' -> S_7002", ">> entering S_0003",
                "ChangeScene 'demo7\\' -> S_0003", "[clear] EventXX(4): GameFirstPlay = 1, clear flag 297 = 1",
                "record.ta2 saved", "ChangeScene 'Title\\' -> the title", "[title] clear data: Yunica 01 Hugo 00 Toal 00"]),
    # Zava's first visit as Hugo: the event box, the intro played (4841 ticks), then the pilot wins.
    "zava_hugo_intro": dict(
        room="S_60/S_6082/S_6082", env={"CLERIA_CHAR": "2", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="1:opt=CutsceneSkip=3;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.45;8:tier=10;"
              "11:flag=163,1;20:at3=-34,8,15.5,0;40:every=10,4870,oktapf;4880:scripts;4890:boss6;12500:quit",
        expect=["slot 0 taken: 1/0", "slot 0 released: 0/0", "t4880 scripts", "script Battle_Hugo pc 38",
                "Zava phase 2 action 0", "flag 225 = 1", "head a destroyed"]),
    # S_6004 walked as Yunica and as Hugo with real inputs (ROUTE_F47_*.md): route_s6004's way — both plates, the four
    # falling steps, the leap over the hub (a gap of 7.5 .. 8.0 at y 14; their running jump + late double jump
    # reaches 10.26 / 9.12 on the flat, no dash) — by the closed-loop pilot `hops=` (the stick at the next waypoint,
    # Jump at an edge or a rise, the second press late), so no tick is timed for a run speed. Toal's own hand-timed
    # case stays; the pilot overshoots step_01 at his speed.
    "route_s6004_yunica": dict(
        room="S_60/S_6004/S_6004", char="1", env=_LOCK,
        drive="2:diff=3;3:flag=163,1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=44;20:killall;30:obj=switch02,0;370:killall;371:obj=switch01,0;710:killall;715:at3=-66,6,72,0;730:hops=-58.3,79.7/-68.7,84.9/-51.4,83.7/-38.3,83.8/-30.5,83.84/-16.3,84.0/-9.9,75.8/7.0,75.93/11.6,76.0/19.6,87.7/17.2,98.1/3.26,97.66/-4.5,97.45/-21.7,96.9/-36.2,97.0/-36.1,112/-36.07,134.5/-37.8,163.2;3300:quit",
        expect=["[plate] step_01 -> @fall_001", "[plate] step_02 -> @fall_001", "waypoint 7 reached at (6.", "14.00, 75.93)",
                "[plate] step_04 -> @fall_001", "[plate] step_03 -> @fall_001", "waypoint 17 reached", ">> entering S_6052"],
        absent=["NOT reached"]),
    "route_s6004_hugo": dict(
        room="S_60/S_6004/S_6004", char="2", env=_LOCK,
        drive="2:diff=3;3:flag=163,1;4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=44;20:killall;30:obj=switch02,0;370:killall;371:obj=switch01,0;710:killall;715:at3=-66,6,72,0;730:hops=-58.3,79.7/-68.7,84.9/-51.4,83.7/-38.3,83.8/-30.5,83.84/-16.3,84.0/-9.9,75.8/7.0,75.93/11.6,76.0/19.6,87.7/17.2,98.1/3.26,97.66/-4.5,97.45/-21.7,96.9/-36.2,97.0/-36.1,112/-36.07,134.5/-37.8,163.2;3300:quit",
        expect=["[plate] step_01 -> @fall_001", "[plate] step_02 -> @fall_001", "waypoint 7 reached at (6.", "14.00, 75.93)",
                "[plate] step_04 -> @fall_001", "[plate] step_03 -> @fall_001", "waypoint 17 reached", ">> entering S_6052"],
        absent=["NOT reached"]),
    # S_7099 (Darm) has no floor for Hugo or Yunica, and S_7000 has a SEN door to it. Nobody walks through it: a
    # collision wall at z -3.98 stops a running, double-jumping Hugo short of the door at z 10.1 (Toal enters by
    # EVT_7000_THOR_2's ChangeScene). The room root starts no script that opens anything there for any character.
    "s7000_no_walk_to_s7099": dict(
        room="S_70/S_7000/S_7000", char="2", env=_LOCK,
        drive="1:opt=InfiniteHP=1;1:opt=CutsceneSkip=3;2:flag=163,1;3:flag=181,1;10:move=0,1,400;50:jump;60:jump;64:jump;"
              "70:pos;300:pos;301:quit",
        expect=["feet (-31.881, 4.899, -3.975)", "feet (-31.883, 0.000, -3.975)"], absent=[">> entering"]),
    # The one element-gated object of floors 4-7 (drive `objs` over every room): S_4006's crumbling wall kabe_00,
    # resists 1 / 1 / 0 / 1 — class 2 only. Hugo's Trap Mine (an Eye's 0x88 shot) and Yunica's Bolt Crash break it.
    "kabe_s4006_hugo": dict(
        room="S_40/S_4006/S_4006", char="2", env=dict(_LOCK, CLERIA_DMG="1"),
        drive="5:opt=InfiniteHP=1;6:opt=DamageTaken=0;8:bracelet=117;9:flag=117,1;15:killall;20:obj=kabe_00;30:skill+;32:skill-;"
              "33:abil;200:quit",
        expect=["[abil] t33 sec 0x10c", "fires 0x88 (class 2)", "[hit] kabe_00 -> s_kabe00"]),
    "kabe_s4006_yunica": dict(
        room="S_40/S_4006/S_4006", char="1", env=_LOCK,
        drive="5:opt=InfiniteHP=1;6:opt=DamageTaken=0;8:bracelet=117;9:flag=117,1;15:killall;20:obj=kabe_00;30:atk;50:atk;"
              "70:atk;95:pos;100:skill+;102:skill-;240:obj=kabe_00;242:skill+;244:skill-;400:quit",
        expect=["t95 pos", "[hit] kabe_00 -> s_kabe00"]),
    # The double jump (163) is S_4002's chest box_01, behind the room's ward, for every character.
    "djump_chest_s4002_yunica": dict(
        room="S_40/S_4002/S_4002", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;5:opt=InfiniteHP=1;8:killall;20:every=10,500,oktapf;515:flagdump;520:obj=box_01;530:act;"
              "540:every=10,1200,oktapf;1200:scripts;1201:flagdump;1202:quit",
        expect=["chest box_01 -> S_BOX01.XSO", "t515 flagdump", "[probe] box_01 -> s_box01", "frozen 0 fix 0/0", " 163=1 "],
        count={" 163=1 ": 1}),          # not in the dump before the chest
    "djump_chest_s4002_hugo": dict(
        room="S_40/S_4002/S_4002", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;5:opt=InfiniteHP=1;8:killall;20:every=10,500,oktapf;515:flagdump;520:obj=box_01;530:act;"
              "540:every=10,1200,oktapf;1200:scripts;1201:flagdump;1202:quit",
        expect=["chest box_01 -> S_BOX01.XSO", "t515 flagdump", "[probe] box_01 -> s_box01", "frozen 0 fix 0/0", " 163=1 "],
        count={" 163=1 ": 1}),          # not in the dump before the chest
    # Boost and the super in a floor-4 room (S_4001, level 40). Yunica: the Boost press, then the second press runs
    # her super (section 0x139) and it lands on the room's monsters (the combo counter); Hugo: section 0x12c and the
    # barrier — a hit 100 ticks in is a GUARD.
    "kit_f4_yunica": dict(
        room="S_40/S_4001/S_4001", char="1", env=_LOCK,
        drive="2:level=40;3:flag=187,1;4:flag=188,1;5:opt=InfiniteHP=1;12:boost=30000;20:boostkey;22:abil;90:boostkey;91:abil;"
              "300:abil;400:abil;401:quit",
        expect=["[abil] t22 sec 0x2 mp 3000 lag 3000 boost 29940 boosting 1", "[abil] t91 sec 0x139", "[abil] t300 sec 0x139",
                "combo 3/", "[abil] t400 sec 0x2"]),
    "kit_f4_hugo": dict(
        room="S_40/S_4001/S_4001", char="2", env=_LOCK,
        drive="2:level=40;3:flag=187,1;4:flag=188,1;5:opt=InfiniteHP=1;8:killall;12:boost=30000;20:boostkey;22:abil;90:boostkey;"
              "91:abil;190:hurt=60;191:stats;300:abil;301:quit",
        expect=["[abil] t22 sec 0x2 mp 3000 lag 3000 boost 29940 boosting 1", "[abil] t91 sec 0x12c", "[popup] 0 ff0000",
                "Chr_Damage player atk 60 -> HP 284", "[abil] t300 sec 0x2"]),
    # R8's last link, the pause book itself: Esc, the Item page (Right, Confirm), Down, Down, Right onto the Dreaming
    # Idol, Confirm, Yes -> @UseSekizou -> s_Evt5080_Yunica_1 -> the Mantid Medallion (82) and 271.
    "route_s5080_yunica_idol_book": dict(
        room="S_50/S_5080/S_5080", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;5:flag=104,1;6:flag=270,1;7:flag=268,1;8:flag=105,1;20:at3=-61.1,3,40,0;60:esctap;"
              "110:right;140:ok;170:down;182:down;194:right;226:ok;266:ok;306:scripts;320:every=10,4500,oktapf;4500:scripts;"
              "4501:flagdump;4502:quit",
        expect=["use -> @UseSekizou", "script s_Evt5080_Yunica_1", "t4500 scripts", "frozen 0 fix 0/0", " 82=1 ", " 271=1 "]),
    # The ending scene and the clear write, per character: S_0003's Evt_0001_<name> -> demo7 -> Clear_Event_1, whose
    # EventXX(4) sets the character's clear flag and byte and saves record.ta2 (in the case's own save folder).
    "ending_hugo": dict(
        room="S_00/S_0003/S_0003", char="2",
        drive="1:opt=CutsceneSkip=3;40:every=10,8400,ok;8400:quit",
        expect=["ChangeScene 'demo7\\' -> S_0003", "[clear] EventXX(4): GameFirstPlay = 1, clear flag 298 = 1",
                "[clear] clear bytes Yunica 00 Hugo 01 Toal 00, 1 cleared tier(s); record.ta2 saved"]),
    "ending_yunica": dict(
        room="S_00/S_0003/S_0003", char="1",
        drive="1:opt=CutsceneSkip=3;40:every=10,8900,ok;8900:quit",
        expect=["ChangeScene 'demo7\\' -> S_0003", "[clear] EventXX(4): GameFirstPlay = 1, clear flag 297 = 1",
                "[clear] clear bytes Yunica 01 Hugo 00 Toal 00, 1 cleared tier(s); record.ta2 saved"]),
    # ROUTE_F47 R8, Yunica's item gates. S_4021's evil door: the Evil Ring held (Flag_IsEquip(94)) and 93 == 1 ->
    # Look_Door -> EV_4021_Yunica_2 -> S_4017's EV_4017_Yunica_1. Hugo's branch of the same door: EV_4021_Hugo_2.
    "route_s4021_yunica": dict(
        room="S_40/S_4021/S_4021", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;5:flag=93,1;6:held=94;7:flag=94,1;20:at3=0,0,6,0;25:move=0,1,10;40:act;41:scripts;"
              "60:every=10,6000,ok;6000:scripts;6001:quit",
        expect=["[probe] actor 166 -> look_door", "script EV_4021_Yunica_2", ">> entering S_4017", "t6000 scripts",
                "script EV_4017_Yunica_1"]),
    "route_s4021_hugo": dict(
        room="S_40/S_4021/S_4021", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;5:flag=93,1;6:held=94;7:flag=94,1;20:at3=0,0,6,0;25:move=0,1,10;40:act;41:scripts;"
              "60:every=10,6000,ok;6000:scripts;6001:quit",
        expect=["[probe] actor 166 -> look_door", "script EV_4021_Hugo_2", ">> entering S_4017", "t6000 scripts",
                "script EV_4017_Hugo_1"]),
    # ... S_5080: the Dreaming Idol used by the statue (the book's use of item 0x69 starts @UseSekizou; started here by
    # the drive, the book's own press is not driven) with 104 / 270 set, inside the 10-unit sphere: s_Evt5080_Yunica_1
    # plays (slot 1, 4070 ticks) and gives the Mantid Medallion (82) and 271.
    "route_s5080_yunica_idol": dict(
        room="S_50/S_5080/S_5080", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;5:flag=104,1;6:flag=270,1;7:flag=268,1;20:at3=-61.1,3,40,0;40:run=@UseSekizou;41:scripts;"
              "60:every=10,4200,ok;4200:scripts;4201:flagdump;4202:quit",
        expect=["slot 1 taken: 0/1", "script s_Evt5080_Yunica_1", "slot 1 released: 0/0", "t4200 scripts", "frozen 0 fix 0/0",
                " 82=1 ", " 271=1 "]),
    # ROUTE_F47 R1 as Yunica: S_4005's Karon event encloses her and waits for 180 == 6 exactly; the six Karons killed,
    # the count is 6 and Battle_Yunica goes on (it takes its Fix again).
    "karon_event_yunica": dict(
        room="S_40/S_4005/S_4005", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:every=10,2700,ok;"
              "2751:actors;2760:opt=OneHitKill=1;2770:every=12,3000,nearest;2771:every=12,3000,atk;3000:scripts;"
              "3001:flagdump;3002:quit",
        expect=["slot 0 released: 0/0", "[actor] id 172 cast 60 enemy hp 261", "slot 0 taken: 1/0", "t3000 scripts",
                "script Battle_Yunica", " 180=6 "]),
    # XSO op 0x161 Chr_SlantFire (FUN_00571ca0: +0x4ec = degrees x 0.0174533; it was unhandled): Toal's scene in S_5097,
    # walked into and played with Confirm — Hugo's Eye 167 is slanted -5 degrees at pc 1803 (seen at t7250), the pitch
    # anime_GenerateEffect gives its attached shot effect [EXPORT 004496f0.c:106].
    "slant_fire_s5097": dict(
        room="S_50/S_5097/S_5097", char="3", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=InfiniteHP=1;10:move=0,1,400;40:every=10,7300,ok;7200:chr=167;7300:chr=167;7301:quit",
        expect=["slot 0 taken: 1/0", "t7300 chr=167", "[chr] 167 slant -5.0 deg (+0x4ec)"]),
    # A skipped cutscene leaves what the same scene leaves played to its end (build/shell_re/ACCESS.md §1.5; retail has
    # no skip, so this is CleriaCore's own rule). Each case below skips with one Esc and pins what the survey of
    # 2026-10-12 read off the PLAYED scene after its Fix (tools: the scratch surveys named in ACCESS.md §1.5).
    # The camera. S_1000's opening, skipped on its third tick: the rig ends in the sticky slot 3 the played scene ends
    # in, and no slot was rewritten (the slots' sum 151.807). Before: slot 2, dist 21 / pitch 0.7156 / fov 0.5411, sum
    # 145.807 — the scene's Camera_Change had edited the slot the player stood in, the rig never having stepped.
    "skip_cam_s1000": dict(
        room="S_10/S_1000/S_1000", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;2:every=4,70,flag=131,1;3:esctap;60:cam;61:quit",
        expect=["cutscene skip (single)", "[cam] cur 3 from 3 forced -1 t 1.00 scroll 0.00>0.00 shake 0",
                "slot dist 24.00 yaw 0.0000 pitch 0.6632 fov 0.5236 | slots 151.807"]),
    # ... and a skip stops at a script menu (it was waved through with no row chosen): S_3015's EV_3015_HUGO_2 asks
    # (SentakuMenu); the skip at t22 leaves the menu up, `ok` answers it, a second skip ends the scene with the
    # camera returned as when played (before: slot 1 forced, the scroll at 1.00, for good).
    "skip_stops_at_menu": dict(
        room="S_30/S_3015/S_3015", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;20:run=EV_3015_HUGO_2;22:esctap;60:scripts;100:every=15,200,ok;201:scripts;210:skiptap;"
              "300:scripts;301:cam;302:quit",
        expect=["cutscene skip (single)", "t60 scripts", "frozen 1 fix 2/0", "script SentakuMenu pc 12", "t300 scripts",
                "frozen 0 fix 0/0", "[cam] cur 0 from 0 forced -1 t 1.00 scroll 0.00>0.00 shake 0"],
        count={"cutscene skip (single)": 2}),
    # Zava's intro skipped on its third tick (it was: the heads under the floor at y -2, `Summon` playing on over the
    # fight — Wait_Script was waved through and nothing stepped the scripted objects): the heads are up at y 6.1
    # (6.08: the brain's first bob) on the first fight tick, Summon is over, and zava_fight's pilot wins the fight.
    "skip_intro_zava_fight": dict(
        room="S_60/S_6082/S_6082",
        env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="4:opt=DamageTaken=0;5:opt=InfiniteHP=1;6:level=50;7:opt=DamageDealt=0.45;8:tier=10;9:opt=CutsceneSkip=0;"
              "11:flag=163,1;20:at3=-34,8,15.5,0;22:esctap;24:scripts;24:mesh=boss06a;24:mesh=boss06b;60:boss6;6500:quit",
        expect=["cutscene skip (single)", "t24 scripts", "frozen 0 fix 0/0", "script Battle_Yunica pc 38",
                "mesh boss06a pos (-39.46, 6.08, 30.00)", "mesh boss06b pos (-28.82, 6.08, 30.00)",
                "Zava phase 1 action 1 hp 6000/6000", "Zava phase 2 action 0", "flag 225 = 1", "head b destroyed", "head a destroyed"],
        absent=["script Summon"]),
    # One skipped intro per boss and per duel room: the gauge, the fight flags, the fight's script and the forced
    # camera slot 2 ticks after the Fix are the played intro's. (The Retry start, flag 205, is not the reference: it
    # runs another branch of the same scripts and differs from the played start itself — the slots it never edits.)
    # S_3080 as Hugo was broken at the start and the middle before: Epona, whose walk off the stage nobody waits
    # for, still stood there, and her handler's defeat path ended Toal's duel five ticks in (`Battle_Lose`).
    "skip_intro_jenocres": dict(
        room="S_40/S_4013/S_4013", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;3:esctap;5:cam;5:scripts;5:gauge;5:dflags;7:quit",
        expect=["cutscene skip (single)", "[cam] cur 6 from 6 forced 6", "t5 scripts", "frozen 0 fix 0/0", "[drive]   script Battle_Yunica pc 52", "[drive] gauge 1 id 168 hp 5869/5869", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_nygtilger": dict(
        room="S_20/S_2099/S_2099", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;3:esctap;5:cam;5:scripts;5:gauge;5:dflags;7:quit",
        expect=["cutscene skip (single)", "[cam] cur 0 from 0 forced 0", "t5 scripts", "frozen 0 fix 0/0", "[drive]   script BuruBuru pc 18", "[drive]   script Battle_Yunica pc 33", "[drive] gauge 1 id -6 hp 7000/7000", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_gelaldy": dict(
        room="S_30/S_3099/S_3099", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:run=s_Evt3099_2;22:esctap;24:cam;24:scripts;24:gauge;24:dflags;26:quit",
        expect=["cutscene skip (single)", "[cam] cur 5 from 5 forced 5", "t24 scripts", "frozen 0 fix 0/0", "[drive]   script BuruBuru pc 18", "[drive]   script Battle pc 30", "[drive] gauge 1 id -7 hp 1500/1500", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_khonsclard": dict(
        room="S_40/S_4099/S_4099", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at=-2.8,23.3,0;22:esctap;24:cam;24:scripts;24:gauge;24:dflags;26:quit",
        expect=["cutscene skip (single)", "[cam] cur 3 from 3 forced 3", "t24 scripts", "frozen 0 fix 0/0", "[drive]   script BuruBuru pc 18", "[drive]   script Battle_1 pc 30", "[drive] gauge 1 id -8 hp 3000/3000", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_pictimos": dict(
        room="S_50/S_5099/S_5099", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;10:move=0,1,200;16:esctap;18:cam;18:scripts;18:gauge;18:dflags;20:quit",
        expect=["cutscene skip (single)", "[cam] cur 3 from 3 forced 3", "t18 scripts", "frozen 0 fix 0/0", "[drive]   script Battle_Start pc 34", "[drive] gauge 1 id -9 hp 4000/4000", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_dalles": dict(
        room="S_70/S_7080/S_7080", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;11:flag=116,1;12:flag=117,1;13:flag=118,1;3:esctap;5:cam;5:scripts;5:gauge;5:dflags;7:quit",
        expect=["cutscene skip (single)", "[cam] cur 1 from 1 forced 1", "t5 scripts", "frozen 0 fix 0/0", "[drive]   script Battle pc 23", "[drive] gauge 1 id -11 hp 2500/2500", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_darm": dict(
        room="S_70/S_7099/S_7099", char="3", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;11:flag=163,1;3:esctap;5:cam;5:scripts;5:gauge;5:dflags;7:quit",
        expect=["cutscene skip (single)", "[cam] cur 1 from 1 forced 1", "t5 scripts", "frozen 0 fix 0/0", "[drive]   script Battle pc 30", "[drive] gauge 1 id -12 hp 9999/9999", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_duel1080_y": dict(
        room="S_10/S_1080/S_1080", char="1", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;21:move=0,-1,300;221:esctap;223:cam;223:scripts;223:gauge;223:dflags;225:quit",
        expect=["cutscene skip (single)", "[cam] cur 1 from 1 forced 1", "t223 scripts", "frozen 0 fix 0/0", "[drive]   script s_Evt1080 pc 82", "[drive]   script Battle_1 pc 56", "[drive] gauge 1 id 167 hp 1860/1860", "[duel] flags 130=0 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_duel3080_h": dict(
        room="S_30/S_3080/S_3080", char="2", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at=-12,36,0;30:move=0,1,30;22:esctap;24:cam;24:scripts;24:gauge;24:dflags;26:quit",
        expect=["cutscene skip (single)", "[cam] cur 3 from 3 forced 3", "t24 scripts", "frozen 0 fix 0/0", "[drive]   script Battle pc 55", "[drive] gauge 1 id 198 hp 3550/3550", "[duel] flags 130=1 131=0 133=1 156=1 174=0 230=0 232=0 311=0"]),
    "skip_intro_duel6099_t": dict(
        room="S_60/S_6099/S_6099", char="3", env=_LOCK,
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;5:flag=231,1;40:move=0,1,300;244:esctap;246:cam;246:scripts;246:gauge;246:dflags;248:quit",
        expect=["cutscene skip (single)", "[cam] cur 4 from 4 forced 4", "t246 scripts", "frozen 0 fix 0/0", "[drive]   script Battle_Thor pc 56", "[drive] gauge 1 id 197 hp 4186/4186", "[duel] flags 130=1 131=0 133=0 156=1 174=0 230=0 232=0 311=0"]),
    # Dalles after a skipped intro leaves the loop clip METEOR the scene leaves him in and acts: his first shot comes
    # (played: 62 ticks after the Fix; skipped: once his brain has run METEOR out, about 200 ticks).
    "skip_intro_dalles_acts": dict(
        room="S_70/S_7080/S_7080", env={"CLERIA_CHAR": "1", "CLERIA_BOSSLOG": "1", "CLERIA_DRIVE_LOCKSTEP": "1"},
        drive="1:opt=CutsceneSkip=0;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;3:esctap;400:quit",
        expect=["cutscene skip (single)", "the fight starts at", "clip METEOR_FROM_WAIT", "clip N_WAIT", "clip N_SHOOT_"]),
    # "Always" and what the player starts herself (ACCESS.md §1.3 row 4): the blessing shop walked as `blessing`
    # walks it, with real presses — every menu and box takes its own press, the one purchase is made (SP 250 - 100)
    # and nothing is skipped. Before, each answer set the skip off again and the next choice was up under the press
    # meant for a box.
    "access_always_shop": dict(
        room="S_10/S_1009/S_1009",
        drive=CLEANSE + ";190:opt=CutsceneSkip=4;200:sp=250;220:act;290:down;300:oktap;400:oktap;480:oktap;570:oktap;"
              "650:oktap;651:scripts;730:oktap;820:esc;830:scripts;835:stats;840:quit",
        expect=["[probe] save_03 -> @save", "script @GrowMenu", "script @GrowYes", "t830 scripts", "frozen 0 fix 0/0",
                "sp 150"],
        absent=["cutscene skip"]),
    # ... and a menu an auto-skip lands on is the window's own: Confirm held from before the scene and through the
    # skip answers nothing (FUN_005789a0: the fade-in, then a release, then a press), a fresh press does — the skip
    # is logged only after it (the answered menu's script runs on and is skipped back to the list).
    "access_always_menu_held": dict(
        room="S_10/S_1004/S_1004", env=_LOCK,
        drive="8:opt=CutsceneSkip=4;15:every=1,200,oktap;20:run=@growmenu;210:scripts;230:oktap;260:scripts;261:quit",
        expect=["slot 0 taken: 1/0", "t210 scripts", "frozen 1 fix 1/0", "script @growmenu pc 1165", "t230 oktap",
                "[access] cutscene skip (always)", "t260 scripts"],
        count={"[access] cutscene skip (always)": 1}),
    # Toal's Godspeed, tapped (bracelet 116, section 0x10d; ABILITIES_VERIFIED.md §3.3), in S_1002's flat corridor with
    # the monsters gone (no lock-on). On the ground: 11 ticks x 0.56 = 6.16 u. From a jump the dash's step is added to
    # the mover's ordinary step of the same tick [BYTES 0x4697f9..0x469823], which in the air is the carried velocity:
    # a standing jump lands 6.564 u on (it was 4.606: the dash's own blend alone), a running jump with the stick
    # released 6.878 u from the jump (was 4.921). Gravity is not held for Toal [EXPORT 00465170.c:2464-2484].
    "godspeed_tap_ground": dict(
        room="S_10/S_1002/S_1002", char="3", env=_LOCK,
        drive=_GODSPEED + "40:pos;41:skill+;43:skill-;45:abil;120:pos;140:quit",
        expect=["feet (20.000, -4.098, -37.000)", "[abil] t45 sec 0x10d", "feet (26.160, -4.098, -37.000)"]),
    "godspeed_tap_jump": dict(
        room="S_10/S_1002/S_1002", char="3", env=_LOCK,
        drive=_GODSPEED + "40:pos;41:jump;50:skill+;52:skill-;54:abil;120:pos;140:quit",
        expect=["feet (20.000, -4.098, -37.000)", "[abil] t54 sec 0x10d", "feet (26.564, -4.098, -37.000)"]),
    "godspeed_tap_run_jump": dict(
        room="S_10/S_1002/S_1002", char="3", env=_LOCK,
        drive=_GODSPEED + "34:move=1,0,8;40:pos;41:jump;50:skill+;52:skill-;120:pos;140:quit",
        expect=["feet (20.942, -4.098, -37.000)", "feet (27.820, -4.098, -37.000)"]),
    # ACCESS.md §1.3, CutsceneSkip = 4 "Always" (CleriaCore's own, not retail): no press. A scene the player did not
    # start (S_3015's event, run by the drive) is skipped on the tick its Fix is taken, through the pressed skip's
    # own path.
    "access_skip_always": dict(
        room="S_30/S_3015/S_3015", char="1", env=_LOCK,
        drive="8:opt=CutsceneSkip=4;20:run=EV_3015_YUNICA_1;25:scripts;90:quit",
        expect=["[access] cutscene skip (always)", "t25 scripts", "frozen 0 fix 0/0"],
        count={"[access] cutscene skip (always)": 1}),
    # ... and one she starts with the action button is hers (since 2026-10-12; it was skipped unread): the statue
    # cleanse of access_skip_double plays, and one press of the menu button skips it as under Single press.
    "access_skip_always_talk": dict(
        room="S_10/S_1009/S_1009",
        drive="5:flag=310,1;6:flag=84,1;8:opt=CutsceneSkip=4;10:at=-0.036,8.2,0;20:act;25:scripts;40:esctap;45:scripts;"
              "46:pos;90:quit",
        expect=["t25 scripts", "frozen 1 fix 0/1", "script save pc 87", "[access] cutscene skip (single)", "t45 scripts",
                "frozen 0 fix 0/0", "feet (-0.036, 2.000, 8.200)"],
        absent=["cutscene skip (always)"]),
    # ... and it stops at a script menu: the blessing shop's list stays up for its answer (no skip is logged).
    "access_skip_always_menu": dict(
        room="S_10/S_1004/S_1004", env=_LOCK,
        drive="8:opt=CutsceneSkip=4;20:run=@growmenu;60:scripts;100:quit",
        expect=["slot 0 taken: 1/0", "t60 scripts", "frozen 1 fix 1/0", "script @growmenu pc 1165"],
        absent=["cutscene skip"]),
    # ... and the opening with no input at all: the three movies and every scene of the chain skip by themselves.
    "opening_skip_always": dict(
        room="S_00/S_0000/S_0000", env=dict(CLERIA_MOVIE="1"),
        drive="1:opt=CutsceneSkip=4;40:every=50,1600,killall;1600:scripts;1601:quit",
        expect=["demo1 skipped", "demo2 skipped", ">> entering S_0002", "cutscene skip (always)", "demo3 skipped",
                ">> entering S_1000", "cutscene skip (always)", "t1600 scripts", "frozen 0 fix 0/0", "script s_Stop1000_03"],
        absent=["script battle_", "script BattleStart1", "script Prologue_Event", "ended at"]),
})

# The Archipelago cases, played by the Lua mod samples/mods/archipelago_lua against a local scripted AP
# server (tools/aplua_regress.py, LUA_API.md §9).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aplua_regress  # noqa: E402
# ---- batch G (2026-10-11) ----------------------------------------------------------------------------
CASES.update({
    # Video: the water caustics (CONFIG_PARITY.md 13). A flooded room draws them room-wide, the frame steps every
    # 2 ticks and the scroll every tick; "Disable water caustics" takes the room-wide part off at once and back.
    "caustics_live": dict(
        room="S_20/S_2008/S_2008",
        drive="10:vstat;11:vopt=WaterCaustics=1;20:vstat;21:vopt=WaterCaustics=0;30:vstat;31:quit",
        expect=["[video] caustics room 1 off 0 frame 4 scroll 0.0045 0.00135",
                "[video] caustics room 0 off 1 frame 9 scroll 0.0095 0.00285",
                "[video] caustics room 1 off 0 frame 14 scroll 0.0145 0.00435"]),
    # Video: the water surfaces (REFLECTIONS.md 5). Reflections High draws them through a full-size grab of the
    # frame, Low through a half-size one, Off as the plain sheet; a room without water takes no grab.
    "water_levels": dict(
        room="S_20/S_2000/S_2000",
        drive="10:vstat;11:vopt=WaterEffectLevel=1;20:vstat;21:vopt=WaterEffectLevel=0;30:vstat;"
              "31:vopt=WaterEffectLevel=2;40:vstat;41:quit",
        expect=["[video] water level 2 surfaces 1 grab 1280x800", "[video] water level 1 surfaces 1 grab 640x400",
                "[video] water level 0 surfaces 0 grab 0x0"]),
    # Video: the actors' round shadows (SHADOWS.md 2). Full lays the shadow of every actor in range on the room's
    # triangles (decals), Player only the player's, Simple draws flat quads; switched while the game runs.
    "shadow_levels": dict(
        room="S_60/S_6015/S_6015",
        drive="30:vstat;31:vopt=ShadowEffectLevel=1;35:vstat;36:vopt=ShadowEffectLevel=0;40:vstat;41:quit",
        expect=["[video] shadows level 2 decals 2 (16 triangles) quads 28", "[video] shadows level 1 decals 1 (8 triangles) quads 29",
                "[video] shadows level 0 decals 0 (0 triangles) quads 30"]),
    # The spinning blades of S_3005 / S_3008 (fire00.., s30fire0: section 0 'DEFAULT', 120 frames, looping, 0.25 a
    # tick): a named object no script sets in motion plays its section 0 from the load. The cursor advances, the
    # hull turns with the blade (standing beside the hub, off the blade's rest pose, she is touched: @Touch_Toge,
    # ATK 120), and an object a script does name stays the script's (S_4015's step_00 holds its frame).
    "blades_spin": dict(
        room="S_30/S_3005/S_3005", env=dict(CLERIA_DMG="1"),
        drive="20:mesh=fire00;120:mesh=fire00;121:mesh=fire02;125:at=2.5,34,0;600:pos;601:quit",
        expect=["mesh fire00 pos (-2.00, 2.00, 34.00) clip 0 t 4.8", "mesh fire00 pos (-2.00, 2.00, 34.00) clip 0 t 29.8",
                "mesh fire02 pos (52.00, 2.00, 34.00) clip 0 t 30.0", "fire00 -> @touch_toge", "[hurt] dmg 96 (atk 120"]),
    "blades_spin_s3008": dict(
        room="S_30/S_3008/S_3008", drive="120:mesh=fire00;121:mesh=fire06;122:quit",
        expect=["mesh fire00 pos (86.00, 4.00, 36.00) clip 0 t 29.8", "mesh fire06 pos (42.10, 1.00, 18.85) clip 0 t 30.0"]),
    "blades_named_by_script_hold": dict(
        room="S_40/S_4015/S_4015", drive="20:mesh=step_00;120:mesh=step_00;121:quit",
        expect=["mesh step_00 pos (-30.00, 26.00, 43.00) clip 1 t 90.0", "mesh step_00 pos (-30.00, 26.00, 43.00) clip 1 t 90.0"]),
    "caustics_dry_room": dict(
        room="S_10/S_1009/S_1009", drive="10:vstat;11:quit",
        expect=["[video] caustics room 0 off 0", "[video] water level 2 surfaces 0 grab 0x0"]),
})

# The sprite's AIA file +0x97c and op 0x167 Chr_AniChange (build/event_re/ANI_VARIANT.md, issue #92): `chr=` prints the
# file index after the pose. Yunica (196) lying in S_3015 is placed and pinned in one script step, so only the op's 6
# (C_102) reaches her; Toal (198) in S_5097 and S_7000 goes demon sheet 12 (C_115) -> 17 (C_113) for the knock-down
# tag 9 -> 8 (C_104) for the rise tag 410; Hugo (197) in S_6099 sits on 7 (C_103, chosen by section 319 in the one
# tick hugo_anime leaves before its pins — an NPC's script runs in play) and raises his staff on 16 (C_112).
_ANI7 = "1:opt=CutsceneSkip=3;1:opt=DamageTaken=0;1:opt=InfiniteHP=1;2:level=50;3:tier=10;20:at3=0,7.4,-27.8,0;40:every=10,3100,oktapf;"
CASES.update({
    "anichange_s3015_yunica_down": dict(
        room="S_30/S_3009/S_3009", char="3", env=_LOCK, drive="10:flag=251,1;20:exit=1;60:chr=196;61:quit",
        expect=[">> entering S_3015", "[chr] 196 ", "pin 405/0 motion -1 aso 2/-1/0", ") file 6"]),
    "anichange_s5097_toal": dict(
        room="S_50/S_5097/S_5097", char="3", env=_LOCK,
        drive="1:opt=CutsceneSkip=3;1:opt=InfiniteHP=1;10:move=0,1,400;40:every=10,10500,ok;9860:chr=198;9990:chr=198;10460:chr=198;10461:quit",
        expect=["pin -1/0", ") file 12", "pin 9/6", ") file 17", "pin 410/1", ") file 8"]),
    "anichange_s6099_hugo": dict(
        room="S_60/S_6099/S_6099", char="3", env=_F6,
        drive="5:flag=231,1;8:tier=10;9:opt=DamageTaken=0;40:move=0,1,300;80:every=20,2400,ok;100:chr=197;2256:chr=197;2340:chr=197;2341:quit",
        expect=["pin 302/3 motion 319 aso 319/", ") file 7", "pin 300/3", ") file 16", "pin 300/5", "aso 2/-1/0", ") file 16"]),
    "anichange_s7000_toal": dict(
        room="S_70/S_7000/S_7000", char="3", env=_LOCK,
        drive=_ANI7 + "2700:chr=198;2900:chr=198;3010:chr=198;3011:quit",
        expect=["pin 308/5", ") file 12", "pin 9/6", ") file 17", "pin 410/", ") file 8"]),
})

# Arena reach (build/mode_re/ARENA.md §12). Hugo's shot is FireBullet(2, 130, ..): homing period 1 with a max turn of
# 0, and the bullet's target scan FUN_00596c70 runs for effects 0x20 / 0x15 only [EXPORT 00593b90.c:176] — it flies
# level from where it was fired, so the fliers (Chelasken 42 and Vagully 41 on Stage 3, Diphtheloyd 39 on Stage 4) are
# shot from a jump. The pilot alternates 440 ticks of ground shots with 440 of jump shots (jump, fire 6 ticks later);
# both stages clear and bank with no sweep.
def _hugo_jumpshots(end):
    d = "10:opt=OneHitKill=1;11:opt=DamageTaken=0"
    for b in range(130, end, 900):
        d += (f";{b}:every=9,{b + 440},nearest=1.2;{b + 3}:every=9,{b + 440},atk;{b + 450}:every=60,{b + 890},nearest=2;"
              f"{b + 470}:every=60,{b + 890},jump;{b + 476}:every=60,{b + 890},atk")
    return d + f";{end}:stats;{end + 1}:quit"
_ARENA_HUGO = "1:keys;40:down;45:down;50:ok;100:down;110:ok;150:down;190:ok;270:ok;{rows}520:ok;700:quit"
# Menoak (cast 56), "Shell repels all physical attacks": its ENEMYINFO resists are 1, -1, -5, 1 and the damage is
# (1 - resist[class]) x raw [EXPORT 004fa550.c:43], the class from the section (FUN_0046d5e0: 0x10d -> 0, 0x10e -> 1,
# 0x10c -> 2, anything else 3). So a swing or a plain shot does 0, the bracelet-116 skill (section 0x10d) does 0, the
# 118 skill (0x10e) does x2 and the 117 skill (0x10c) x6 — with each character.
_MENOAK = ("5:flag=116,1;6:flag=117,1;7:flag=118,1;8:flag=182,1;9:flag=183,1;10:flag=184,1;12:level=20;13:opt=InfiniteHP=1;"
           "20:every=30,460,nearest=1.6;40:atk;55:atk;100:bracelet=116;120:skill+;122:skill-;124:abil;200:bracelet=118;220:skill+;"
           "222:skill-;224:abil;300:bracelet=117;320:skill+;322:skill-;324:abil;460:quit")
CASES.update({
    "arena_stage3_hugo_fliers": dict(
        room=None, char="2", env=dict(_LOCK, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[5, 6, 7, 8, 9, 10])},
        title=_ARENA_HUGO.format(rows="430:down;432:down;"), drive=_hugo_jumpshots(3400),
        expect=["(Guilty Fire): the run starts in S_9129", "[arena] Arena(cast 42,", "ArenaSave: SP ", "cleared in "]),
    "arena_stage4_hugo_fliers": dict(
        room=None, char="2", env=dict(_LOCK, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[5, 6, 7, 8, 9, 10])},
        title=_ARENA_HUGO.format(rows="430:down;432:down;434:down;"), drive=_hugo_jumpshots(4500),
        expect=["(Silent Sands): the run starts in S_9139", "[arena] Arena(cast 39,", "ArenaSave: SP ", "cleared in "]),
    "menoak_resists_yunica": dict(
        room="S_20/S_2008/S_2008", char="1", env=dict(_LOCK, CLERIA_DMG="1"), drive=_MENOAK,
        expect=["hit Menoak (lv14 DEF 91): 0 dmg, 146/146 left", "t124 sec 0x10d", "): 0 dmg, 146/146 left", "t224 sec 0x10e",
                "hit Menoak (lv14 DEF 91): 7", "t324 sec 0x10c", "hit Menoak (lv14 DEF 91): 159 dmg, 0/146 left"]),
    "menoak_resists_hugo": dict(
        room="S_20/S_2008/S_2008", char="2", env=dict(_LOCK, CLERIA_DMG="1"), drive=_MENOAK,
        expect=["hit Menoak (lv14 DEF 91): 0 dmg, 146/146 left", "t224 sec", "hit Menoak (lv14 DEF 91): 49 dmg"]),
    "menoak_resists_toal": dict(
        room="S_20/S_2008/S_2008", char="3", env=dict(_LOCK, CLERIA_DMG="1"), drive=_MENOAK,
        expect=["hit Menoak (lv14 DEF 91): 0 dmg, 146/146 left", "t124 sec 0x10d", "): 0 dmg, 146/146 left", "t224 sec 0x10e",
                "hit Menoak (lv14 DEF 91): 6", "t324 sec 0x10c", "hit Menoak (lv14 DEF 91): 18"]),
})

CASES.update(aplua_regress.CASES)
# The .cleriamod install cases: a mod file on the command line (tools/modfile_regress.py, MODFILE.md).
import modfile_regress  # noqa: E402
CASES.update(modfile_regress.CASES)

# --smoke: the quick tier for a per-commit check (under a minute with --jobs 8; the whole list is the
# pre-push check). One or two short cases per subsystem, picked by their measured time (2026-10-09, each
# under 16 s): a change that breaks a subsystem outright fails here; the long fights and routes do not run.
SMOKE = [
    "jump_wall_s1002", "arrival_floor", "swim_ceiling",                      # movement, a door, water
    "exp_even", "hugo_shot", "boost_iframes",                                # combat: a kill's EXP, a shot, Boost
    "reacq_teleport", "oggnome_wake_s2001", "keep_dy_limit", "chups_latch",  # an enemy brain on floors 1-4,
    "urobass_strikes", "far_freeze_keeps_phase",                             # 5 and 6
    "statue_swap", "break_script", "access_skip_double",                     # scripts: the cleanse, a breakable, a skip
    "saveload_floor", "gameover_retry",                                      # save / load, the continue snapshot
    "zava_single_jump", "boss_part_feedback",                                # a boss's start (Zava, Khonsclard)
    "duel_s1080_gameover",                                                   # a duel's start
    "book_return_title", "shell_subpages", "hud_damage_numbers",             # the book, the shell, the HUD
    "weather_embers", "video_passes", "video_looks",                         # weather, the Video page
    "config_live_video", "config_live_input", "config_live_audio",           # config.exe parity: options change live
    "config_ky_preset",
    "sfx_loop_scene_stop",                                                   # audio: a looping SE, the scene stop
    "content_chest", "lua_save", "lua_exp_floor", "modfile_install",         # mods: content, Lua, a .cleriamod
    "aplua_items_save",                                                      # Archipelago (its mock server)
]


# The frame's edges (the user report of a dark line at the left of the title, 2026-10-12: the left side strip ended on
# the field's edge and blended in the black texel past its art). A title boot at a frame size (CLERIA_DRIVE_SIZE): the
# title, the book over it (New Game: Character Select, the AltPillar strips) and the Bonus Shop (ShopPillar) are shot
# and measured by frame_edges — the first and last column and row are drawn, and on a screen wider than 4:3 the strips'
# inner columns match their neighbours (the title and the book: their strips are smooth there; the shop's are not).
for _w, _h in ((2560, 1440), (1920, 1080), (1366, 768), (1280, 800), (1024, 768)):
    CASES["frame_edges_%dx%d" % (_w, _h)] = dict(
        room=None, env=dict(CLERIA_NYA="1", CLERIA_DRIVE_SIZE="%dx%d" % (_w, _h)),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)]), "record.ar2": [0]},
        title="30:shot=title;40:ok;90:shot=book;100:esc;160:down;165:down;170:ok;220:down;225:down;230:ok;330:shot=shop;340:quit",
        drive="10:quit", edges={"title": True, "book": True, "shop": False},
        expect=["[shop] opened", "[edges] title %dx%d: ok" % (_w, _h),
                "[edges] book %dx%d: ok" % (_w, _h), "[edges] shop %dx%d: ok" % (_w, _h)])


def frame_edges(path, name, seams):
    """One line on a shot (a .ppm): '[edges] <name> WxH: ok', or what is wrong — an edge column / row that is mostly
    the clear colour (black), or (`seams`, wider than 4:3) a side strip's inner column that differs from the strip's
    next one by more than 8 levels."""
    try:
        blob = open(path, "rb").read()
    except OSError:
        return "[edges] %s: no shot" % name
    _, size, _, px = blob.split(b"\n", 3)
    w, h = map(int, size.split())
    col = lambda x: [px[x * 3 + c::w * 3] for c in range(3)]
    mean = lambda x: sum(map(sum, col(x))) / (3.0 * h)
    black = lambda rgb: sum(1 for p in zip(*rgb) if p == (0, 0, 0)) / float(len(rgb[0]))
    row = lambda y: [px[y * w * 3 + c:(y + 1) * w * 3:3] for c in range(3)]
    bad = ["%s %d is %d%% black" % (k, i, round(100 * f)) for k, i, f in
           (("column", 0, black(col(0))), ("column", w - 1, black(col(w - 1))), ("row", 0, black(row(0))),
            ("row", h - 1, black(row(h - 1)))) if f > 0.5]
    left = (w - 4 * h // 3) // 2
    if seams and left > 2:
        for x, nb in ((left - 1, left - 2), (w - left, w - left + 1)):
            if abs(mean(x) - mean(nb)) > 8:
                bad.append("column %d is %.1f, column %d %.1f" % (x, mean(x), nb, mean(nb)))
    return "[edges] %s %dx%d: %s" % (name, w, h, "; ".join(bad) or "ok")


# The cases of a minute or more (measured 2026-10-09): with --jobs they start first, so the longest one
# (darm_fight, ~7 minutes) does not start last and run on alone.
LONG = ["darm_fight", "duel_s1080_vagullion_long", "duel_s3080_toal_win", "duel_s6099_hugo", "dalles_fight",
        "duel_s3080_toal_lose", "duel_s3080_kishgal", "route_s4015", "duel_s3080_shion", "scene_light"]


def content_setup(case, env, tmp):
    """Mod content cases (build/shell_re/CONTENT.md): a scratch settings folder with the case's mods
    installed and turned on. `mods`: sample folders under samples/mods; `content`: {file: text} of a
    generated test mod "test.regress"; `lua`: the main.lua of a generated scripted mod "test.lua";
    `mods_off`: installed but turned off."""
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
    if case.get("lua"):
        d = os.path.join(mods, "regress_lua")
        os.makedirs(d)
        open(os.path.join(d, "mod.ini"), "w").write("[mod]\nId=test.lua\nApiVersion=3\nScript=main.lua\n")
        open(os.path.join(d, "main.lua"), "w").write(case["lua"])
        ids.append("test.lua")
    on = [] if case.get("mods_off") else ids
    open(os.path.join(tmp, "cleria.ini"), "w").write(
        '[cleria.mods]\nUseMods=1\nOrder="%s"\nActive="%s"\n' % (",".join(ids), ",".join(on)))
    open(os.path.join(tmp, "yso_win.ini"), "w").close()
    env["CLERIA_INI"] = os.path.join(tmp, "yso_win.ini")


def run(name, case, exe, assets):
    """One case, in its own temp folder (saves, shots, settings, an aplua case's server on a free port):
    cases share nothing, so they run side by side. Returns (passed, its report lines, seconds).
    `room=None` boots the title (no room argument), driven by `title` (CLERIA_TITLE_DRIVE); `records`:
    {file name: dwords} written into the save folder first (record_file)."""
    t0 = time.time()
    msg = []
    with tempfile.TemporaryDirectory() as saves:
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR=case.get("char", "1"), CLERIA_DRIVE=case["drive"],
                   CLERIA_SAVE_DIR=saves)
        env.update(case.get("env", {}))            # a case may override any of these (CLERIA_CHAR)
        env.setdefault("CLERIA_SHOT_DIR", saves)   # a case's `shot` / `shell` frames stay in its temp folder
        if "mods" in case or "content" in case or "lua" in case:
            content_setup(case, env, os.path.join(saves, "settings"))
        srv = aplua_regress.start(case, env, os.path.join(saves, "settings"))   # an `aplua` case: its server, the mod
        extra = modfile_regress.start(case, env, os.path.join(saves, "settings"))   # a `modfile` case: the file
        for f, dwords in case.get("records", {}).items():
            open(os.path.join(saves, f), "wb").write(record_file(dwords))
        if "title" in case:
            env["CLERIA_TITLE_DRIVE"] = case["title"]
        sob = [os.path.join(assets, "MAP", case["room"] + ".SOB")] if case.get("room") else []
        r = subprocess.run([exe, assets] + sob + extra, env=env, capture_output=True, text=True, timeout=900)
        out = r.stdout + r.stderr
        if "drive2" in case:        # a second boot on the same save folder (`room2`: its room)
            sob2 = os.path.join(assets, "MAP", case.get("room2", case["room"]) + ".SOB")
            r = subprocess.run([exe, assets, sob2] + extra, env=dict(env, CLERIA_DRIVE=case["drive2"]), capture_output=True,
                               text=True, timeout=900)
            out += "==== second boot" + chr(10) + r.stdout + r.stderr
        lost = [n for n in case.get("files", []) if not os.path.exists(os.path.join(saves, n))]   # must be in the save folder
        if lost:
            out = "MISSING FILE " + ", ".join(lost)
        for shot, seams in case.get("edges", {}).items():   # shots measured while the folder exists (frame_edges)
            out += chr(10) + frame_edges(os.path.join(env["CLERIA_SHOT_DIR"], shot + ".ppm"), shot, seams)
    ok = aplua_regress.finish(name, case, srv, msg.append) and check(name, case, r, out, msg.append)
    return ok, msg, time.time() - t0


def check(name, case, r, out, say):
    if out.startswith("MISSING FILE "):
        say(f"FAIL {name}: the save folder has no {out[13:]}")
        return False
    for e in case.get("absent", []):
        if e in out:
            say(f"FAIL {name}: unexpected '{e}'")
            return False
    for e, n in case.get("count", {}).items():      # exactly n times in the whole output
        if out.count(e) != n:
            say(f"FAIL {name}: '{e}' appears {out.count(e)} times, not {n}")
            return False
    at = 0
    for e in case["expect"]:
        k = out.find(e, at)
        if k < 0:
            say(f"FAIL {name}: missing '{e}' (after offset {at})")
            # Say how the run ended, so a flake shows its cause (a crash, a GPU loss, an early quit).
            tail = [l for l in r.stderr.splitlines() if l.strip()][-3:]
            say(f"     exit code {r.returncode}; last stdout line: {(r.stdout.splitlines() or [''])[-1][:160]!r}")
            for l in tail:
                say(f"     stderr: {l[:160]}")
            return False
        at = k + len(e)
    return True


def main():
    args = sys.argv[1:]
    opt = {"--exe": EXE, "--assets": ASSETS, "--jobs": str(min(8, max(1, (os.cpu_count() or 2) // 2)))}
    for k in opt:
        if k in args:
            i = args.index(k)
            opt[k] = args[i + 1] if k == "--jobs" else os.path.abspath(args[i + 1])
            del args[i:i + 2]
    smoke = "--smoke" in args
    if smoke:
        args.remove("--smoke")
    names = args or (SMOKE if smoke else list(CASES))
    workers = max(1, int(opt["--jobs"]))
    t0 = time.time()
    failed = []
    # Each worker thread waits on one cleria-view process; a case's lines are printed together when it ends.
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        order = sorted(names, key=lambda n: n not in LONG) if workers > 1 else names
        jobs = {pool.submit(run, n, CASES[n], opt["--exe"], opt["--assets"]): n for n in order}
        for j in concurrent.futures.as_completed(jobs):
            n = jobs[j]
            try:
                ok, msg, secs = j.result()
            except Exception as e:                 # the 900 s timeout, a missing exe
                ok, msg, secs = False, [f"FAIL {n}: {e}"], 0.0
            if ok:
                msg.append(f"ok   {n}")
            else:
                failed.append(n)
            msg[0] += f"  ({secs:.1f}s)"
            print("\n".join(msg), flush=True)
    if failed:
        print("failed: " + " ".join(sorted(failed, key=names.index)))
    print(f"{len(names) - len(failed)}/{len(names)} passed  ({time.time() - t0:.0f}s, --jobs {opt['--jobs']})")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
