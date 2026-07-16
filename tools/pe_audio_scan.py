"""Scan yso_win.exe for audio-related strings and imported DLLs/symbols.

Prints: imported DLL names + selected import symbols, and any ASCII string
matching an audio keyword, with its virtual address.
"""
import struct
import re
import sys

EXE = r"D:\SteamLibrary\steamapps\common\Ys Origin\yso_win.exe"

KEYWORDS = [
    b'EFFECTDEF', b'effectdef', b'bgmdef', b'BGMDEF', b'.ogg', b'.OGG',
    b'music', b'MUSIC', b'wave', b'WAVE', b'sound', b'Sound', b'SOUND',
    b'LOOPSTART', b'LOOPLENGTH', b'vorbis', b'Vorbis', b'ogg',
    b'DirectSound', b'dsound', b'XAudio', b'xaudio', b'wav', b'WAV',
    b'g_sound', b'bgm', b'BGM', b'se_', b'volume', b'Volume',
]


def load(path):
    with open(path, 'rb') as f:
        return f.read()


def pe_info(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[e_lfanew:e_lfanew + 4] == b'PE\0\0', 'not a PE'
    coff = e_lfanew + 4
    nsec = struct.unpack_from('<H', data, coff + 2)[0]
    opt_size = struct.unpack_from('<H', data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from('<H', data, opt)[0]
    pe32 = (magic == 0x10b)
    image_base = struct.unpack_from('<I' if pe32 else '<Q', data,
                                    opt + (28 if pe32 else 24))[0]
    ddir = opt + (96 if pe32 else 112)
    dirs = []
    nrva = struct.unpack_from('<I', data, opt + (92 if pe32 else 108))[0]
    for i in range(nrva):
        rva, sz = struct.unpack_from('<II', data, ddir + i * 8)
        dirs.append((rva, sz))
    secs = []
    so = opt + opt_size
    for i in range(nsec):
        b = so + i * 40
        name = data[b:b + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, b + 8)
        secs.append(dict(name=name, vsize=vsize, vaddr=vaddr,
                         rsize=rsize, raddr=raddr))
    return dict(image_base=image_base, secs=secs, dirs=dirs, pe32=pe32)


def rva2off(info, rva):
    for s in info['secs']:
        if s['vaddr'] <= rva < s['vaddr'] + max(s['vsize'], s['rsize']):
            return s['raddr'] + (rva - s['vaddr'])
    return None


def off2va(info, off):
    for s in info['secs']:
        if s['raddr'] <= off < s['raddr'] + s['rsize']:
            return info['image_base'] + s['vaddr'] + (off - s['raddr'])
    return None


def cstr(data, off, maxlen=260):
    e = data.find(b'\0', off, off + maxlen)
    if e < 0:
        return b''
    return data[off:e]


def imports(data, info):
    rva, sz = info['dirs'][1]
    if not rva:
        return []
    off = rva2off(info, rva)
    out = []
    while True:
        oft, ts, fc, name_rva, first = struct.unpack_from('<IIIII', data, off)
        if name_rva == 0 and first == 0:
            break
        dll = cstr(data, rva2off(info, name_rva)).decode('ascii', 'replace')
        syms = []
        t = oft or first
        if t:
            to = rva2off(info, t)
            i = 0
            while True:
                v = struct.unpack_from('<I', data, to + i * 4)[0]
                if v == 0:
                    break
                if not (v & 0x80000000):
                    o = rva2off(info, v)
                    if o:
                        syms.append(cstr(data, o + 2).decode('ascii', 'replace'))
                else:
                    syms.append('#%d' % (v & 0xffff))
                i += 1
        out.append((dll, syms))
        off += 20
    return out


def main():
    data = load(EXE)
    info = pe_info(data)
    print(f"image_base=0x{info['image_base']:x}  pe32={info['pe32']}")
    print("sections:")
    for s in info['secs']:
        print(f"  {s['name']:9s} va=0x{info['image_base']+s['vaddr']:08x} "
              f"vsize=0x{s['vsize']:>7x} raw=0x{s['raddr']:>7x}/0x{s['rsize']:x}")
    print("\n=== IMPORTS ===")
    for dll, syms in imports(data, info):
        print(f"\n{dll}  ({len(syms)} symbols)")
        low = dll.lower()
        show = low.startswith(('dsound', 'xaudio', 'winmm', 'ole32', 'msacm',
                               'x3daudio', 'openal', 'fmod', 'bass'))
        for s in syms if show else syms[:0]:
            print(f"    {s}")

    print("\n=== AUDIO STRINGS ===")
    seen = set()
    for kw in KEYWORDS:
        start = 0
        while True:
            i = data.find(kw, start)
            if i < 0:
                break
            start = i + 1
            # expand to full printable run
            b = i
            while b > 0 and 0x20 <= data[b - 1] < 0x7f:
                b -= 1
            e = i
            while e < len(data) and 0x20 <= data[e] < 0x7f:
                e += 1
            s = data[b:e]
            if len(s) < 3 or len(s) > 200:
                continue
            va = off2va(info, b)
            if va is None:
                continue
            key = (va, s)
            if key in seen:
                continue
            seen.add(key)
    for va, s in sorted(seen):
        print(f"0x{va:08x}  {s.decode('ascii','replace')!r}")


if __name__ == '__main__':
    main()
