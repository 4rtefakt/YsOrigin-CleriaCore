#!/usr/bin/env python3
"""Run a mod's own drive cases through cleria-view's headless harness (Mod API 7, LUA_API.md §12.23).

    python tools/mod_test.py <mod folder> [case ...] [--exe <cleria-view>] [--assets <game assets>] [--show <text>]

The mod folder holds `mod.ini` and a cases file: `tests/cases.py` or `.dev/cases.py`, a Python file that defines

    CASES = {
        "name": dict(room="S_10/S_1002/S_1002",            # the room the game boots in (under <assets>/MAP)
                     drive="30:lua=mycmd arg;90:quit",     # tick:command;... (CLERIA_DRIVE; see DRIVE.md)
                     expect=["[lua:my.mod] done"],         # lines of the output, in this order
                     absent=["error"],                     # lines that must not appear
                     settings="Key=1",                     # lines for the mod's section of cleria.ini
                     char="2", env={...},                  # CLERIA_CHAR and more environment
                     drive2="...",                         # a second boot on the same saves and settings
                     shots={"title": 60},                  # a `shot` command's tick -> <cases folder>/shots/<name>.png
                     check=lambda out: None or "why"),     # the case's own check of the whole output
    }

Each case runs in its own scratch settings and save folders with only that mod installed and turned on, muted.
A mod that stops with an error fails the case. Defaults: the exe is build/Release/cleria-view(.exe) of this
checkout, the assets its assets/ folder (or CLERIA_EXE / CLERIA_ASSETS). Exit code 1 when a case fails."""
import importlib.util, os, shutil, struct, subprocess, sys, tempfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STOPPED = ("script stopped", "Mod stopped", "the mod is disabled until the game restarts")


def mod_id(mod):
    for line in open(os.path.join(mod, "mod.ini"), encoding="utf-8"):
        if line.strip().lower().startswith("id="):
            return line.split("=", 1)[1].strip().strip('"').lower()
    raise SystemExit("mod.ini has no Id=")


def ppm_to_png(src, dst):
    with open(src, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        rgb = f.read()
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(dst, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def run(name, case, mod, exe, assets, shots_dir, show):
    mid = mod_id(mod)
    with tempfile.TemporaryDirectory() as tmp:
        settings = os.path.join(tmp, "settings")
        shutil.copytree(mod, os.path.join(settings, "mods", mid),
                        ignore=shutil.ignore_patterns(".*", "*.cleriamod", "*.zip", "__pycache__", "tests"))
        with open(os.path.join(settings, "cleria.ini"), "w") as f:
            f.write('[cleria.mods]\nUseMods=1\nOrder="%s"\nActive="%s"\n\n[cleria.mod.%s]\n%s\n' % (mid, mid, mid, case.get("settings", "")))
        open(os.path.join(settings, "yso_win.ini"), "w").close()
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR=case.get("char", "1"), CLERIA_DRIVE=case["drive"],
                   CLERIA_SAVE_DIR=tmp, CLERIA_SHOT_DIR=tmp, CLERIA_INI=os.path.join(settings, "yso_win.ini"))
        env.update(case.get("env", {}))
        sob = os.path.join(assets, "MAP", case["room"] + ".SOB")
        r = subprocess.run([exe, assets, sob], env=env, capture_output=True, text=True, errors="replace", timeout=900)
        out = r.stdout + r.stderr
        if "drive2" in case:
            r = subprocess.run([exe, assets, sob], env=dict(env, CLERIA_DRIVE=case["drive2"]), capture_output=True,
                               text=True, errors="replace", timeout=900)
            out += "\n==== second boot\n" + r.stdout + r.stderr
        for shot, tick in case.get("shots", {}).items():
            p = os.path.join(tmp, "t%d.ppm" % tick)
            if os.path.exists(p):
                os.makedirs(shots_dir, exist_ok=True)
                ppm_to_png(p, os.path.join(shots_dir, shot + ".png"))
    lua = "\n".join(l for l in out.splitlines() if "[lua" in l or (show and show in l))
    if show:
        print(lua)
    for e in list(case.get("absent", [])) + list(STOPPED):
        if e in out:
            print(f"FAIL {name}: unexpected '{e}'\n{lua[-1500:]}")
            return False
    at = 0
    for e in case.get("expect", []):
        k = out.find(e, at)
        if k < 0:
            print(f"FAIL {name}: missing '{e}'\n{lua[-2500:]}")
            return False
        at = k + len(e)
    err = case["check"](out) if case.get("check") else None
    if err:
        print(f"FAIL {name}: {err}")
        return False
    print(f"ok   {name}")
    return True


def main(argv):
    opt = {"--exe": os.environ.get("CLERIA_EXE") or os.path.join(ROOT, "build", "Release", "cleria-view" + (".exe" if os.name == "nt" else "")),
           "--assets": os.environ.get("CLERIA_ASSETS") or os.path.join(ROOT, "assets"), "--show": ""}
    rest = []
    it = iter(argv)
    for a in it:
        if a in opt:
            opt[a] = next(it)
        else:
            rest.append(a)
    if not rest:
        raise SystemExit(__doc__)
    mod = os.path.abspath(rest[0])
    cases_file = next((p for p in (os.path.join(mod, "tests", "cases.py"), os.path.join(mod, ".dev", "cases.py")) if os.path.exists(p)), None)
    if not cases_file:
        raise SystemExit("no tests/cases.py or .dev/cases.py in " + mod)
    spec = importlib.util.spec_from_file_location("cases", cases_file)
    cases = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cases)
    names = rest[1:] or list(cases.CASES)
    unknown = [n for n in names if n not in cases.CASES]
    if unknown:
        raise SystemExit("no such case: " + ", ".join(unknown))
    shots = os.path.join(os.path.dirname(cases_file), "shots")
    exe, assets = os.path.abspath(opt["--exe"]), os.path.abspath(opt["--assets"])
    bad = sum(not run(n, cases.CASES[n], mod, exe, assets, shots, opt["--show"]) for n in names)
    print(f"{len(names) - bad}/{len(names)} passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
