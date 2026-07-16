"""List a room's enemies (+ positions + sprite) from its .SCT sidecar.

    python tools/sct_enemies.py S_5001
    python tools/sct_enemies.py --all          # every room, summary
    python tools/sct_enemies.py --roster       # the 90-enemy roster

Chain (all [C-DIS] from yso_win.exe, see build/enemy_re/ENEMY_PLACEMENT.md):
    <ROOM>.SCT  record          -> name @+0x00, yaw @+0x108, pos @+0x10C
      name --strcmp--> MISC/CASTINFO.DAT  (186 x 0x200)   [FUN_004353b0]
        castinfo +0x60 = cast_id ; +0x6c = actor class (2/4/0x10=monster)
        cast_id --> enemy_index --> MISC/ENEMYINFO.DAT (90 x 0x108) [FUN_00459410]
"""
import argparse
import collections
import glob
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
MAP = os.path.join(ROOT, "MAP")
MISC = os.path.join(ROOT, "MISC")

SCT_STRIDE = 0x2B0
CAST_STRIDE = 0x200
ENEMY_STRIDE = 0x108
CAST_ADOL = 13


def read_table(path, magic_is_str=False):
    d = open(path, "rb").read()
    ver, _, stride, count = struct.unpack_from("<IIII", d, 0)
    assert len(d) == 0x10 + stride * count, f"{path}: bad container"
    return d, stride, count


class Cast:
    """MISC/CASTINFO.DAT - the cast (actor type) table."""

    def __init__(self):
        self.d, self.stride, self.count = read_table(os.path.join(MISC, "CASTINFO.DAT"))
        self.by_name = {}
        for i in range(self.count):
            self.by_name[self.ident(i)] = i

    def _o(self, i):
        return 0x10 + i * self.stride

    def ident(self, i):
        return self.d[self._o(i):self._o(i) + 0x10].split(b"\0")[0].decode("ascii", "replace")

    def display(self, i):
        return self.d[self._o(i) + 0x10:self._o(i) + 0x24].split(b"\0")[0].decode("ascii", "replace")

    def code(self, i):
        return self.d[self._o(i) + 0x24:self._o(i) + 0x30].split(b"\0")[0].decode("ascii", "replace")

    def i32(self, i, off):
        return struct.unpack_from("<i", self.d, self._o(i) + off)[0]

    def cast_id(self, i):
        return self.i32(i, 0x60)          # == i, verified corpus-wide

    def actor_class(self, i):
        return self.i32(i, 0x6C)          # 2 / 4 / 0x10(monster)


def enemy_index(cast_id):
    """Exact transcription of yso_win.exe FUN_00459410 @0045965c.

        if ((0xd < id && id < 0x54) || (0xa5 < id && id < 0xba)) {
            idx = id - 0xe;  if (0x53 < idx) idx = id - 0x60;
        }
    Yields exactly 0..89 == the 90 ENEMYINFO records.
    """
    if (0xD < cast_id < 0x54) or (0xA5 < cast_id < 0xBA):
        idx = cast_id - 0xE
        if 0x53 < idx:
            idx = cast_id - 0x60
        return idx
    return None


class EnemyInfo:
    def __init__(self):
        self.d, self.stride, self.count = read_table(os.path.join(MISC, "ENEMYINFO.DAT"))

    def level(self, idx):
        """+0xe8 int, range 1..60. Drives the GAMEINFO stat lerp (FUN_004202e0)."""
        return struct.unpack_from("<i", self.d, 0x10 + idx * self.stride + 0xE8)[0]


def room_path(room):
    hits = glob.glob(os.path.join(MAP, "*", "*", room + ".SCT"))
    if not hits:
        raise SystemExit(f"no SCT for room {room!r}")
    return hits[0]


