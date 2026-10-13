"""collect.py [groups regex] [flags]: for every room of the groups, as Hugo: the SEN exits, the scripted objects, the event
boxes and the collision mesh at tick 60 (drive dumpmesh=) -> rooms/<room>.mesh, rooms/<room>.log, rooms.json.
flags: "f=v,f=v" set at tick 2 followed by saveload? no - plain flag sets before the room scripts' second tick."""
import concurrent.futures as cf, json, os, re, subprocess, sys, tempfile
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
A = os.environ.get("CLERIA_ASSETS", os.path.join(WT, "assets")).replace(os.sep, "/")
EXE = os.path.join(WT, "build_diff", "Release", "cleria-view.exe")
groups = sys.argv[1] if len(sys.argv) > 1 else "S_40|S_41|S_50|S_51|S_60|S_61|S_70"
OUT = os.path.join(HERE, "rooms"); os.makedirs(OUT, exist_ok=True)
rooms = {}
for g in sorted(os.listdir(os.path.join(A, "MAP"))):
    if not re.match("(" + groups + ")$", g): continue
    for r in sorted(os.listdir(os.path.join(A, "MAP", g))):
        if os.path.exists(os.path.join(A, "MAP", g, r, r + ".SOB")): rooms[r] = g + "/" + r + "/" + r
def one(r):
    mesh = os.path.join(OUT, r + ".mesh").replace("\\", "/")
    d = f"1:opt=InfiniteHP=1;1:opt=CutsceneSkip=4;4:pos;60:dumpmesh={mesh}0;8:every=40,640,killall;20:every=10,640,oktapf;650:exits;650:objs;650:boxes;650:dumpmesh={mesh};1050:dumpmesh={mesh}2;1052:quit"
    with tempfile.TemporaryDirectory() as s:
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR="2", CLERIA_DRIVE=d, CLERIA_SAVE_DIR=s, CLERIA_SHOT_DIR=s, CLERIA_DRIVE_LOCKSTEP="1")
        try: out = subprocess.run([EXE, A, os.path.join(A, "MAP", rooms[r] + ".SOB")], env=env, capture_output=True, text=True, errors="replace", timeout=400).stdout
        except subprocess.TimeoutExpired: out = "TIMEOUT"
    open(os.path.join(OUT, r + ".log"), "w", encoding="utf-8", errors="replace").write(out)
    ex = [dict(i=int(m.group(1)), name=m.group(2), dest=m.group(3), pos=[float(x) for x in m.group(4).split(",")], arr=[float(x) for x in m.group(5).split(",")])
          for m in re.finditer(r"\[exit\] (\d+) (\S*) -> (\S+) at \(([^)]*)\) arrival \(([^)]*)\)", out)]
    ob = [dict(name=m.group(1), kind=m.group(2), script=m.group(3), pos=[float(x) for x in m.group(4).split(",")])
          for m in re.finditer(r"\[obj\] (\S+) (trigger \d+|action) script (\S*) .*at \(([^)]*)\)", out)]
    bx = [dict(name=m.group(1), c=[float(x) for x in m.group(2).split(",")]) for m in re.finditer(r"\[box\] (\S+) centre \(([^)]*)\)", out)]
    sp = re.search(r"feet \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)", out)
    return r, dict(path=rooms[r], exits=ex, objs=ob, boxes=bx, spawn=[float(x) for x in sp.groups()] if sp else None, ok=os.path.exists(mesh))
with cf.ThreadPoolExecutor(4) as pool: R = dict(pool.map(one, rooms))
json.dump(R, open(os.path.join(HERE, "rooms.json"), "w"), indent=1)
print(len(R), "rooms;", sum(len(v["exits"]) for v in R.values()), "exits;", [r for r, v in R.items() if not v["ok"]])
