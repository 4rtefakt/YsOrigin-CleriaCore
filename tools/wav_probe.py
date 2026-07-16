"""Cross-check assets/WAVE/EFFECT/*.wav against EFFECTDEF.TXT.

Answers:
  * do all referenced files exist?
  * what WAV format/rate/channels do they use?
  * does the ';s' / ';m' comment field actually predict mono/stereo?
  * do 3d==1 sounds have to be mono (DirectSound requires mono for CTRL3D)?
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from effectdef_parse import parse_effectdef, tokenize  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    'assets', 'WAVE')
EFFDIR = os.path.join(ROOT, 'EFFECT')


def wav_fmt(path):
    """Return (audio_format, channels, rate, bits, data_bytes) from a RIFF WAVE."""
    with open(path, 'rb') as f:
        d = f.read(4096)
    if d[:4] != b'RIFF' or d[8:12] != b'WAVE':
        return None
    off = 12
    fmt = None
    data_sz = None
    size = os.path.getsize(path)
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        csz = struct.unpack_from('<I', d, off + 4)[0]
        if cid == b'fmt ':
            af, ch, sr, br, ba, bits = struct.unpack_from('<HHIIHH', d, off + 8)
            fmt = (af, ch, sr, bits)
        elif cid == b'data':
            data_sz = csz
            break
        off += 8 + csz + (csz & 1)
    if fmt is None:
        return None
    return fmt + (data_sz,)


def comment_fields(path):
    """Pull the ';<s|m>  <status>  <NAME>' comment tail for each id.

    These are COMMENTS -- the game never parses them (see effectdef_parse.py).
    We read them only to test whether they agree with the actual WAV data.
    """
    out = {}
    with open(path, 'rb') as f:
        text = f.read().decode('cp932')
    for line in text.split('\n'):
        if ';' not in line:
            continue
        head, _, tail = line.partition(';')
        head = head.strip()
        if not head or not head[0].isdigit():
            continue
        rid = int(re.match(r'\d+', head).group(0))
        parts = tail.split()
        if not parts:
            continue
        form = parts[0] if parts[0] in ('s', 'm') else None
        status = None
        name = None
        for p in parts:
            if p in ('了', '仮', '没', '未'):
                status = p
            if p.startswith('WAV_'):
                name = p
        out[rid] = dict(form=form, status=status, name=name, raw=tail.strip())
    return out


def main():
    recs = parse_effectdef(os.path.join(EFFDIR, 'EFFECTDEF.TXT'))
    cmts = comment_fields(os.path.join(EFFDIR, 'EFFECTDEF.TXT'))

    missing = []
    fmts = {}
    stats = dict(mono=0, stereo=0)
    agree = disagree = noform = 0
    d3d_mono = d3d_stereo = 0
    seen_files = set()

    for i in sorted(recs):
        fn = recs[i].get('filename')
        if not fn:
            continue
        p = os.path.join(EFFDIR, fn.replace('\\', os.sep) + '.wav')
        if not os.path.exists(p):
            missing.append((i, fn))
            continue
        seen_files.add(os.path.normcase(p))
        fm = wav_fmt(p)
        if fm is None:
            print(f"  !! {fn}: not a RIFF WAVE")
            continue
        af, ch, sr, bits, dsz = fm
        fmts[(af, ch, sr, bits)] = fmts.get((af, ch, sr, bits), 0) + 1
        stats['mono' if ch == 1 else 'stereo'] += 1
        c = cmts.get(i, {})
        form = c.get('form')
        if form is None:
            noform += 1
        else:
            want = 1 if form == 'm' else 2
            if want == ch:
                agree += 1
            else:
                disagree += 1
        if recs[i].get('3d'):
            if ch == 1:
                d3d_mono += 1
            else:
                d3d_stereo += 1

    print(f"definitions: {len(recs)}")
    print(f"missing wav: {len(missing)}")
    for i, fn in missing[:20]:
        print(f"   id {i:4d} -> {fn}.wav")
    if len(missing) > 20:
        print(f"   ... and {len(missing)-20} more")

    print("\nWAV formats (audio_format, channels, rate, bits) -> count:")
    for k, v in sorted(fmts.items(), key=lambda kv: -kv[1]):
        print(f"   {k} -> {v}")
    print(f"\nchannels: {stats}")

    print("\n--- does the ';s'/';m' COMMENT predict channels? ---")
    print(f"   agree={agree}  disagree={disagree}  no-form-field={noform}")

    print("\n--- do 3d==1 sounds use mono? (DirectSound CTRL3D requires mono) ---")
    print(f"   3d sounds mono={d3d_mono}  stereo={d3d_stereo}")

    # files on disk never referenced
    alld = set()
    for dirp, _, files in os.walk(EFFDIR):
        for f in files:
            if f.lower().endswith('.wav'):
                alld.add(os.path.normcase(os.path.join(dirp, f)))
    orphan = alld - seen_files
    print(f"\nwav files on disk: {len(alld)}; referenced: {len(seen_files)}; "
          f"orphaned: {len(orphan)}")
    for o in sorted(orphan)[:20]:
        print(f"   {os.path.relpath(o, EFFDIR)}")

    # status field tally
    st = {}
    for c in cmts.values():
        st[c.get('status')] = st.get(c.get('status'), 0) + 1
    print(f"\nstatus (状態) comment tally: {st}")


if __name__ == '__main__':
    main()
