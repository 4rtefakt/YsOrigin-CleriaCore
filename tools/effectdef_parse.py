"""Parser for Ys Origin's data\\wave\\effect\\effectdef.txt (and release\\music\\bgmdef.txt).

This is a faithful reimplementation of the game's own parsers:

  EFFECTDEF  -> FUN_00439ac0  (main/game/g_sound.cpp)   [C-DIS]
  BGMDEF     -> FUN_00439740  (main/game/g_sound.cpp)   [C-DIS]

Both use the SAME hand-rolled tokenizer:

  * the whole file is read into one buffer and NUL-terminated
  * strtok(buf, " \\t\\r:/.,-")            <- note: '\\n' is NOT a delimiter,
                                              but '.' ',' '-' '/' ':' ARE
  * a token starting with ';' => rest of the LINE is a comment: the parser
    scans forward to the next '\\n' and restarts the column counter at 0
  * a token starting with '\\n' => end of record: column counter resets to 0
  * otherwise the token is assigned to column N and N is incremented

Consequences that matter (all verified against the shipped files):
  - because '.' is a delimiter, mindist/maxdist can never contain a decimal
    point; atof() only ever sees integer text
  - because '-' is a delimiter, no value can be negative
  - everything after the ';' on a data line (the "s/m", the "了/仮/没/未"
    status, the WAV_* symbolic name and the Japanese description) is a
    COMMENT and is NOT read by the game. It is build-time documentation only.

EFFECTDEF record: 0xdc (220) bytes, INDEXED BY ID (table[id]).
  +0x00 u32  dsbcaps      = 0x20090 if 3d else 0x80   (DirectSound DSBCAPS mask)
  +0x04 u32  dup          (max simultaneous voices)
  +0x08 u32  flags        bit0 loop, bit2 nega, bit3 manual, bit4 load1st,
                          bit5 wishhw, bit6 forcesw
                          (bit1 and bit15 are set at runtime, not by this file)
  +0x0c u32  id
  +0x10 u32  cancel
  +0x14 char name[0xc0]   e.g. "sys\\s_ok"
  +0xd4 f32  mindist
  +0xd8 f32  maxdist

BGMDEF record: 0x14 (20) bytes, a FLAT LIST in file order (not indexed by id).
  +0x00 char name[0x10]   e.g. "YSO_001"
  +0x10 u32  id
"""
import sys
import os
import struct

DELIMS = set(" \t\r:/.,-")

# EFFECTDEF columns, in order, as dispatched by the switch in FUN_00439ac0.
EFFECTDEF_COLUMNS = [
    'id', 'filename', '3d', 'loop', 'nega', 'dup', 'load1st',
    'manual', 'cancel', 'mindist', 'maxdist', 'forcesw', 'wishhw',
]

# flag bit for each boolean column, as ORed into record+0x08
FLAG_BITS = {
    'loop': 0x01,
    'nega': 0x04,
    'manual': 0x08,
    'load1st': 0x10,
    'wishhw': 0x20,
    'forcesw': 0x40,
}


def c_atoi(s):
    """C atoi(): leading ws, optional sign, digits, stop at first non-digit."""
    i = 0
    while i < len(s) and s[i] in ' \t\n\v\f\r':
        i += 1
    sign = 1
    if i < len(s) and s[i] in '+-':
        if s[i] == '-':
            sign = -1
        i += 1
    v = 0
    while i < len(s) and s[i].isdigit():
        v = v * 10 + int(s[i])
        i += 1
    return sign * v


def c_atof(s):
    """C atof(), restricted: the tokenizer guarantees no '.' or '-' survives."""
    i = 0
    while i < len(s) and s[i] in ' \t\n\v\f\r':
        i += 1
    j = i
    while j < len(s) and (s[j].isdigit() or s[j] in '+-.eE'):
        j += 1
    try:
        return float(s[i:j]) if j > i else 0.0
    except ValueError:
        return 0.0


def tokenize(text):
    """Replicate the game's strtok(" \\t\\r:/.,-") + ';' comment + '\\n' record split.

    Yields tokens; a token beginning with '\\n' signals a record break, and a
    token beginning with ';' signals "skip to end of line".
    strtok collapses runs of delimiters, so empty tokens never appear.
    """
    i = 0
    n = len(text)
    while i < n:
        # strtok skips leading delimiters
        while i < n and text[i] in DELIMS:
            i += 1
        if i >= n:
            return
        j = i
        while j < n and text[j] not in DELIMS:
            j += 1
        yield text[i:j]
        i = j


