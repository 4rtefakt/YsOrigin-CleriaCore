#!/usr/bin/env python3
"""A realistic Ys Origin `slot_data` fixture for the offline Archipelago tests (tools/aplua_regress.py).

The apworld's World.fill_slot_data (YsOrigin-Archipelago/ys_origin/__init__.py) needs Archipelago
itself to run; this tool rebuilds the same dict from the apworld's own data_tables.py (pure Python,
read-only, no bytecode written) for the DEFAULT options and a chosen character, so the local test
server (tools/ap_mock_server.py) hands CleriaCore exactly the keys and value forms a real room does
(retail `offset` strings, "S_xxxx" scene tags, string-keyed maps). Keep the key list in step with
fill_slot_data when the apworld changes (`--check` compares with the committed fixture).

    python tools/ap_slotdata.py [--apworld C:/Users/kores/repos/YsOrigin-Archipelago] [--character 0]
    python tools/ap_slotdata.py --check
"""
import argparse
import json
import os
import sys

sys.dont_write_bytecode = True   # never write __pycache__ into the read-only apworld repo
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ap_import  # noqa: E402  (load_data_tables, read_json)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "tests", "ap", "test_slot_data.json")


class _Opt:
    def __init__(self, v):
        self.value = v


def build(apworld, character):
    dt = ap_import.load_data_tables(apworld)
    # the apworld's option defaults (ys_origin/options.py)
    opts = dict(character=character, goal=0, death_link=0, statue_checks=1, blessing_checks=1, boss_checks=1,
                floor_checks=1, room_checks=0, statue_warp_locks=0, random_start=0, starting_items=["Crystal", "Dark Crystal"],
                starting_level=1, starting_weapon_level=1, level_scaling=2, level_margin=0, exp_multiplier_base=20,
                exp_multiplier_catchup=20, exp_catchup_margin=5, blessing_costs=0, blessing_shop_unlock=0, shop_hints=1,
                progressive_blessings=1, progressive_skills=1, blessing_items=0, progressive_armor=1,
                weapon_requirements=1)
    o = type("Options", (), {k: _Opt(v) for k, v in opts.items()})()
    char = dt.char_name(o)
    active = {n for names in dt.locations_by_region(dt.enabled_categories(o), char).values() for n in names}
    meta = dt.LOC_META
    loc_ids = dt.location_name_to_id
    return {
        "character": character,
        "goal": 0,
        "death_link": False,
        "statue_warp_locks": False,
        "statue_unlocks": {},
        "random_start": False,
        "start_statue_scene": 0,
        "start_weapon": dt.weapon_value_for_level(1),
        "start_level": 1,
        "start_items": dt.start_item_indices(["Crystal", "Dark Crystal"]),
        "sp_items": dict(dt.SP_FILLER),
        "sp_flag_idx": dt.SP_FLAG_IDX,
        "item_tiers": dt.item_tiers(),
        "progressive_gear": dt.progressive_gear_slot_data(char),
        "level_scaling": 2,
        "level_margin": 0,
        "exp_base_mult": 20,
        "exp_catchup_mult": 20,
        "exp_catchup_margin": 5,
        "weapon_requirements": 1,
        "scene_levels": dt.scene_levels(),
        "scene_floors": dt.scene_floors(),
        "location_signals": {n: i for n, i in loc_ids.items() if n in active},
        "location_detect": {n: meta[n]["detect"] for n in sorted(active)},
        "item_index": dt.item_index,
        "skill_grants": dt.skill_grants(),
        "scene_names": dt.scene_names(),
        "scene_locations": dt.scene_locations_map(active, loc_ids),
        "floor_locations": dt.floor_locations_map(active, loc_ids),
        "blessing_names": dt.blessing_location_names(active, loc_ids),
        "shop_hints": True,
        "blessing_costs": {},
        "blessing_shop_unlock": 0,
        "blessing_items": False,
        "blessing_vanilla_price_map": {},
        "statue_scenes": dt.statue_scenes(),
        "suppress_items": dt.suppress_item_indices(active, char),
        "progressive_blessings": {},
        "progressive_skills": dt.progressive_skill_slot_data(),
        "suppress_give_ids": dt.suppress_give_ids(active, char),
    }


def dump(doc):
    return json.dumps(doc, ensure_ascii=False, indent=1, sort_keys=True) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apworld", default=ap_import.DEFAULT_APWORLD)
    ap.add_argument("--character", type=int, default=0, help="0 Yunica (the drive tests' default), 1 Hugo, 2 Toal")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = dump(build(a.apworld, a.character))
    if a.check:
        with open(a.out, encoding="utf-8") as f:
            same = json.load(f) == json.loads(text)        # the data, whatever the line ends of the checkout
        print("ap_slotdata: %s is %s" % (a.out, "up to date" if same else "STALE (re-run tools/ap_slotdata.py)"))
        return 0 if same else 1
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    d = json.loads(text)
    print("ap_slotdata: %d active locations, %d suppressed cells -> %s" % (
        len(d["location_signals"]), len(d["suppress_items"]), os.path.relpath(a.out, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
