"""Mod-file cases for tools/drive_regress.py (build/shell_re/MODFILE.md): `cleria-view.exe <assets> <room>
<file>.cleriamod` installs the file into a scratch mods folder before the mods load, so the mod runs in that
same session. `modfile`: a samples/mods folder packed with tools/pack_mod.py, or "broken" (not a zip: refused,
nothing installed). The settings are a scratch CLERIA_INI; the file association is never touched in a drive run.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CASES = {
    # A double-clicked .cleriamod: installed, turned on, loaded in this session.
    "modfile_install": dict(
        room="S_10/S_1009/S_1009", modfile="hello", drive="30:quit",
        expect=["[modfile] Installed Hello Lua 1.0.0", "[modfile] file association: skipped (drive run)",
                "active: Hello Lua 1.0.0", "[lua] Hello Lua: started"],
        absent=["message box"]),
    # An invalid file: refused before anything is written; the game starts unmodded.
    "modfile_bad": dict(
        room="S_10/S_1009/S_1009", modfile="broken", drive="30:quit",
        expect=["[modfile] Could not install broken.cleriamod: not a valid zip file", "0 installed, active: none"],
        absent=["[lua] Hello Lua"]),
}


def start(case, env, settings_dir):
    """A `modfile` case: its scratch settings and the packed file; returns the extra command-line arguments."""
    if "modfile" not in case:
        return []
    os.makedirs(settings_dir, exist_ok=True)
    open(os.path.join(settings_dir, "yso_win.ini"), "w").close()
    env["CLERIA_INI"] = os.path.join(settings_dir, "yso_win.ini")
    out = os.path.join(settings_dir, "downloads")
    os.makedirs(out, exist_ok=True)
    if case["modfile"] == "broken":
        path = os.path.join(out, "broken.cleriamod")
        open(path, "wb").write(b"not a zip at all")
        return [path]
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "pack_mod.py"),
                        os.path.join(ROOT, "samples", "mods", case["modfile"]), "-o", out],
                       capture_output=True, text=True, check=True)
    return [r.stdout.split("  (")[0].strip()]
