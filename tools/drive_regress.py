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
}

# The Archipelago cases, played by the Lua mod samples/mods/archipelago_lua against a local scripted AP
# server (tools/aplua_regress.py, LUA_API.md §9).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aplua_regress  # noqa: E402
CASES.update(aplua_regress.CASES)


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
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR="1", CLERIA_DRIVE=case["drive"],
                   CLERIA_SAVE_DIR=saves, **case.get("env", {}))
        env.setdefault("CLERIA_SHOT_DIR", saves)   # a case's `shot` / `shell` frames stay in its temp folder
        if "mods" in case or "content" in case:
            content_setup(case, env, os.path.join(saves, "settings"))
        srv = aplua_regress.start(case, env, os.path.join(saves, "settings"))   # an `aplua` case: its server, the mod
        sob = os.path.join(assets, "MAP", case["room"] + ".SOB")
        r = subprocess.run([exe, assets, sob], env=env, capture_output=True, text=True, timeout=300)
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
