#!/usr/bin/env python3
"""xso_room_log.py - run every floor-1..3 room headlessly and collect the VM's own log of the
sub-ops it skips as unmodelled (CLERIA_XSO_LOG=1 -> "[xso] unhandled 0xNNN in <script>").

Each room starts from the fresh New-Game flag state (the room root's story-gated branches decide
what runs), so this sees the scripts a room starts on entry, not every script it owns; the
static census (tools/xso_floor_ops.py) is the complete count.

    python tools/xso_room_log.py --exe build_diff/Release/cleria-view.exe --assets <root>
                                 --scratch <dir> [--chars 1,2,3] [--ticks 240] [--out log.txt]
"""
import argparse, concurrent.futures as cf, os, re, subprocess, sys
from collections import defaultdict

GROUPS = ["S_10", "S_11", "S_20", "S_21", "S_30", "S_31"]


def rooms(assets):
    out = []
    for g in GROUPS:
        gd = os.path.join(assets, "MAP", g)
        if not os.path.isdir(gd):
            continue
        for r in sorted(os.listdir(gd)):
            sob = os.path.join(gd, r, r + ".SOB")
            if os.path.exists(sob):
                out.append((r, sob))
    return out


def run_one(exe, assets, sob, ch, ticks, scratch):
    env = dict(os.environ, CLERIA_MUTE="1", CLERIA_CHAR=str(ch), CLERIA_XSO_LOG="1",
               CLERIA_DRIVE="%d:quit" % ticks,
               CLERIA_INI=os.path.join(scratch, "yso_win.ini"),
               CLERIA_SAVE_DIR=os.path.join(scratch, "saves"))
    env.pop("CLERIA_DRIVE_VISIBLE", None)
    try:
        r = subprocess.run([exe, assets, sob], env=env, capture_output=True, text=True, timeout=180)
        return r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return "TIMEOUT"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--assets', required=True)
    ap.add_argument('--scratch', required=True)
    ap.add_argument('--chars', default='1')
    ap.add_argument('--ticks', type=int, default=240)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--out', default=None)
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    os.makedirs(os.path.join(a.scratch, "saves"), exist_ok=True)
    jobs = [(r, sob, int(c)) for r, sob in rooms(a.assets) for c in a.chars.split(',')]
    hits = defaultdict(set)          # sub -> {(room, char, script)}
    fails = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = {ex.submit(run_one, a.exe, a.assets, sob, c, a.ticks, a.scratch): (r, c) for r, sob, c in jobs}
        for f in cf.as_completed(futs):
            r, c = futs[f]
            out = f.result()
            if out == "TIMEOUT" or "[events] room" not in out:
                fails.append((r, c, out[-300:] if out else ""))
            for m in re.finditer(r'\[xso\] unhandled 0x([0-9a-f]+) in (\S*)', out):
                hits[int(m.group(1), 16)].add((r, c, m.group(2)))
    lines = ["rooms run: %d (chars %s, %d ticks each); runs without a live room root: %d"
             % (len(jobs), a.chars, a.ticks, len(fails))]
    for sub in sorted(hits):
        sc = sorted(set(s for _, _, s in hits[sub]))
        rm = sorted(set(r for r, _, _ in hits[sub]))
        lines.append("0x%03x  rooms %-3d %s  scripts: %s" % (sub, len(rm), ",".join(rm[:8]), ",".join(sc[:6])))
    for r, c, tail in fails[:10]:
        lines.append("  no-root %s c%d: %s" % (r, c, tail.replace('\n', ' | ')[-160:]))
    txt = "\n".join(lines)
    print(txt)
    if a.out:
        open(a.out, 'w', encoding='utf-8').write(txt + "\n")


if __name__ == '__main__':
    main()
