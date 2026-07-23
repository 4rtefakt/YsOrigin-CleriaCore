#!/usr/bin/env python3
"""Golden-screenshot regression net for cleria-view.

Renders a fixed set of headless screens and compares their pixel hashes against
tests/goldens.json. Exact-match on the same machine/GPU; the point is to prove a
refactor changed NOTHING, not to fuzzy-match across machines.

    python tools/goldens.py capture   # (re)record hashes -> tests/goldens.json
    python tools/goldens.py compare   # re-render, diff; exit 1 on any mismatch

On mismatch, compare reports the per-pixel diff count (never a mean — small
features matter) and leaves both PPMs in build/goldens/ for eyeballing.
"""
import hashlib, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build_diff", "Release", "cleria-view.exe")
ASSETS = os.path.join(ROOT, "assets")
OUT = os.path.join(ROOT, "build", "goldens")
DB = os.path.join(ROOT, "tests", "goldens.json")

W, H = 1280, 860
ROOM = lambda r: os.path.join(ASSETS, "MAP", f"S_{r[2:4]}", r, r + ".SOB")

# name -> argv tail (after exe). Menus take a placeholder room arg.
SCREENS = {
    "title":       ["_", "--title"],
    "charselect":  ["_", "--charselect"],
    "bonus":       ["_", "--bonus"],
    "shot_S0000":  [ROOM("S_0000"), "--shot"],
    "shot_S1000":  [ROOM("S_1000"), "--shot"],
    "shot_S3004":  [ROOM("S_3004"), "--shot"],
    "hud_S1000":   [ROOM("S_1000"), "--fps"],
}


def render(name, tail):
    os.makedirs(OUT, exist_ok=True)
    ppm = os.path.join(OUT, name + ".ppm")
    argv = [EXE, ASSETS] + tail + [ppm]
    if tail[-1] in ("--shot", "--fps"):
        argv += [str(W), str(H)]
    r = subprocess.run(argv, capture_output=True, text=True, timeout=180)
    if r.returncode != 0 or not os.path.exists(ppm):
        sys.exit(f"render FAILED: {name}\n{' '.join(argv)}\n{r.stdout}{r.stderr}")
    return ppm


def pixels(ppm):
    data = open(ppm, "rb").read()
    # strip the PPM header (3 whitespace-separated fields after the magic)
    i, fields = 2, 0
    while fields < 3:
        while data[i] in b" \t\r\n":
            i += 1
        if data[i : i + 1] == b"#":
            while data[i] not in b"\r\n":
                i += 1
            continue
        while data[i] not in b" \t\r\n":
            i += 1
        fields += 1
    return data[i + 1 :]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "compare"
    if mode == "capture":
        db = {}
        for name, tail in SCREENS.items():
            ppm = render(name, tail)
            px = pixels(ppm)
            import shutil
            shutil.copyfile(ppm, os.path.join(OUT, name + ".golden.ppm"))
            db[name] = hashlib.sha256(px).hexdigest()
            print(f"  {name}: {db[name][:16]}")
        os.makedirs(os.path.dirname(DB), exist_ok=True)
        json.dump(db, open(DB, "w"), indent=2)
        print(f"recorded {len(db)} goldens -> {DB}")
    elif mode == "compare":
        db = json.load(open(DB))
        bad = 0
        for name, tail in SCREENS.items():
            px = pixels(render(name, tail))
            h = hashlib.sha256(px).hexdigest()
            if h == db.get(name):
                print(f"  OK   {name}")
            else:
                golden = os.path.join(OUT, name + ".golden.ppm")
                ndiff = "?"
                if os.path.exists(golden):
                    gp = pixels(golden)
                    ndiff = sum(a != b for a, b in zip(px, gp)) // 3
                print(f"  DIFF {name}  (~{ndiff} px differ)")
                bad += 1
        if bad:
            sys.exit(f"{bad}/{len(SCREENS)} goldens differ")
        print(f"all {len(SCREENS)} goldens match")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
