"""Pack a CleriaCore mod folder into a .cleriamod file (build/shell_re/MODFILE.md).

    python tools/pack_mod.py <mod folder> [-o <output folder>]

writes <output folder>/<id>-<version>.cleriamod (default: the current folder). A .cleriamod is a plain zip
holding ONE top-level folder named by the mod's id:

    ysorigin.archipelago/mod.ini
    ysorigin.archipelago/main.lua
    ysorigin.archipelago/ap/client.lua ...

The manifest is checked the way CleriaCore reads it (src/cleria/mods.cpp parse_manifest): a [mod] Id of
a-z 0-9 . _ - (lowercased, at most 64), a well-formed Requires / Conflicts list, an ApiVersion this
CleriaCore supports, the Script file present. The zip is deterministic: entries sorted, '/' separators,
a fixed 1980-01-01 time stamp, no extra attributes, so the same folder always gives the same bytes.
Left out: names starting with '.', __pycache__, *.pyc, Thumbs.db, desktop.ini, and any .cleriamod / .zip
inside the folder.
"""
import argparse
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ID_RE = re.compile(r"^[a-z0-9._-]{1,64}$")
DEP_RE = re.compile(r"^\s*([A-Za-z0-9._-]+)\s*(?:([<>=]+)\s*([^\s<>=]\S*))?\s*$")   # the op: every <>= char
DEP_OPS = {">=", ">", "=", "==", "<=", "<"}
SKIP_NAMES = {"__pycache__", "thumbs.db", "desktop.ini"}
SKIP_EXT = (".pyc", ".cleriamod", ".zip")
STAMP = (1980, 1, 1, 0, 0, 0)


def api_version():
    """mods::kApiVersion from src/cleria/mods.hpp (3 when the header is not beside this tool)."""
    try:
        with open(os.path.join(ROOT, "src", "cleria", "mods.hpp"), encoding="utf-8") as f:
            m = re.search(r"kApiVersion\s*=\s*(\d+)", f.read())
        return int(m.group(1)) if m else 3
    except OSError:
        return 3


def read_manifest(path):
    """[mod] of mod.ini as {lowercased key: value}; quotes around a value removed, ';' lines skipped."""
    with open(path, "rb") as f:
        text = f.read().decode("utf-8-sig", errors="replace")
    out, section = {}, None
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("[") and s.endswith("]"):
            section = s[1:-1].strip().lower()
            continue
        if section != "mod" or "=" not in s:
            continue
        k, v = s.split("=", 1)
        v = v.strip()
        if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
            v = v[1:-1]
        out.setdefault(k.strip().lower(), v)
    return out


def check_manifest(folder):
    """The manifest's id and version; raises SystemExit with every problem found."""
    ini = os.path.join(folder, "mod.ini")
    if not os.path.isfile(ini):
        sys.exit(f"pack_mod: no mod.ini in {folder}")
    m = read_manifest(ini)
    errors = []
    raw_id = m.get("id", "")
    mod_id = raw_id.lower()
    if not raw_id:
        errors.append("mod.ini has no [mod] Id")
    elif not ID_RE.match(mod_id):
        errors.append(f'invalid Id "{raw_id}" (use a-z, 0-9, ".", "_", "-"; at most 64)')
    version = m.get("version", "") or "0"
    if re.search(r'[\\/:*?"<>|\s]', version):
        errors.append(f'Version "{version}" cannot be part of a file name')
    api = m.get("apiversion", "")
    if api:
        try:
            if int(api) > api_version():
                errors.append(f"ApiVersion {api} is newer than this CleriaCore's ({api_version()})")
        except ValueError:
            errors.append(f'ApiVersion "{api}" is not a number')
    for key in ("requires", "conflicts"):
        for entry in filter(None, (e.strip() for e in m.get(key, "").split(","))):
            d = DEP_RE.match(entry)
            if not d or not ID_RE.match(d.group(1).lower()) or (d.group(2) and d.group(2) not in DEP_OPS):
                errors.append(f'bad {key.capitalize()} entry "{entry}"')
    script = m.get("script", "")
    if script and not os.path.isfile(os.path.join(folder, script)):
        errors.append(f"Script={script} is not in the folder")
    if errors:
        sys.exit("pack_mod: " + "\n          ".join(errors))
    return mod_id, version


def files_of(folder):
    """Every file to pack, as (path on disk, name inside the mod folder with '/'), sorted by name."""
    out = []
    for base, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if not d.startswith(".") and d.lower() not in SKIP_NAMES]
        for f in files:
            if f.startswith(".") or f.lower() in SKIP_NAMES or f.lower().endswith(SKIP_EXT):
                continue
            p = os.path.join(base, f)
            out.append((p, os.path.relpath(p, folder).replace(os.sep, "/")))
    out.sort(key=lambda t: t[1])
    return out


def pack(folder, out_dir):
    folder = os.path.abspath(folder)
    mod_id, version = check_manifest(folder)
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, f"{mod_id}-{version}.cleriamod")
    part = dest + ".part"
    entries = files_of(folder)
    with zipfile.ZipFile(part, "w") as z:
        for path, name in entries:
            info = zipfile.ZipInfo(f"{mod_id}/{name}", date_time=STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 0
            info.external_attr = 0
            with open(path, "rb") as f:
                z.writestr(info, f.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(part) as z:   # read it back: every entry unpacks
        bad = z.testzip()
        if bad:
            os.remove(part)
            sys.exit(f"pack_mod: {bad} did not read back")
    os.replace(part, dest)
    return dest, len(entries)


def main():
    ap = argparse.ArgumentParser(description="Pack a CleriaCore mod folder into <id>-<version>.cleriamod")
    ap.add_argument("folder", help="the mod folder (the one holding mod.ini)")
    ap.add_argument("-o", "--out", default=".", help="where to write the .cleriamod (default: the current folder)")
    a = ap.parse_args()
    dest, n = pack(a.folder, a.out)
    print(f"{dest}  ({n} files, {os.path.getsize(dest)} bytes)")


if __name__ == "__main__":
    main()
