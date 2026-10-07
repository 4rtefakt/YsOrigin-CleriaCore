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
    # credits 66 (retail 33), Damage taken 0 keeps the 60 HP it would otherwise lose some of.
    "access_cheats": dict(
        room="S_10/S_1002/S_1002",
        drive="8:opt=DamageDealt=10;9:opt=ExpRate=2;10:opt=DamageTaken=0;15:stats;" +
              ";".join("%d:near=17;%d:atk" % (t, t + 1) for t in range(20, 400, 25)) + ";420:stats;430:quit",
        expect=["stats exp 0 lv 1 hp 60/60", "stats exp 66 lv 1 hp 60/60"]),
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
}


def run(name, case, exe, assets):
    with tempfile.TemporaryDirectory() as saves:
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR="1", CLERIA_DRIVE=case["drive"],
                   CLERIA_SAVE_DIR=saves, **case.get("env", {}))
        sob = os.path.join(assets, "MAP", case["room"] + ".SOB")
        r = subprocess.run([exe, assets, sob], env=env, capture_output=True, text=True, timeout=300)
    out = r.stdout + r.stderr
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
