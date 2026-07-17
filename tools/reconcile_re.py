#!/usr/bin/env python3
"""Find drift between the RE notes in build/*_re/ and what src/ actually claims.

Usage: python tools/reconcile_re.py [--stale]


This exists because the same failure happened three times in one session, and none of
them was a hard problem:

  * aia.hpp said "DO NOT ASSUME IT IS AN OVERLAY" after we had proved it was one and
    shipped the compositor.
  * aso.hpp called the footstep-sound question [UNKNOWN] while build/foot_re/ held the
    answer, in a dump our own script had labelled "(SE play)".
  * sct.hpp asserted `--cast_id--> ENEMYINFO` -- arithmetically impossible, 186 casts
    into 90 records -- and build/enemy_re/ENEMY_PLACEMENT.md had the real formula
    written out correctly the whole time.

The expensive failure mode on this project is not being wrong. It is being RIGHT and
leaving the wrong version in the place someone reads. A stale comment is
indistinguishable from a current one at the point of reading, and `build/` is
gitignored, so nothing links a note to the code that should consume it.

This is a REPORTER, not a linter. It cannot know which side is right -- it points at
pairs a human should look at. Run it after any RE pass lands:

    python tools/reconcile_re.py            # drift report
    python tools/reconcile_re.py --stale    # only src claims an artifact contradicts
"""
import argparse
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Artifacts carry CP932 room names and box-drawing; don't die on a console codepage.
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# Tokens too generic to match on: every struct has a +0x10.
TOO_GENERIC = re.compile(r'^\+?0x[0-9a-f]{1,2}$', re.I)

# A claim in src/ that is explicitly open. If an artifact discusses the same symbol,
# that pair is worth a look -- we may already know.
OPEN_TAG = re.compile(r'\[(UNKNOWN|H-high|H-medium)\]')

# Words that mark an artifact as having SETTLED something.
SETTLED = re.compile(r'\[C-DIS\]|\[C-COR\]|\bsolved\b|\bconfirmed\b|\bproven\b', re.I)


def tokens(text):
    """Symbols specific enough to join a src claim to an artifact."""
    out = set()
    for m in re.finditer(r'(?:FUN_|DAT_)[0-9a-fA-F]{6,8}', text):
        out.add(m.group(0))
    for m in re.finditer(r'0x[0-9a-fA-F]{4,8}', text):
        out.add(m.group(0))
    for m in re.finditer(r'\+0x[0-9a-fA-F]{3,4}', text):
        out.add(m.group(0))
    return {t for t in out if not TOO_GENERIC.match(t)}


def load(patterns):
    files = []
    for pat in patterns:
        for p in glob.glob(os.path.join(ROOT, pat), recursive=True):
            if not os.path.isfile(p):
                continue
            try:
                files.append((os.path.relpath(p, ROOT),
                              io.open(p, encoding='utf-8', errors='replace').read()))
            except OSError:
                pass
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stale', action='store_true',
                    help='only report src claims an artifact appears to have settled')
    args = ap.parse_args()

    srcs = load(['src/cleria/*.hpp', 'src/cleria/*.cpp', 'apps/*/main.cpp'])
    arts = load(['build/*_re/*.md', 'build/*_re/*.json', 'build/source_map/*.md'])
    if not arts:
        print('no RE artifacts found under build/*_re/ -- nothing to reconcile')
        print('(build/ is gitignored; these notes live only on the machine that made them)')
        return 0

    print(f'{len(srcs)} source files vs {len(arts)} RE artifacts\n')
    hits = 0
    for sp, st in srcs:
        for m in OPEN_TAG.finditer(st):
            lo, hi = max(0, m.start() - 160), min(len(st), m.end() + 110)
            ctx = ' '.join(st[lo:hi].split())
            line = st[:m.start()].count('\n') + 1
            toks = tokens(ctx)
            if not toks:
                continue
            # A claim that already cites an RE note is reconciled by construction.
            # Drift is a claim that does NOT know the note exists.
            if re.search(r'build/[a-z_]+(_re)?/', ctx):
                continue
            for ap_, at in arts:
                shared = {t for t in toks if t.lower() in at.lower()}
                if not shared:
                    continue
                # Does the artifact look like it SETTLED this symbol?
                for t in shared:
                    for am in re.finditer(re.escape(t), at, re.I):
                        a_lo = max(0, am.start() - 200)
                        a_hi = min(len(at), am.end() + 200)
                        near = at[a_lo:a_hi]
                        if args.stale and not SETTLED.search(near):
                            continue
                        hits += 1
                        print(f'{sp}:{line}  [{m.group(1)}]  {t}')
                        print(f'    src: {ctx[:130]}')
                        print(f'    {ap_}: {" ".join(near.split())[:130]}')
                        print()
                        break
                    break
                break
    if not hits:
        print('no drift found: every open claim in src/ is open in the notes too.')
    else:
        print(f'{hits} pair(s) to check by hand. This tool cannot tell which side is')
        print('right -- read both. An artifact can be stale too.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
