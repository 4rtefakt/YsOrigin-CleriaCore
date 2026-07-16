"""Audit the XSO corpus for branches our VM would get WRONG at runtime.

The VM skips unknown sub-ops by length. That is safe for the static grant scan,
but at runtime it leaves the accumulator STALE — so any conditional jump reading
it branches on a value we never computed. This lists the unimplemented sub-ops
whose result feeds a conditional, i.e. exactly where runtime execution silently
diverges from the real game.

Keep KNOWN in sync with the sub-ops XsoVm actually implements (src/cleria/xso.cpp).

    python tools/xso_audit.py
"""
import glob, struct, os, collections
# Which sub-ops does the VM actually implement?
KNOWN = {0x5F,0x60,0x64,0x65,0x66,0x67,0x69,0x6B,0x71,0x73,0x75,0x77,0x116,0xD5,
         0x169,0x175,0x16B,0x16C,
         # comparison + test family, RE'd 2026-07-16
         0x5C,0x5D,0x61,0x62,0x63,0x90,0x81}
# Find scripts where an UNKNOWN sub-op is immediately followed by a conditional
# jump -- i.e. the branch is decided by a value we never computed.
risky = collections.Counter()
sites = collections.defaultdict(list)
for f in glob.glob('assets/MAP/**/*.XSO', recursive=True):
    d = open(f,'rb').read()
    if len(d) < 0x24 or d[:4] != b'XSR\x00': continue
    n = struct.unpack_from('<I', d, 0x1C)[0]
    if 0x24+4*n > len(d): continue
    code = list(struct.unpack_from('<%di'%n, d, 0x24))
    pc = 0; last_sub = None
    while pc < len(code):
        w = code[pc]; cls = (w >> 24) & 0xff
        if cls == 2:
            sub = (w>>12)&0xfff; na = w & 0xff
            last_sub = sub if sub not in KNOWN else None
            pc += 1 + ((w>>8)&0xf) + na
        elif cls in (5,6,7,8,9,0xA):
            if last_sub is not None:
                risky[last_sub] += 1
                sites[last_sub].append(os.path.basename(f))
            last_sub = None; pc += 2
        elif cls in (3,0xB,4,0xE,0xF,0x10,0x11,0x12,0x13): last_sub=None; pc += 2
        elif cls == 1: break
        else: last_sub=None; pc += 1
print("Unimplemented sub-ops whose result feeds a conditional jump")
print("(the branch is then decided by a stale accumulator):\n")
print("  %-8s %6s  %s" % ("sub-op","sites","example files"))
for sub, cnt in risky.most_common(10):
    ex = sorted(set(sites[sub]))[:3]
    print("  %#-8x %6d  %s" % (sub, cnt, ", ".join(ex)))
print("\n  total risky branch sites:", sum(risky.values()))
