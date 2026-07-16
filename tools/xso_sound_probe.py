"""Corpus-profile the EVENT-VM sound sub-ops across every XSO script.

The sound sub-ops were located by mapping g_sound.cpp call sites back to the
event VM's jump table (0x56e0b0):

    sub 0xac (172) -> FUN_0043a010  SE_Preload
    sub 0xbc (188) -> FUN_004396f0 + FUN_0043a040   (play)
    sub 0xbd (189) -> FUN_004396f0 + FUN_0043a040   (play)
    sub 0xbe (190) -> FUN_0043a150                  (play)
    sub 0xbf (191) -> FUN_0043a1c0                  (set 1 param)
    sub 0xc0 (192) -> FUN_0043a230                  (set 2 params)

This script checks the operands actually observed in shipped scripts against
EFFECTDEF: if operand 0 is a sound id, every observed value must be a valid
EFFECTDEF id (0..900) and should resolve to a plausible wav name.

Encoding (per tools/xso_dis.py):
    class  = w >> 24                (class 2 = call)
    sub    = (w >> 12) & 0xfff
    operands start at pc + 1 + ((w >> 8) & 0xf), count = w & 0xff
"""
import struct
import sys
import os
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from effectdef_parse import parse_effectdef  # noqa: E402

SOUND_SUBS = {
    0xac: 'SE_Preload  FUN_0043a010',
    0xbc: 'SE_Play     FUN_0043a040',
    0xbd: 'SE_Play     FUN_0043a040',
    0xbe: 'SE_Play_b   FUN_0043a150',
    0xbf: 'SE_Set1     FUN_0043a1c0',
    0xc0: 'SE_Set2     FUN_0043a230',
}


def scan(root):
    hits = defaultdict(list)   # sub -> [(file, [operands])]
    files = sorted({p.resolve() for p in Path(root).rglob('*.[Xx][Ss][Oo]')})
    for f in files:
        b = f.read_bytes()
        if b[:4] != b'XSR\0':
            continue
        nw = struct.unpack_from('<I', b, 0x1c)[0]
        code = b[0x24:0x24 + nw * 4]
        words = list(struct.unpack('<%dI' % (len(code) // 4), code))
        i = 0
        while i < len(words):
            w = words[i]
            cls = w >> 24
            if cls != 2:
                i += 1
                continue
            sub = (w >> 12) & 0xfff
            skip = (w >> 8) & 0xf
            cnt = w & 0xff
            ops = words[i + 1 + skip: i + 1 + skip + cnt]
            if sub in SOUND_SUBS:
                hits[sub].append((f.name, ops))
            i += 1 + skip + cnt
    return hits, len(files)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'assets'
    recs = parse_effectdef(os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        'assets', 'WAVE', 'EFFECT', 'EFFECTDEF.TXT'))
    hits, nfiles = scan(root)
    print(f"scanned {nfiles} XSO files\n")

    for sub in sorted(SOUND_SUBS):
        h = hits.get(sub, [])
        print(f"=== sub 0x{sub:02x} ({sub})  {SOUND_SUBS[sub]} : "
              f"{len(h)} call sites in {len(set(f for f,_ in h))} files ===")
        if not h:
            print("    (never used in the shipped corpus)\n")
            continue
        arities = Counter(len(o) for _, o in h)
        print(f"    operand counts: {dict(arities)}")
        maxn = max(len(o) for _, o in h)
        for k in range(maxn):
            vals = [o[k] for _, o in h if len(o) > k]
            c = Counter(vals)
            rng = f"[{min(vals)}..{max(vals)}]"
            top = ', '.join(f'{v}x{n}' for v, n in c.most_common(6))
            print(f"    op{k}  distinct={len(c):<4} range={rng:<16} top={top}")
        # is op0 a valid EFFECTDEF id?
        op0 = [o[0] for _, o in h if o]
        valid = [v for v in op0 if v in recs]
        print(f"    op0 valid EFFECTDEF ids: {len(valid)}/{len(op0)}")
        if valid:
            ex = Counter(valid).most_common(6)
            for v, n in ex:
                print(f"        id {v:4d} x{n:<4} -> {recs[v].get('filename','?')}")
        bad = sorted(set(v for v in op0 if v not in recs))
        if bad:
            print(f"    op0 NOT valid ids: {bad[:20]}")
        files = Counter(f for f, _ in h)
        print(f"    top files: {', '.join(f'{k} x{v}' for k,v in files.most_common(6))}")
        print()


if __name__ == '__main__':
    main()
