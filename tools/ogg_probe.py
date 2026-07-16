"""Scan Ogg Vorbis files: identification header + comment header + last-page granule.

Reports sample rate, channels, bitrate hints, total samples (from final page
granulepos) and every Vorbis comment tag (looking for LOOPSTART/LOOPLENGTH etc).
"""
import struct
import sys
import os
import glob


def pages(data):
    """Yield (offset, header_type, granulepos, serial, seq, payload) for each Ogg page."""
    off = 0
    n = len(data)
    while off + 27 <= n:
        if data[off:off + 4] != b'OggS':
            nxt = data.find(b'OggS', off + 1)
            if nxt < 0:
                return
            off = nxt
            continue
        ver = data[off + 4]
        htype = data[off + 5]
        gran = struct.unpack_from('<q', data, off + 6)[0]
        serial = struct.unpack_from('<I', data, off + 14)[0]
        seq = struct.unpack_from('<I', data, off + 18)[0]
        nseg = data[off + 26]
        segs = data[off + 27:off + 27 + nseg]
        body_len = sum(segs)
        body_off = off + 27 + nseg
        yield (off, htype, gran, serial, seq, data[body_off:body_off + body_len])
        off = body_off + body_len


def parse_ident(pkt):
    # \x01vorbis
    if not pkt.startswith(b'\x01vorbis'):
        return None
    ver, ch, rate = struct.unpack_from('<IBI', pkt, 7)
    br_max, br_nom, br_min = struct.unpack_from('<iii', pkt, 16)
    return dict(version=ver, channels=ch, rate=rate,
                br_max=br_max, br_nom=br_nom, br_min=br_min)


def parse_comment(pkt):
    if not pkt.startswith(b'\x03vorbis'):
        return None, []
    off = 7
    (vlen,) = struct.unpack_from('<I', pkt, off)
    off += 4
    vendor = pkt[off:off + vlen].decode('utf-8', 'replace')
    off += vlen
    (n,) = struct.unpack_from('<I', pkt, off)
    off += 4
    tags = []
    for _ in range(n):
        if off + 4 > len(pkt):
            break
        (l,) = struct.unpack_from('<I', pkt, off)
        off += 4
        tags.append(pkt[off:off + l].decode('utf-8', 'replace'))
        off += l
    return vendor, tags


def probe(path):
    with open(path, 'rb') as f:
        data = f.read()
    ident = None
    vendor = None
    tags = []
    last_gran = -1
    serials = set()
    # Header packets are in the first pages; walk enough to catch them.
    pkt_accum = {}
    for (off, htype, gran, serial, seq, body) in pages(data):
        serials.add(serial)
        if gran >= 0:
            last_gran = max(last_gran, gran)
        if seq == 0 and ident is None:
            ident = parse_ident(body)
        if vendor is None and b'vorbis' in body[:8]:
            v, t = parse_comment(body)
            if v is not None:
                vendor, tags = v, t
        # comment header may start on page 1
        if vendor is None and seq == 1:
            v, t = parse_comment(body)
            if v is not None:
                vendor, tags = v, t
    return dict(path=path, size=len(data), ident=ident, vendor=vendor,
                tags=tags, last_granule=last_gran, streams=len(serials))


def main():
    args = sys.argv[1:]
    files = []
    for a in args:
        files.extend(sorted(glob.glob(a)))
    if not files:
        print("usage: ogg_probe.py <glob>...")
        return
    for p in files:
        r = probe(p)
        i = r['ident'] or {}
        rate = i.get('rate', 0)
        secs = (r['last_granule'] / rate) if rate else 0
        print(f"{os.path.basename(p):16s} {r['size']:>10,}B  "
              f"{i.get('channels','?')}ch {rate:>6}Hz  "
              f"nom={i.get('br_nom',0):>7}  "
              f"samples={r['last_granule']:>10,} ({secs:7.2f}s)  "
              f"streams={r['streams']}")
        print(f"    vendor: {r['vendor']}")
        if r['tags']:
            for t in r['tags']:
                print(f"    TAG: {t}")
        else:
            print("    TAG: (none)")


if __name__ == '__main__':
    main()
