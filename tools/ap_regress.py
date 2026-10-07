"""Archipelago cases for tools/drive_regress.py (build/shell_re/AP_GAME.md §8).

Each case starts tools/ap_mock_server.py on 127.0.0.1 (a free port; no internet), points a scratch
cleria.ini [cleria.archipelago] at it with AutoConnect=1, runs the CLERIA_DRIVE list, then checks the
game's output (`expect`, in order; `absent`) and the server's log of what the game sent
(`server_expect`, in order). Every run is deterministic: the server's room, placements and slot_data
are fixed (res/ap/test_slot_data.json), and its replies only follow the game's own packets and the
drive's `apsay=` commands.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHEST = "10:kill1;420:obj=box_01;430:act"       # the S_1001 chest behind the barrier (content_chest's route)

CASES = {
    # A check: the S_1001 chest holds another world's item. The vanilla Panacea is swallowed, the box flag
    # 302 sends "Wailing Blue: 2F Path 1", the rising icon and the treasure box show the AP item.
    "ap_chest": dict(
        room="S_10/S_1001/S_1001", ap={},
        drive=CHEST + ";740:ok;760:apflag=0x59;770:apflag=302;780:apstate;790:quit",
        expect=["[ap] connected as Yunica (slot 2) seed CLERIATEST, 305 locations", "save stamped: seed CLERIATEST slot 2",
                "[content] icon 0x59 -> 0x2A0", "[ap] suppressed g_flags[0x59] -1 -> 1 (S_1001/S_BOX01)",
                "[ap] check 5857605 Wailing Blue: 2F Path 1 (store)", "[content] chest S_1001/S_BOX01 opened (flag 302)",
                "[ap] box S_1001/S_BOX01: Sent\\nButton Activation to Clicker.",
                "[ap] print ItemSend: Yunica sent Button Activation to Clicker (Wailing Blue: 2F Path 1)",
                "[ap] flag 0x59 = -1", "[ap] flag 0x12E = 1", "checks 3 goal 0 active 1"],
        server_expect=['Connect {"cmd":"Connect","game":"Ys Origin","items_handling":7,"name":"Yunica","password":""',
                       "LocationScouts", '{"cmd":"LocationChecks","locations":[5857605]}']),
    # Receiving: two Panaceas from the other world, a save between them, a load of that save: the second is
    # granted again into the older save, the first never twice (the received index lives in the sidecar).
    "ap_items_save": dict(
        room="S_10/S_1001/S_1001", ap={},
        drive="60:apsay=!give Celcetan Panacea;100:apflag=0x59;120:csave=2;130:apsay=!give Celcetan Panacea;"
              "170:apflag=0x59;200:cload=2;260:apflag=0x59;265:apstate;270:quit",
        expect=["Celcetan Panacea: g_flags[0x59] -1 -> 1 (from Clicker)", "[ap] flag 0x59 = 1", "sidecar yso_02.cleria written",
                "Celcetan Panacea: g_flags[0x59] 1 -> 2 (from Clicker)", "[ap] flag 0x59 = 2", "event load",
                "[ap] loaded save: seed CLERIATEST applied 3", "Celcetan Panacea: g_flags[0x59] 1 -> 2 (from Clicker)",
                "[ap] flag 0x59 = 2", "applied 4/4"],
        server_expect=['Say {"cmd":"Say","text":"!give Celcetan Panacea"}']),
    # DeathLink both ways: Clicker's death kills Yunica (and is not sent back); after the Retry her own
    # death is sent as a Bounce.
    "ap_deathlink": dict(
        room="S_10/S_1009/S_1009", ap=dict(death_link=True),
        drive="200:apsay=!death;360:ok;700:hp=0;760:apstate;800:quit",
        expect=["[ap] deathlink from Clicker: Clicker pressed the button too hard.", "[death] HP 0",
                "[gameover] chose retry", "[death] HP 0", "[ap] deathlink sent", "deathlink 1"],
        absent=["deathlink from Yunica"],
        server_expect=['"tags":["DeathLink"]', '"cause":"Yunica ran out of HP"']),
    # A statue unlock, two traps (the fog, a Chaos Warp to the one registered statue: S_1000) and the goal.
    "ap_goal_traps": dict(
        room="S_10/S_1009/S_1009", ap={},
        drive="60:apsay=!give 1F Warp;100:apsay=!give Blinding Fog;200:apsay=!give Chaos Warp;320:flag=226,1;"
              "340:apstate;400:quit",
        expect=["1F Warp: statue S_1000 unlocked (warp 0)", "trap Blinding Fog", "chaos warp to S_1000", ">> entering S_1000",
                "[ap] check 5857392 Explore: 1F Save (S_1000) (room)", "goal reached: StatusUpdate(Goal)", "goal 1"],
        absent=["Statue: 1F Save (S_1000) ("],
        server_expect=['{"cmd":"StatusUpdate","status":30}']),
    # The save's seed guard: the room changes seed; reconnecting refuses it before any Connect, and the
    # save keeps the rules of its own seed (the cached slot_data).
    "ap_seed_guard": dict(
        room="S_10/S_1009/S_1009", ap={},
        drive="100:apsay=!reseed OTHERSEED;130:apdisconnect;150:apconnect;250:apstate;260:quit",
        expect=["save stamped: seed CLERIATEST", "disconnected", "offline: rules of seed CLERIATEST",
                "seed mismatch: the room is seed OTHERSEED; this save belongs to seed CLERIATEST",
                "auth failed: this room runs a different seed than the save' seed CLERIATEST slot 2 applied 2/0 checks 2 goal 0 active 1"]),
    # No slot set: the chest is retail (the Panacea), and nothing of AP runs.
    "ap_off": dict(
        room="S_10/S_1001/S_1001",
        drive=CHEST + ";740:ok;760:apflag=0x59;770:quit",
        expect=["[probe] box_01 -> s_box01", "chest S_1001/S_BOX01 opened (flag 302)", "[ap] flag 0x59 = 1"],
        absent=["[ap] connecting", "[ap] new game", "suppressed", "icon 0x59"]),
}


def start(case, env, settings_dir):
    """Start the server for an `ap` case and write the scratch settings; returns the server process."""
    if "ap" not in case:
        return None
    a = case["ap"]
    args = [sys.executable, os.path.join(ROOT, "tools", "ap_mock_server.py"), "--port", "0"]
    if a.get("death_link"):
        args.append("--death-link")
    srv = subprocess.Popen(args, stdout=subprocess.PIPE, text=True)
    port = srv.stdout.readline().strip().rsplit(":", 1)[1]
    os.makedirs(settings_dir, exist_ok=True)
    with open(os.path.join(settings_dir, "cleria.ini"), "w") as f:
        f.write('[cleria.archipelago]\nServer="127.0.0.1:%s"\nSlot="Yunica"\nAutoConnect=1\nDeathLink=%d\n'
                % (port, int(bool(a.get("death_link")))))
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
