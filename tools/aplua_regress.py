"""Archipelago cases for tools/drive_regress.py (build/shell_re/LUA_API.md §9): the Lua mod
samples/mods/archipelago_lua against a local scripted AP server.

Each case starts tools/ap_mock_server.py on 127.0.0.1 (a free port; no internet), installs and turns on the
mod in a scratch mods folder, points its settings ([cleria.mod.ysorigin.archipelago] in cleria.ini) at the
server with AutoConnect=1, drives the game with the mod's commands (`lua=apsay ...`, `lua=apstate` ...), then
checks the game's output (`expect`, in order; `absent`) and the server's log of what the game sent
(`server_expect`, in order). Every run is deterministic: the server's room, placements and slot_data are fixed
(tests/ap/test_slot_data.json), and its replies only follow the game's own packets and the drive's commands.
These six cases once ran line for line beside the built-in C++ client's ap_* cases (removed with that client).
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = "ysorigin.archipelago"
P = "[lua:%s] " % MOD
CHEST = "10:kill1;420:obj=box_01;430:act"       # the S_1001 chest behind the barrier (ap_chest's route)

CASES = {
    # A check: the S_1001 chest holds another world's item (ap_chest).
    "aplua_chest": dict(
        room="S_10/S_1001/S_1001", aplua={},
        drive=CHEST + ";740:ok;760:lua=apflag 0x59;770:lua=apflag 302;780:lua=apstate;790:quit",
        expect=["[lua] Archipelago: started (main.lua)", P + "connected as Yunica (slot 2) seed CLERIATEST, 305 locations",
                P + "save stamped: seed CLERIATEST slot 2", "[content] icon 0x59 -> 0x2A1",
                P + "suppressed g_flags[0x59] -1 -> 1 (S_1001/S_BOX01)", P + "check 5857605 Wailing Blue: 2F Path 1 (store)",
                "[content] chest S_1001/S_BOX01 opened (flag 302)", P + "box S_1001/S_BOX01: Sent\\nButton Activation to Clicker.",
                P + "print ItemSend: Yunica sent Button Activation to Clicker (Wailing Blue: 2F Path 1)",
                P + "flag 0x59 = -1", P + "flag 0x12E = 1", "checks 3 goal 0 active 1"],
        server_expect=['Connect {"cmd":"Connect","game":"Ys Origin","items_handling":7,"name":"Yunica","password":""',
                       "LocationScouts", '{"cmd":"LocationChecks","locations":[5857605]}']),
    # Receiving, a save between two grants, a load of that save: no double grant (ap_items_save).
    "aplua_items_save": dict(
        room="S_10/S_1001/S_1001", aplua={},
        drive="60:lua=apsay !give Celcetan Panacea;100:lua=apflag 0x59;120:csave=2;130:lua=apsay !give Celcetan Panacea;"
              "170:lua=apflag 0x59;200:cload=2;260:lua=apflag 0x59;265:lua=apstate;270:quit",
        expect=["Celcetan Panacea: g_flags[0x59] -1 -> 1 (from Clicker)", P + "flag 0x59 = 1", "sidecar yso_02.cleria written",
                "Celcetan Panacea: g_flags[0x59] 1 -> 2 (from Clicker)", P + "flag 0x59 = 2", "event load",
                P + "loaded save: seed CLERIATEST applied 3", "Celcetan Panacea: g_flags[0x59] 1 -> 2 (from Clicker)",
                P + "flag 0x59 = 2", "applied 4/4"],
        server_expect=['Say {"cmd":"Say","text":"!give Celcetan Panacea"}']),
    # DeathLink both ways (ap_deathlink).
    "aplua_deathlink": dict(
        room="S_10/S_1009/S_1009", aplua=dict(death_link=True),
        drive="200:lua=apsay !death;360:ok;700:hp=0;760:lua=apstate;800:quit",
        expect=[P + "deathlink from Clicker: Clicker pressed the button too hard.", "[death] HP 0",
                "[gameover] chose retry", "[death] HP 0", P + "deathlink sent", "deathlink 1"],
        absent=["deathlink from Yunica"],
        server_expect=['"tags":["DeathLink"]', '"cause":"Yunica ran out of HP"']),
    # A statue unlock, two traps and the goal (ap_goal_traps).
    "aplua_goal_traps": dict(
        room="S_10/S_1009/S_1009", aplua={},
        drive="60:lua=apsay !give 1F Warp;100:lua=apsay !give Blinding Fog;200:lua=apsay !give Chaos Warp;320:flag=226,1;"
              "340:lua=apstate;400:quit",
        expect=["1F Warp: statue S_1000 unlocked (warp 0)", "trap Blinding Fog", P + "chaos warp to S_1000", ">> entering S_1000",
                P + "check 5857392 Explore: 1F Save (S_1000) (room)", P + "goal reached: StatusUpdate(Goal)", "goal 1"],
        absent=["Statue: 1F Save (S_1000) ("],
        server_expect=['{"cmd":"StatusUpdate","status":30}']),
    # The save's seed guard (ap_seed_guard).
    "aplua_seed_guard": dict(
        room="S_10/S_1009/S_1009", aplua={},
        drive="100:lua=apsay !reseed OTHERSEED;130:lua=apdisconnect;150:lua=apconnect;250:lua=apstate;260:quit",
        expect=[P + "save stamped: seed CLERIATEST", P + "disconnected", P + "offline: rules of seed CLERIATEST",
                P + "seed mismatch: the room is seed OTHERSEED; this save belongs to seed CLERIATEST",
                "auth failed: this room runs a different seed than the save' seed CLERIATEST slot 2 applied 2/0 checks 2 goal 0 active 1"]),
    # The mod on, no slot set: the chest is retail and the mod opens no socket (ap_off).
    "aplua_off": dict(
        room="S_10/S_1001/S_1001", aplua=dict(slot=""),
        drive=CHEST + ";740:ok;760:lua=apflag 0x59;770:quit",
        expect=["[lua] Archipelago: started (main.lua)", "[probe] box_01 -> s_box01", "chest S_1001/S_BOX01 opened (flag 302)",
                P + "flag 0x59 = 1"],
        absent=[P + "connecting", P + "new game", "suppressed", "icon 0x59"]),
}


def start(case, env, settings_dir):
    """An `aplua` case: the server, the mod installed and on, its settings; returns the server process."""
    if "aplua" not in case:
        return None
    a = case["aplua"]
    srv = None
    port = "1"
    slot = a.get("slot", "Yunica")
    if slot:
        args = [sys.executable, os.path.join(ROOT, "tools", "ap_mock_server.py"), "--port", "0"]
        if a.get("death_link"):
            args.append("--death-link")
        srv = subprocess.Popen(args, stdout=subprocess.PIPE, text=True)
        port = srv.stdout.readline().strip().rsplit(":", 1)[1]
    mods = os.path.join(settings_dir, "mods")
    os.makedirs(mods, exist_ok=True)
    if os.environ.get("APLUA_MOD_ZIP"):           # test a release zip as players install it
        shutil.copy(os.environ["APLUA_MOD_ZIP"], mods)
    else:
        shutil.copytree(os.path.join(ROOT, "samples", "mods", "archipelago_lua"), os.path.join(mods, "archipelago_lua"))
    with open(os.path.join(settings_dir, "cleria.ini"), "w") as f:
        f.write('[cleria.mods]\nUseMods=1\nOrder="%s"\nActive="%s"\n\n' % (MOD, MOD))
        f.write('[cleria.mod.%s]\nServer="127.0.0.1:%s"\nSlot="%s"\nAutoConnect=1\nDeathLink=%d\n'
                % (MOD, port, slot, int(bool(a.get("death_link")))))
    open(os.path.join(settings_dir, "yso_win.ini"), "w").close()
    env["CLERIA_INI"] = os.path.join(settings_dir, "yso_win.ini")
    return srv


def finish(name, case, srv):
    """Stop the server and check its log; True when every server_expect is there in order."""
    if srv is None:
        return True
    srv.kill()
    log = srv.stdout.read()
    at = 0
    for e in case.get("server_expect", []):
        k = log.find(e, at)
        if k < 0:
            print(f"FAIL {name}: the server never got '{e}' (after offset {at})")
            return False
        at = k + len(e)
    return True
