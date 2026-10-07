#!/usr/bin/env python3
"""Import the Ys Origin apworld's data into CleriaCore (build/shell_re/ARCHIPELAGO.md §5).

CleriaCore is a client for the EXISTING apworld (YsOrigin-Archipelago/ys_origin, game "Ys Origin");
nothing there may change. This tool reads that repo READ-ONLY and writes res/ap/ys_origin.json:
every AP location id -> its CleriaCore detection (g_flags index / item_arr slot / scene / blessing
bit / floor), its room and chest (box flag + script), its vanilla item(s); every AP item id -> what
receiving it does in CleriaCore terms (g_flags item index, SP amount, progressive ladder, statue
unlock, blessing bit, trap ...).

The ids are not copied by hand: the tool loads the apworld's own ys_origin/data_tables.py (pure
Python, no Archipelago import) and uses its location_name_to_id / item_name_to_id, so they are the
ids the generator and server use. Bytecode writing is off so the repo is left untouched.

    python tools/ap_import.py [--apworld C:/Users/kores/repos/YsOrigin-Archipelago] [--out res/ap/ys_origin.json]
    python tools/ap_import.py --check      # regenerate in memory and compare with the committed file

Retail addresses in locations.json are module-relative (VA = 0x400000 + offset):
    g_flags      int32[0x200] at +0x36B91C  -> CleriaCore XsoVm::g_flags   (src/cleria/xso.hpp)
    item_arr     u32[54]      at +0x36A654  -> CleriaCore XsoVm::item_arr  ("Raval" gear-upgrade cells;
                                               build/menu_re/BLESSING_SHOP.md)
    blessing bits             at +0x36BC80  =  g_flags[0xD9]
    floor cell                at +0x36BC58  =  g_flags[0xCF]
"""
import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import sys

sys.dont_write_bytecode = True   # never write __pycache__ into the read-only apworld repo

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_APWORLD = r"C:/Users/kores/repos/YsOrigin-Archipelago"
DEFAULT_OUT = os.path.join(ROOT, "res", "ap", "ys_origin.json")

G_FLAGS_REL = 0x36B91C
G_FLAGS_COUNT = 0x200
ITEM_ARR_REL = 0x36A654
ITEM_ARR_COUNT = 54
BLESS_ITEM_BASE = 0x200
GEM_ITEM_BASE = 0x300
CHARACTERS = ("yunica", "hugo", "toal")


