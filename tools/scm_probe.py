#!/usr/bin/env python3
"""
scm_probe.py - decode Ys Origin .SCM per-room camera regions.

Reverse-engineered from yso_win.exe v1.1.1.0 (2024-01-08 build):
  FUN_004338c0  SCM file loader        (memcpy record[0x000..0x3DF] -> slot+0x60)
  FUN_0042a3d0  camera runtime update  (region select + blend, table DAT_0076a150)
  FUN_0042b6e0  camera point evaluator (mode dispatch: 0=fixed 1=follow 3=orbit)
  FUN_0042a080  view push              (DAT_007b38f4 = fov) -> D3DXMatrixPerspectiveFovLH

Record = 0x410 (1040) bytes:
    [0x000 .. 0x1D3]  CameraPoint A  -- the LOOK-AT TARGET   (0x1D4 bytes)
    [0x1D4 .. 0x3A7]  CameraPoint B  -- the EYE / position   (0x1D4 bytes)
    [0x3A8 .. 0x40F]  region header  (sticky, aabbs, origin, yaw, fov, blend, name)

CameraPoint (0x1D4 bytes), only the decoded fields:
    +0x00  f32[3]  aabb-ish min      (undecoded)
    +0x18  f32[3]  (read when +0x30 != 0)
    +0x30  u32     sub-mode flag
    +0x34  u32     MODE   0 = fixed point (+0x54)
                          1 = follow player
                          3 = orbit around the other point
    +0x38  f32     yaw   (radians)   [mode 3]
    +0x3c  f32     pitch (radians)   [mode 3]
    +0x40  f32     distance          [mode 3]
    +0x44  u32     auto-yaw flags: !=0 -> yaw tracks atan2 of (player-target)
                                   bit0 clear -> negate the delta
    +0x54  f32[3]  fixed position    [mode 0]

Region header (offsets are absolute in the record):
    +0x3A8  u8      sticky
    +0x3AC  f32[?]  (mostly 0)
    +0x3B8  f32     FOV  (vertical, radians)  -> D3DXMatrixPerspectiveFovLH
    +0x3BC  f32     blend speed (per-frame lerp increment; engine default 0.015)
    +0x3C0  char[]  region name
    +0x3E0  f32[3]  aabb A
    +0x3EC  f32[3]  aabb B
    +0x3F8  f32[3]  origin
    +0x408  f32     region yaw (radians)

Orbit math (mode 3), from FUN_0042b6e0 raw disassembly:
    a  = -pi/2 - B.yaw
    if B.flags: a -= atan2(dz, dx) + pi/2        # face the player
    cp = cos(B.pitch)
    eye.x = target.x - cos(a) * B.dist * cp
    eye.y = target.y + sin(B.pitch) * B.dist
    eye.z = target.z + sin(a) * B.dist * cp
"""
import struct, glob, math, os, sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

STRIDE = 0x410
PT = 0x1D4


def f3(d, o):
    return struct.unpack_from('<3f', d, o)


def read_scm(path):
    d = open(path, 'rb').read()
    magic, zero, stride, count = struct.unpack_from('<4sIII', d, 0)
    assert magic == b'SCM\0', (path, magic)
    assert stride == STRIDE, (path, stride)
    assert len(d) == 16 + stride * count, (path, len(d), count)
    return [d[16 + i * stride: 16 + (i + 1) * stride] for i in range(count)]


def point(r, base):
    return dict(
        mode=struct.unpack_from('<I', r, base + 0x34)[0],
        yaw=struct.unpack_from('<f', r, base + 0x38)[0],
        pitch=struct.unpack_from('<f', r, base + 0x3c)[0],
        dist=struct.unpack_from('<f', r, base + 0x40)[0],
        flags=struct.unpack_from('<I', r, base + 0x44)[0],
        fixed=f3(r, base + 0x54),
    )


def parse(r):
    # region names are CP932 (same as SOB labels)
    name = r[0x3C0:0x3E0].split(b'\0')[0].decode('cp932', 'replace')
    return dict(
        target=point(r, 0x000),          # A
        eye=point(r, PT),                # B
        sticky=r[0x3A8],
        fov=struct.unpack_from('<f', r, 0x3B8)[0],
        blend=struct.unpack_from('<f', r, 0x3BC)[0],
        name=name,
        aabb_a=f3(r, 0x3E0), aabb_b=f3(r, 0x3EC),
        origin=f3(r, 0x3F8),
        region_yaw=struct.unpack_from('<f', r, 0x408)[0],
    )


def eye_from(target, B):
    """Reproduce FUN_0042b6e0 mode-3 orbit placement."""
    a = -math.pi / 2 - B['yaw']
    cp = math.cos(B['pitch'])
    return (target[0] - math.cos(a) * B['dist'] * cp,
            target[1] + math.sin(B['pitch']) * B['dist'],
            target[2] + math.sin(a) * B['dist'] * cp)


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else 'assets/MAP'
    files = sorted(glob.glob(os.path.join(root, '**', '*.SCM'), recursive=True))
    for f in files:
        recs = read_scm(f)
        print('\n%s  (%d regions)' % (os.path.basename(f), len(recs)))
        for i, r in enumerate(recs):
            c = parse(r)
            E = c['eye']
            print('  [%d] %-14s fov=%5.1fdeg blend=%.3f  A.mode=%d B.mode=%d '
                  'yaw=%7.2fdeg pitch=%5.1fdeg dist=%5.1f flags=%d'
                  % (i, c['name'], math.degrees(c['fov']), c['blend'],
                     c['target']['mode'], E['mode'],
                     math.degrees(E['yaw']), math.degrees(E['pitch']),
                     E['dist'], E['flags']))
