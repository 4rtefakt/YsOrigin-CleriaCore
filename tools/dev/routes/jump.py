"""jump.py: per-tick (x, y) of a running jump on flat ground (S_1002's corridor) per character, with / without the dash,
single and double (second press at +dj) -> jump.json {"<ch><dash>": {"<dj>": [[dx, dy], ...from the press tick]}}.
The tail of each track is extended below the take-off height with the mover's own vertical rule
(physics_player.hpp: dy = max(0.5 * 9.81 * t * |t|, -0.65), t -= 0.0168, floor -0.5) and the last airborne dx."""
import concurrent.futures as cf, json, math, os, re, subprocess, tempfile
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
A = os.environ.get("CLERIA_ASSETS", os.path.join(WT, "assets")).replace(os.sep, "/")
def one(x):
    ch, dash, dj = x
    d = "5:killall;8:flag=163,1;" + ("9:flag=181,1;10:vopt=AlwaysDashOK=1;" if dash else "") + "30:at=14,-37,90;34:move=1,0,200;71:jump;"
    if dj: d += f"{71 + dj}:jump;"
    d += "70:every=1,200,pos;201:quit"
    with tempfile.TemporaryDirectory() as s:
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR=ch, CLERIA_DRIVE=d, CLERIA_SAVE_DIR=s, CLERIA_SHOT_DIR=s, CLERIA_DRIVE_LOCKSTEP="1")
        out = subprocess.run([os.path.join(WT, "build_diff", "Release", "cleria-view.exe"), A, os.path.join(A, "MAP", "S_10/S_1002/S_1002.SOB")], env=env, capture_output=True, text=True, errors="replace", timeout=300).stdout
    f = [(float(a), float(b)) for a, b in re.findall(r"feet \(([-\d.]+), ([-\d.]+),", out)]
    x0, y0 = f[0]
    tr = [(a - x0, b - y0) for a, b in f[1:]]
    # cut at the landing (back on the floor after having risen)
    up = False; end = len(tr)
    for i, (a, b) in enumerate(tr):
        if b > 0.3: up = True
        if up and abs(b) < 0.01: end = i; break
    run_dx = f[1][0] - f[0][0]
    air = tr[:end]
    # extend the fall
    dxa = air[-1][0] - air[-2][0]; dy = air[-1][1] - air[-2][1]
    t = -math.sqrt(max(-dy, 0) / (0.5 * 9.81)) if dy > -0.65 else -0.5
    x, y = air[-1]
    while y > -40:
        t = max(t - 0.0168, -0.5); dy = max(0.5 * 9.81 * t * abs(t), -0.65)
        x += dxa; y += dy; air.append((x, y))
    return f"{ch}{dash}", str(dj), air, end, run_dx
jobs = [(ch, dash, dj) for ch in "123" for dash in (0, 1) for dj in [0] + list(range(10, 37, 2))]
res = {}
with cf.ThreadPoolExecutor(4) as ex:
    for k, dj, air, end, run_dx in ex.map(one, jobs):
        res.setdefault(k, {})[dj] = [[round(a, 4), round(b, 4)] for a, b in air]
        print(k, dj, "airborne", end, "reach", round(air[end - 1][0], 2), "apex", round(max(b for a, b in air), 2), "run dx", round(run_dx, 4), flush=True)
json.dump(res, open(os.path.join(HERE, "jump.json"), "w"))