def parse_effectdef(path):
    with open(path, 'rb') as f:
        raw = f.read()
    text = raw.decode('cp932')

    records = {}
    col = 0
    cur_id = None
    skip_line = False

    for tok in tokenize(text):
        # a token may itself begin with newlines (\n is not a delimiter)
        while tok and tok[0] == '\n':
            tok = tok[1:]
            col = 0
            skip_line = False
        if not tok:
            continue
        if skip_line:
            # inside a comment: only a newline (handled above) can end it
            if '\n' in tok:
                tail = tok.split('\n', 1)[1]
                skip_line = False
                col = 0
                tok = tail
                if not tok:
                    continue
            else:
                continue
        if tok[0] == ';':
            skip_line = True
            if '\n' in tok:
                skip_line = False
                col = 0
            continue
        # embedded newline inside a token ends the record mid-token
        if '\n' in tok:
            head, rest = tok.split('\n', 1)
            if head:
                _assign(records, cur_id, col, head)
                if col == 0:
                    cur_id = c_atoi(head)
            col = 0
            # the remainder becomes the first column of the next record
            if rest:
                _assign(records, None, 0, rest)
                cur_id = c_atoi(rest)
                col = 1
            continue
        if col == 0:
            cur_id = c_atoi(tok)
        _assign(records, cur_id, col, tok)
        col += 1

    return records


def _assign(records, rid, col, tok):
    if col >= len(EFFECTDEF_COLUMNS):
        return
    name = EFFECTDEF_COLUMNS[col]
    if col == 0:
        rid = c_atoi(tok)
        records.setdefault(rid, {})['id'] = rid
        return
    if rid is None or rid not in records:
        return
    r = records[rid]
    if name == 'filename':
        r['filename'] = tok
    elif name in ('mindist', 'maxdist'):
        r[name] = c_atof(tok)
    else:
        r[name] = c_atoi(tok)


def to_record_words(r):
    """Build the 0xdc record exactly as the game does, returning the key fields."""
    dsbcaps = 0x20090 if r.get('3d', 0) else 0x80
    flags = 0
    for k, bit in FLAG_BITS.items():
        if r.get(k, 0):
            flags |= bit
    return dict(dsbcaps=dsbcaps, dup=r.get('dup', 0), flags=flags,
                id=r['id'], cancel=r.get('cancel', 0),
                name=r.get('filename', ''),
                mindist=r.get('mindist', 0.0), maxdist=r.get('maxdist', 0.0))


def parse_bgmdef(path):
    with open(path, 'rb') as f:
        text = f.read().decode('cp932')
    out = []
    col = 0
    cur_id = None
    skip = False
    for tok in tokenize(text):
        while tok and tok[0] == '\n':
            tok = tok[1:]
            col = 0
            skip = False
        if not tok:
            continue
        if skip:
            if '\n' in tok:
                skip = False
                col = 0
            continue
        if tok[0] == ';':
            skip = True
            if '\n' in tok:
                skip = False
                col = 0
            continue
        parts = tok.split('\n')
        for pi, p in enumerate(parts):
            if pi > 0:
                col = 0
            if not p:
                continue
            if col == 0:
                cur_id = c_atoi(p)
            elif col == 1:
                out.append((p[:15], cur_id))
            col += 1
    return out


def main():
    base = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        'assets', 'WAVE', 'EFFECT')
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(base, 'EFFECTDEF.TXT')
    recs = parse_effectdef(path)
    ids = sorted(recs)
    print(f"{len(recs)} definitions, id range {min(ids)}..{max(ids)}")
    print(f"table alloc = (max_id+1) * 0xdc = {(max(ids)+1)*0xdc} bytes")
    print()
    hdr = ('id', 'filename', '3d', 'loop', 'nega', 'dup', 'ld1', 'man',
           'cancel', 'min', 'max', 'fsw', 'whw')
    print(f"{hdr[0]:>4} {hdr[1]:<26} {hdr[2]:>2} {hdr[3]:>4} {hdr[4]:>4} "
          f"{hdr[5]:>3} {hdr[6]:>3} {hdr[7]:>3} {hdr[8]:>6} {hdr[9]:>5} "
          f"{hdr[10]:>5} {hdr[11]:>3} {hdr[12]:>3}  dsbcaps flags")
    for i in ids:
        r = recs[i]
        w = to_record_words(r)
        print(f"{r['id']:>4} {r.get('filename',''):<26} {r.get('3d',0):>2} "
              f"{r.get('loop',0):>4} {r.get('nega',0):>4} {r.get('dup',0):>3} "
              f"{r.get('load1st',0):>3} {r.get('manual',0):>3} "
              f"{r.get('cancel',0):>6} {r.get('mindist',0):>5.0f} "
              f"{r.get('maxdist',0):>5.0f} {r.get('forcesw',0):>3} "
              f"{r.get('wishhw',0):>3}  0x{w['dsbcaps']:05x} 0x{w['flags']:02x}")


if __name__ == '__main__':
    main()