def parse_sct(path):
    """Yield (name, yaw_deg, (x,y,z)) per record."""
    d = open(path, "rb").read()
    magic, _, stride, count = struct.unpack_from("<4sIII", d, 0)
    assert magic == b"SCT\0" and stride == SCT_STRIDE
    assert len(d) == 0x10 + stride * count
    for i in range(count):
        o = 0x10 + i * stride
        name = d[o:o + 0x104].split(b"\0")[0].decode("ascii", "replace")
        yaw = struct.unpack_from("<i", d, o + 0x108)[0]
        pos = struct.unpack_from("<3f", d, o + 0x10C)
        yield name, yaw, pos


def enemies_of(room, cast, einfo, path=None):
    """-> (player_spawn or None, [enemy dicts]). `room` may be a name or a .SCT path."""
    spawn, out = None, []
    for name, yaw, pos in parse_sct(path or room_path(room)):
        if not name:
            continue
        ci = cast.by_name.get(name)
        if ci is None:
            continue
        if ci == CAST_ADOL:
            spawn = {"yaw": yaw, "pos": pos}
            continue
        ei = enemy_index(cast.cast_id(ci))
        if ei is None:
            continue
        out.append({
            "name": name,
            "display": cast.display(ci),
            "cast_id": ci,
            "enemy_index": ei,
            "level": einfo.level(ei),
            "sprite": "ANI/" + name.replace("CAST_M", "M_") + ".AIA",
            "yaw": yaw,
            "pos": pos,
        })
    return spawn, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("room", nargs="?")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--roster", action="store_true")
    a = ap.parse_args()

    cast, einfo = Cast(), EnemyInfo()

    if a.roster:
        print(f"{'idx':>3} {'cast':<12} {'display':<22} {'lvl':>3}  sprite")
        for i in range(cast.count):
            ei = enemy_index(cast.cast_id(i))
            if ei is None:
                continue
            stem = cast.ident(i).replace("CAST_M", "M_")
            ok = "" if os.path.exists(os.path.join(ROOT, "ANI", stem + ".AIA")) else "   (NO AIA)"
            print(f"{ei:3d} {cast.ident(i):<12} {cast.display(i):<22} "
                  f"{einfo.level(ei):3d}  ANI/{stem}.AIA{ok}")
        return

    if a.all:
        tot = collections.Counter()
        rooms = 0
        for p in sorted(glob.glob(os.path.join(MAP, "**", "*.SCT"), recursive=True)):
            r = os.path.basename(p)[:-4]
            _, es = enemies_of(r, cast, einfo, path=p)
            if es:
                rooms += 1
                print(f"{r:<10} {len(es):3d} enemies  "
                      f"{', '.join(sorted({e['display'] for e in es}))}")
            for e in es:
                tot[e["display"]] += 1
        print(f"\n{rooms} rooms with enemies, {sum(tot.values())} placements, "
              f"{len(tot)} distinct enemy types")
        return

    if not a.room:
        raise SystemExit("give a room (e.g. S_5001), or --all / --roster")

    spawn, es = enemies_of(a.room, cast, einfo)
    if spawn:
        print(f"player spawn (CAST_ADOL): pos=({spawn['pos'][0]:.3f}, "
              f"{spawn['pos'][1]:.3f}, {spawn['pos'][2]:.3f}) yaw={spawn['yaw']}")
    print(f"\n{len(es)} enemies in {a.room}:")
    print(f"{'#':>3} {'cast':<12} {'display':<20} {'ei':>3} {'lvl':>3} {'yaw':>5} "
          f"{'X':>9} {'Y':>7} {'Z':>9}  sprite")
    for i, e in enumerate(es):
        x, y, z = e["pos"]
        yaw = "rand" if e["yaw"] in (360, -1) else str(e["yaw"])
        print(f"{i:3d} {e['name']:<12} {e['display']:<20} {e['enemy_index']:3d} "
              f"{e['level']:3d} {yaw:>5} {x:9.3f} {y:7.3f} {z:9.3f}  {e['sprite']}")


if __name__ == "__main__":
    main()
