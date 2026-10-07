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
        expect=["[gameover] gameover.dds", "[gameover] chose title", "Return to Title"]),
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
    # FLOOR13_OPS.md: a SOB trigger-4 touch volume (the S_1011 ward) runs Touch_kekkai -> 0x15c Chr_Damage.
    "ward_touch": dict(
        room="S_10/S_1011/S_1011", drive="30:obj=kekkai_01;32:hp=999;35:move=-1,1,40;80:quit",
        expect=["[touch] kekkai_01 -> touch_kekkai", "Chr_Damage player atk 50 -> HP"]),
    # FLOOR13_OPS.md: a breakable runs its own @Break_30 (0x40 / 0xb5 / 0x17c) instead of the stand-in.
    "break_script": dict(
        room="S_10/S_1001/S_1001", drive="30:obj=break_01;45:atk;60:scripts;150:scripts;160:quit",
        expect=["[hit] break_01 -> @break_30", "script @break_30", "t150 scripts"]),
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
                "[abil] t210", "combo 6/"]),
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
        r = subprocess.run([exe, assets, sob] + extra, env=env, capture_output=True, text=True, timeout=300)
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
