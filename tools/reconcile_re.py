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

## Why there are TWO passes

The failure above listed sct.hpp as fixed. It drifted again on the SAME link, and this
tool did not see it -- twice over:

  1. It joined claims to notes only on hex tokens (FUN_/DAT_/0x1234). The stale text
     said "the last link is [UNKNOWN]" and named ENEMYINFO and cast_id -- no hex, no
     tokens, so it was skipped before any comparison happened. Symbol NAMES join too.
  2. The answer was not in build/ at all. It was in enemies.hpp, twelve files away in
     the same directory. src-vs-build cannot see src-vs-src.

Pass 2 is the src-vs-src one, and it is the more dangerous class: build/ is scratch
that a reader may never open, but a header asserting [UNKNOWN] about something the
header next to it has SOLVED is a lie told in the exact place someone goes to read.
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


# The names the engine's own data files and helpers go by. These are what a prose
# comment says when it is NOT citing an address -- and prose is exactly where the
# expensive drift lives, because prose is what gets read.
def names(text):
    """Symbol names specific enough to join two src claims."""
    out = set()
    # ENEMYINFO, CASTINFO, GAMEINFO, EEX -- the game's own file/table names.
    for m in re.finditer(r'\b[A-Z][A-Z0-9_]{3,}\b', text):
        out.add(m.group(0))
    # enemy_index_for_cast, decode_frame_composited -- our own helpers.
    for m in re.finditer(r'\b[a-z][a-z0-9]*(?:_[a-z0-9]+){2,}\b', text):
        out.add(m.group(0))
    return {t for t in out if t not in NAME_STOP}


# Names that appear in nearly every header and so join nothing.
NAME_STOP = {'UNKNOWN', 'TODO', 'FIXME', 'NOTE', 'MISC', 'CAST', 'DAT', 'FUN',
             'AND', 'NOT', 'THE', 'BUT', 'ALL', 'ONE', 'TWO', 'HIGH', 'C_DIS',
             'C_COR', 'H_HIGH', 'PLAYER', 'ENEMY', 'LEVEL', 'BASE', 'RAW', 'STR',
             'DEF', 'EXP', 'ATK', 'HP'}


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
    ap.add_argument('--cross', action='store_true',
                    help='also run the src-vs-src pass (noisy: 6/6 false positives '
                         'when written -- a hand-check list, not a gate)')
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

    if args.cross:
        cross_src(srcs)
    return 0


# Each header's title line declares what it owns: "from MISC/ENEMYINFO.DAT" means
# enemies.hpp is where ENEMYINFO questions get answered. This is the whole join, and
# it is precise because ownership is one file per name.
OWNS_FILE = re.compile(r'\bMISC/([A-Z][A-Z0-9_]{2,})\.DAT', re.I)
# An inline helper another file should CALL rather than re-derive.
OWNS_FUNC = re.compile(r'^inline\s+[\w:<>,\s\*&]+?\b([a-z][a-z0-9_]{6,})\s*\(', re.M)


def owners(srcs):
    """name -> the src file that owns it. Ownership is declared, not guessed.

    ** The TITLE LINE only, and its FIRST match only. ** A four-line window was tried
    and got ENEMYINFO wrong: cast.hpp draws the whole chain in its opening comment, so
    it named MISC/ENEMYINFO.DAT on line 3 and -- via setdefault, first file wins --
    stole ownership from enemies.hpp by sort order. It looked right, which is the
    point. "Mentions it" is not "owns it"; line 1 says what a file IS.
    """
    out = {}
    for sp, st in srcs:
        m = OWNS_FILE.search(st.split('\n', 1)[0])
        if m:
            claim(out, m.group(1).upper(), sp)
        for m in OWNS_FUNC.finditer(st):
            claim(out, m.group(1), sp)
    return out


def claim(out, name, sp):
    """Record ownership, refusing to guess when two files both claim a name."""
    prev = out.get(name)
    if prev is None:
        out[name] = sp
    elif prev != sp and prev is not False:
        # Two owners is not an answer; silently keeping the first is how the
        # four-line window produced a wrong map that read as authoritative.
        print(f'note: {name} claimed by both {prev} and {sp} -- not joining on it')
        out[name] = False


def cross_src(srcs):
    """Pass 2: a src file calling [UNKNOWN] a name that ANOTHER src file owns.

    ** OFF by default: measured 6/6 false positives on the tree it was written for. **
    Kept because the ownership map is correct and the question is the right one, but
    the join is admittedly weak -- see below. Run it when you WANT a hand-check list,
    not as a gate.

    Two joins were tried:
      * "a settled marker within 250 chars of the name" -- 18/18 false positives.
        Every header here is dense with [C-DIS], so it fires on any name in any
        well-documented file.
      * ownership (this one) -- 18 -> 6, and the map is now right. But all 6 remaining
        are still wrong, and they fail the same way: an [UNKNOWN] NEAR a name is not a
        claim ABOUT that name. eex.hpp cross-references ENEMYINFO+0xd4 while its open
        question is a different field; gameinfo.hpp quotes its own history ("it sat as
        [UNKNOWN]") next to a name it has since settled. Proximity is not aboutness,
        and no window size fixes that -- the sct.hpp case this was built for had the
        tag and the name four lines apart.

    A reporter nobody trusts is worse than no reporter: 18/18 noise is exactly how the
    drift it was built to catch survived in the first place. So the honest state is
    recorded rather than hidden behind a tuned threshold that happens to score well
    today. Catching this class probably needs the claim's SUBJECT, not its neighbours.
    """
    own = owners(srcs)
    print(f'\n--- src vs src ({len(own)} owned names) ---')
    hits = 0
    for sp, st in srcs:
        for m in OPEN_TAG.finditer(st):
            lo, hi = max(0, m.start() - 200), min(len(st), m.end() + 200)
            ctx = ' '.join(st[lo:hi].split())
            line = st[:m.start()].count('\n') + 1
            for t in sorted(names(ctx)):
                op = own.get(t)
                # Unowned, or the owner IS this file (a header may leave its own
                # questions open -- that is honesty, not drift).
                if not op or op == sp:
                    continue
                # Already points at the owner: reconciled by construction. This is
                # what the fixed sct.hpp does, and it is the shape to aim for.
                if os.path.basename(op) in ctx:
                    continue
                hits += 1
                print(f'{sp}:{line}  [{m.group(1)}]  {t}  -- owned by {op}')
                print(f'    {ctx[:150]}')
                print()
    if not hits:
        print('no cross-src drift: no open claim names a symbol another file owns')
        print('without citing it.')
    else:
        print(f'{hits} pair(s). An [UNKNOWN] naming a symbol another header OWNS is')
        print('the drift that costs most -- headers are what get read. Either the')
        print('owner has the answer (fix the claim) or it does not (cite it anyway).')
    return hits


if __name__ == '__main__':
    sys.exit(main())