def load_data_tables(apworld):
    path = os.path.join(apworld, "ys_origin", "data_tables.py")
    spec = importlib.util.spec_from_file_location("ys_origin_data_tables", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_json(apworld, rel):
    with open(os.path.join(apworld, "ys_origin", rel), encoding="utf-8") as f:
        return json.load(f)


def sha256(apworld, rel):
    with open(os.path.join(apworld, rel), "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def git_head(apworld):
    try:
        out = subprocess.run(["git", "-C", apworld, "rev-parse", "HEAD"], capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else ""
    except OSError:
        return ""


def scene_number(s):
    """'S_1014/S_BOX01' -> 1014, 'S_1014' -> 1014 (the mod's rule: the integer after 'S_')."""
    if not s or not s.startswith("S_"):
        return 0
    digits = ""
    for ch in s[2:]:
        if not ch.isdigit():
            break
        digits += ch
    return int(digits) if digits else 0


def flag_of_offset(off):
    idx, rem = divmod(off - G_FLAGS_REL, 4)
    return idx if rem == 0 and 0 <= idx < G_FLAGS_COUNT else None


def convert_detect(d):
    m = d["method"]
    if m == "flag":
        off = int(d["offset"], 16)
        idx = flag_of_offset(off)
        if idx is not None:
            return {"method": "flag", "flag": idx}
        slot, rem = divmod(off - ITEM_ARR_REL, 4)
        if rem == 0 and 0 <= slot < ITEM_ARR_COUNT:
            return {"method": "item_arr", "index": slot}
        return {"method": "unmapped", "offset": d["offset"]}
    if m == "scene":
        return {"method": "scene", "scene": scene_number(d["scene"])}
    if m == "bit":
        idx = flag_of_offset(int(d["offset"], 16))
        return {"method": "bit", "flag": idx, "bit": int(d["bit"])}
    if m == "floor":
        return {"method": "floor", "floor": int(d["floor"]), "flag": flag_of_offset(int(d["offset"], 16))}
    return {"method": "unmapped", "raw": d}


def build(apworld):
    dt = load_data_tables(apworld)
    locs = read_json(apworld, "data/locations.json")
    chests = {c["id"]: c for c in read_json(apworld, "data/chests.json")}
    char_items = read_json(apworld, "data/character_items.json")
    statue_scenes = dt.statue_location_scenes()

    locations = []
    for l in locs:
        name = l["name"]
        e = {
            "id": dt.location_name_to_id[name],
            "name": name,
            "key": l["id"],
            "type": l["type"],
            "zone": l["zone"],
            "floor": l["floor"],
            "room": l["room"],
            "scene": scene_number(l["id"]),
            "excluded": bool(dt.is_excluded(name)),
            "detect": convert_detect(l["detect"]),
            "vanilla": [{"item": int(it["id"], 16), "name": it["name"], "class": it["class"]} for it in l["items"]],
        }
        ch = l.get("char")
        if ch:
            e["chars"] = [ch] if isinstance(ch, str) else list(ch)
        if name in statue_scenes:   # statue_warp_locks switches these to scene detection
            e["statue_scene"] = scene_number(statue_scenes[name])
        c = chests.get(l["id"])
        if c:
            # box_flag null: no usable box flag (S_1014/S_BOX01, detected by scene instead)
            bf = c.get("box_flag")
            e["chest"] = {"box_flag": int(bf, 16) if bf else None, "script": c["script"]}
        locations.append(e)
    locations.sort(key=lambda e: e["id"])

    statue_unlocks = dt.statue_unlock_slot_data()
    prog_skills = dt.progressive_skill_slot_data()
    skill_grants = dt.skill_grants()
    gear = {c: dt.progressive_gear_slot_data(c) for c in CHARACTERS}
    items = []
    for name, iid in sorted(dt.item_name_to_id.items(), key=lambda kv: kv[1]):
        e = {"id": iid, "name": name, "class": dt.item_classification(name)}
        idx = dt.item_index.get(name)
        if name in dt.SP_FILLER:
            e.update(kind="sp", amount=dt.SP_FILLER[name])
        elif name in dt.TRAP_POOL:
            e.update(kind="trap")
        elif name in statue_unlocks:
            su = statue_unlocks[name]
            e.update(kind="statue_unlock", scene=int(su["scene"]), flag=flag_of_offset(int(su["flag"], 16)))
        elif name in (dt.PROGRESSIVE_ARMOR, dt.PROGRESSIVE_BOOTS):
            e.update(kind="progressive_gear", ladder={c: gear[c][name] for c in CHARACTERS})
        elif name in prog_skills:
            e.update(kind="progressive_skill", **prog_skills[name])
        elif name in dt.PROGRESSIVE_BLESSINGS:
            e.update(kind="progressive_blessing", bits=list(dt.PROGRESSIVE_BLESSINGS[name]))
        elif name == dt.CLERIA_ORE:
            e.update(kind="cleria_ore", flag=idx)
        elif idx is not None and idx >= GEM_ITEM_BASE:
            e.update(kind="gem", index=idx, level_cell=skill_grants.get(name))
        elif idx is not None and idx >= BLESS_ITEM_BASE:
            e.update(kind="blessing", bit=idx - BLESS_ITEM_BASE)
        elif idx is not None:
            e.update(kind="item", flag=idx)
            if name in skill_grants:
                e["companion_flag"] = skill_grants[name]
        else:
            e.update(kind="unknown")
        owners = char_items.get(name)
        if owners and owners != ["shared"]:
            e["chars"] = owners
        if name == dt.GOAL_ITEM:
            e["goal_item"] = True
        items.append(e)

    return {
        "format": 1,
        "game": "Ys Origin",
        "source": {
            "repo": "YsOrigin-Archipelago",
            "commit": git_head(apworld),
            "sha256": {rel: sha256(apworld, rel) for rel in (
                "ys_origin/data_tables.py", "ys_origin/data/locations.json",
                "ys_origin/data/chests.json", "ys_origin/data/items.json",
                "ys_origin/data/character_items.json")},
            "tool": "tools/ap_import.py",
        },
        "location_base_id": dt.LOC_BASE_ID,
        "item_base_id": dt.ITEM_BASE_ID,
        "retail": {"g_flags": hex(G_FLAGS_REL), "item_arr": hex(ITEM_ARR_REL), "module_base": "0x400000"},
        "sp_flag": dt.SP_FLAG_IDX,
        "scene_names": {str(k): v for k, v in dt.scene_names().items()},
        "scene_floors": {str(k): v for k, v in dt.scene_floors().items()},
        "statue_scenes": list(dt.statue_scenes()),
        "locations": locations,
        "items": items,
    }


def dump(doc):
    return json.dumps(doc, ensure_ascii=False, indent=1, sort_keys=True) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apworld", default=DEFAULT_APWORLD)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="compare with --out instead of writing it")
    a = ap.parse_args()
    doc = build(a.apworld)
    text = dump(doc)
    if a.check:
        with open(a.out, encoding="utf-8") as f:
            old = json.load(f)
        old.get("source", {}).pop("commit", None)
        doc["source"].pop("commit", None)
        same = old == json.loads(dump(doc))
        print("ap_import: %s is %s" % (a.out, "up to date" if same else "STALE (re-run tools/ap_import.py)"))
        return 0 if same else 1
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    kinds = {}
    for it in doc["items"]:
        kinds[it["kind"]] = kinds.get(it["kind"], 0) + 1
    methods = {}
    for l in doc["locations"]:
        methods[l["detect"]["method"]] = methods.get(l["detect"]["method"], 0) + 1
    print("ap_import: %d locations %s, %d items %s -> %s" % (
        len(doc["locations"]), methods, len(doc["items"]), kinds, os.path.relpath(a.out, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
