#!/usr/bin/env python3
"""Parse data\\effect\\eft\\eftlist{,2,3}.dat -- the effect-id -> asset table.

Container (validated: filesize == 8 + 196*count for all five .dat files):

    u32 zero
    u32 count
    Rec recs[count]        # 196 = 0xC4 bytes each

    Rec: u32 id (== index)
         char name[...]    # +0x04, e.g. "pc\\adol\\counter.eft"

Usage: python tools/eftlist_parse.py <file.dat> [--id N] [--grep SUBSTR]
"""
import argparse
import struct
import sys

STRIDE = 196


def parse(path):
    d = open(path, 'rb').read()
    zero, count = struct.unpack('<II', d[:8])
    assert len(d) == 8 + STRIDE * count, \
        '%s: %d != 8+%d*%d' % (path, len(d), STRIDE, count)
    recs = []
    for i in range(count):
        r = d[8 + i * STRIDE: 8 + (i + 1) * STRIDE]
        rid = struct.unpack('<I', r[:4])[0]
        raw = r[4:]
        name = raw.split(b'\0')[0].decode('cp932', 'replace')
        recs.append((i, rid, name, r))
    return zero, count, recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--id', type=int)
    ap.add_argument('--grep')
    ap.add_argument('--named', action='store_true', help='only records with a name')
    ap.add_argument('--hex', action='store_true')
    args = ap.parse_args()

    for path in args.files:
        zero, count, recs = parse(path)
        print('== %s  zero=%d count=%d ==' % (path, zero, count))
        for i, rid, name, r in recs:
            if args.id is not None and i != args.id:
                continue
            if args.grep and args.grep.lower() not in name.lower():
                continue
            if args.named and not name:
                continue
            flag = '' if rid == i else '  <<< id!=index (%d)' % rid
            print('  [%3d] %-46s%s' % (i, name or '-', flag))
            if args.hex:
                for b in range(0, STRIDE, 16):
                    row = r[b:b + 16]
                    print('        +%03x  %-47s  %s' % (
                        b, ' '.join('%02x' % x for x in row),
                        ''.join(chr(c) if 32 <= c < 127 else '.' for c in row)))


if __name__ == '__main__':
    main()
