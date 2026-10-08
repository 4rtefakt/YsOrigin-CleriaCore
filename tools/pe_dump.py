"""Dump raw bytes / find 32-bit immediates in yso_win.exe by virtual address.

usage:
  pe_dump.py dump <va> <len>          hexdump a VA range
  pe_dump.py tbl  <va> <stride> <n>   dump as records, resolving string pointers
  pe_dump.py find <u32>               find every LE u32 occurrence, report VA
  pe_dump.py str  <va>                read a C string at VA
"""
import os
import struct
import sys

EXE = os.environ.get("YSO_EXE", r"D:\SteamLibrary\steamapps\common\Ys Origin\yso_win.exe")


def pe(data):
    lf = struct.unpack_from('<I', data, 0x3c)[0]
    coff = lf + 4
    nsec = struct.unpack_from('<H', data, coff + 2)[0]
    optsz = struct.unpack_from('<H', data, coff + 16)[0]
    opt = coff + 20
    base = struct.unpack_from('<I', data, opt + 28)[0]
    secs = []
    so = opt + optsz
    for i in range(nsec):
        b = so + i * 40
        secs.append(dict(
            name=data[b:b + 8].rstrip(b'\0').decode(),
            vsize=struct.unpack_from('<I', data, b + 8)[0],
            vaddr=struct.unpack_from('<I', data, b + 12)[0],
            rsize=struct.unpack_from('<I', data, b + 16)[0],
            raddr=struct.unpack_from('<I', data, b + 20)[0]))
    return base, secs


def v2o(base, secs, va):
    r = va - base
    for s in secs:
        if s['vaddr'] <= r < s['vaddr'] + max(s['vsize'], s['rsize']):
            o = s['raddr'] + (r - s['vaddr'])
            if o < s['raddr'] + s['rsize']:
                return o
    return None


def o2v(base, secs, off):
    for s in secs:
        if s['raddr'] <= off < s['raddr'] + s['rsize']:
            return base + s['vaddr'] + (off - s['raddr'])
    return None


def rdstr(data, base, secs, va, maxlen=64):
    o = v2o(base, secs, va)
    if o is None:
        return None
    e = data.find(b'\0', o, o + maxlen)
    if e < 0:
        return None
    s = data[o:e]
    try:
        t = s.decode('cp932')
    except Exception:
        return None
    if any(ord(c) < 0x20 for c in t):
        return None
    return t


def main():
    data = open(EXE, 'rb').read()
    base, secs = pe(data)
    cmd = sys.argv[1]

    if cmd == 'dump':
        va = int(sys.argv[2], 0)
        n = int(sys.argv[3], 0)
        o = v2o(base, secs, va)
        for i in range(0, n, 16):
            chunk = data[o + i:o + i + 16]
            hx = ' '.join(f'{b:02x}' for b in chunk)
            asc = ''.join(chr(b) if 0x20 <= b < 0x7f else '.' for b in chunk)
            print(f'{va+i:08x}  {hx:<47}  {asc}')

    elif cmd == 'tbl':
        va = int(sys.argv[2], 0)
        stride = int(sys.argv[3], 0)
        n = int(sys.argv[4], 0)
        for i in range(n):
            a = va + i * stride
            o = v2o(base, secs, a)
            if o is None:
                print(f'[{i}] {a:08x} <unmapped>')
                continue
            words = struct.unpack_from('<%dI' % (stride // 4), data, o)
            line = f'  [{i:3d}] 0x{a:08x}:' + ''.join(f' {w:08x}' for w in words)
            for j, w in enumerate(words):
                s = rdstr(data, base, secs, w)
                if s:
                    line += f'  +{j*4}->"{s}"'
            print(line)

    elif cmd == 'find':
        needle = struct.pack('<I', int(sys.argv[2], 0))
        start = 0
        hits = 0
        while True:
            i = data.find(needle, start)
            if i < 0:
                break
            start = i + 1
            va = o2v(base, secs, i)
            sec = next((s['name'] for s in secs
                        if s['raddr'] <= i < s['raddr'] + s['rsize']), '?')
            print(f'  file=0x{i:06x} va=0x{va:08x} sec={sec}')
            hits += 1
        print(f'{hits} hits')

    elif cmd == 'str':
        va = int(sys.argv[2], 0)
        print(repr(rdstr(data, base, secs, va, 200)))


if __name__ == '__main__':
    main()
