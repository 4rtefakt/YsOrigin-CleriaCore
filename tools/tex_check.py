"""Which of a room's material textures resolve to DDS files?"""
import struct
import sys
from pathlib import Path

root = Path(sys.argv[1])
sob_name = sys.argv[2]

idx = set(f.name.lower() for f in root.rglob("*.[Dd][Dd][Ss]"))


def resolve(path):
    key = path.replace("\\", "/").lower()
    if key.startswith("data/"):
        key = key[5:]
    return root / key


sob = next(root.rglob(sob_name)).read_bytes()
cnt = struct.unpack_from("<I", sob, 12)[0]
seen, hit, miss = set(), [], []
for i in range(cnt):
    r = 16 + i * 0x2A8
    ypath = sob[r:r + 0x104].split(b"\0")[0].decode("cp932", "replace")
    yf = resolve(ypath)
    if not yf.exists():
        continue
    b = yf.read_bytes()
    n_mat = struct.unpack_from("<I", b, 0x10)[0]
    o = 0xA3 * 4
    for m in range(n_mat):
        rr = o + m * 0xBB * 4
        for t in range(3):
            tn = b[rr + 0xAC + t * 0x30: rr + 0xAC + t * 0x30 + 0x30] \
                .split(b"\0")[0].decode("cp932", "replace")
            if tn and tn.lower() not in seen:
                seen.add(tn.lower())
                (hit if tn.lower() in idx else miss).append(tn)

print(f"unique material textures: {len(seen)}  resolve: {len(hit)}  miss: {len(miss)}")
print("MISSING:", sorted(miss))
print("HIT:", sorted(hit))
