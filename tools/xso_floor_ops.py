#!/usr/bin/env python3
"""xso_floor_ops.py - the XSO sub-op census for floors 1-3 and its implementation status.

Collects every event script of the floor-1..3 map groups (MAP/S_10, S_11, S_20, S_21, S_30,
S_31 — the S_x1 groups are the same floors' side wings, as the FLOOR*_ROUTE_AUDIT.md route
maps treat them) plus the MAP/S_COMMON scripts they reach ('@name' string operands in the
scripts and the rooms' .SOB files, and the engine-started ones CleriaCore's source names),
decodes every class-2 call with the validated linear walk (tools/xso_op_probe.py), and
counts call sites per sub-op.

The status of each sub-op comes from two sources:
  * the VM: `cleria-xsoops` runs every sub-op through XsoVm::step_one on a synthetic
    instruction and reports whether it reached the "unhandled" fallthrough (src/cleria/xso.cpp);
  * the host: for an op the VM forwards to an XsoHost method, whether the live host
    (CutsceneHost in apps/cleria-view/view_game.inl, or ActorWorld in src/cleria/world.*
    when CutsceneHost forwards to `worldHost`) overrides that method.
NOTES below adds the hand-checked [stub]/[partial] judgements with their evidence.

    python tools/xso_floor_ops.py --assets <root> [--xso build_diff/Release/cleria-xsoops.exe]
                                  [--md out.md] [--json out.json]
"""
import argparse, json, os, re, struct, subprocess, sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GROUPS = ["S_10", "S_11", "S_20", "S_21", "S_30", "S_31"]
CORE = {"S_10", "S_20", "S_30"}
# S_COMMON scripts the ENGINE/host starts by name (pause-book items, touch/fall hooks,
# statue menus): grep '"@' in src/ + apps/.
ENGINE_STARTED = ["CheckWing", "CheckWing2", "DarkRiranokaigara", "Flag_Inv", "GrowAr", "GrowMenu",
                  "Riranokaigara", "Save", "SaveBack", "SaveMenu", "UseHammer", "UseHarmonica",
                  "UseHarmonica2", "UseItemBoo", "UseSekizou", "UseWing", "Jump_001", "Fall_001",
                  "Touch_Toge"]


def parse(path):
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 0x24 or d[:4] != b'XSR\0':
        return None
    n = struct.unpack_from('<I', d, 0x1c)[0]
    if 0x24 + n * 4 > len(d):
        return None
    code = list(struct.unpack_from('<%di' % n, d, 0x24))
    scount = struct.unpack_from('<I', d, 0x20)[0]
    strs = []
    ce = 0x24 + n * 4
    try:
        offs = struct.unpack_from('<%dI' % scount, d, ce)
        base = ce + scount * 4
        for o in offs:
            e = d.index(b'\0', base + o)
            strs.append(d[base + o:e].decode('latin-1'))
    except Exception:
        strs = []
    return code, strs


def walk(code):
    pc, n = 0, len(code)
    while pc < n:
        w = code[pc] & 0xffffffff
        cls = w >> 24
        if cls == 2:
            skip = (w >> 8) & 0xF
            cnt = w & 0xFF
            yield pc, (w >> 12) & 0xFFF, code[pc + 1 + skip: pc + 1 + skip + cnt]
            pc += 1 + skip + cnt
        elif cls in (0, 1, 0xC, 0xD):
            pc += 1
        elif cls <= 0x13:
            pc += 2
        else:
            return


def funclist(assets):
    p = os.path.join(assets, "MISC", "FUNCLIST.DEF")
    names = {}
    if os.path.exists(p):
        for i, line in enumerate(open(p, encoding='latin-1').read().split('\n')):
            line = line.strip()
            if '=' in line:
                k, v = line.split('=', 1)
                names[i] = (k, v)
    return names


def collect(assets):
    mp = os.path.join(assets, "MAP")
    files = {}                                   # path -> (group, room)
    for g in GROUPS:
        for dp, _, fns in os.walk(os.path.join(mp, g)):
            for fn in fns:
                p = os.path.join(dp, fn)
                if fn.upper().endswith('.XSO'):
                    files[p] = (g, os.path.basename(dp))
    common = os.path.join(mp, "S_COMMON")
    want = set(n.upper() for n in ENGINE_STARTED)
    # '@' references in the rooms' SOB/SEV sidecars (map-object scripts)
    for g in GROUPS:
        for dp, _, fns in os.walk(os.path.join(mp, g)):
            for fn in fns:
                if fn.upper().endswith(('.SOB', '.SEV', '.SCT')):
                    d = open(os.path.join(dp, fn), 'rb').read()
                    for m in re.finditer(rb'@([A-Za-z0-9_]+)', d):
                        want.add(m.group(1).decode().upper())
    todo = list(files)
    seen_common = set()
    while True:
        for p in todo:
            r = parse(p)
            if not r:
                continue
            for s in r[1]:
                if s.startswith('@'):
                    want.add(s[1:].upper())
        todo = []
        for nm in sorted(want - seen_common):
            seen_common.add(nm)
            p = os.path.join(common, nm + ".XSO")
            if os.path.exists(p) and p not in files:
                files[p] = ("S_COMMON", "S_COMMON")
                todo.append(p)
        if not todo:
            break
    return files


def vm_status(xso_exe):
    """sub -> 'vm' | 'unhandled' from `cleria-xsoops`."""
    out = subprocess.run([xso_exe], capture_output=True, text=True).stdout
    st = {}
    for line in out.splitlines():
        m = re.match(r'\s*0x([0-9a-f]+)\s+(\w+)', line)
        if m:
            st[int(m.group(1), 16)] = m.group(2)
    return st


def host_methods():
    """sub -> [XsoHost methods the VM calls for it], from src/cleria/xso.cpp."""
    src = open(os.path.join(ROOT, "src", "cleria", "xso.cpp"), encoding='utf-8').read()
    body = src[src.index("XsoVm::Step XsoVm::step_one"):src.index("int XsoVm::run(")]
    body += src[src.index("bool XsoVm::eval_wait"):src.index("// ---- ScriptManager")]
    out = defaultdict(set)
    # split at every `sub == 0xNN` / `case 0xNN:` and attribute host-> calls to the subs
    # named in the same condition
    parts = re.split(r'(?=\b(?:if\s*\(|case 0x[0-9a-fA-F]+:))', body)
    last = []
    for part in parts:
        subs = [int(x, 16) for x in re.findall(r'sub == 0x([0-9a-fA-F]+)', part.split('{')[0])]
        subs += [int(x, 16) for x in re.findall(r'^case 0x([0-9a-fA-F]+):', part)]
        if subs:
            last = subs
        else:
            subs = last                      # a nested `if` inside the op's block
        calls = re.findall(r'host->(\w+)\(', part)
        for s in subs:
            for c in calls:
                out[s].add(c)
    return out


def overridden():
    """Methods the live hosts override: CutsceneHost (view_game.inl) and ActorWorld."""
    view = open(os.path.join(ROOT, "apps", "cleria-view", "view_game.inl"), encoding='utf-8').read()
    a = view.index("struct CutsceneHost : XsoHost")
    b = view.index("} csHost;", a)
    decl = r'(\w+)\([^;{()]*\)\s*(?:const\s*)?override'
    cs = set(re.findall(decl, view[a:b]))
    fwd = set(re.findall(decl + r'\s*\{[^}]*worldHost', view[a:b]))
    wtxt = ""
    for f in ("world.hpp", "world.cpp"):
        p = os.path.join(ROOT, "src", "cleria", f)
        if os.path.exists(p):
            wtxt += open(p, encoding='utf-8').read()
    world = set(re.findall(r'\b(\w+)\s*\(', wtxt))      # declared/defined by ActorWorld
    return cs, fwd, world


# Hand-checked judgements: sub -> (status, note). status: 'stub' (accepted and dropped, or a
# placeholder), 'partial' (modelled with a stated gap), or the derived status with a note.
# Evidence for each is in build/event_re/FLOOR13_OPS.md.
NOTES = {
    0x000: ("unhandled", "Steam achievements: the handler strcmp's GiveFruitRoo1..6 / GetBlessing01.. and calls "
                         "FUN_00405e50 [not modelled: no Steam layer]"),
    0x082: ("unhandled", "only @Flag_Inv (the debug inventory script) on these floors [not modelled]"),
    0x089: ("unhandled", "only @Flag_Inv (the debug inventory script) on these floors [not modelled]"),
    0x09b: ("partial", "inert by design: load_room reads the room's own .SOB; [not modelled] S_3099's "
                       "Hard/Nightmare pick of S_3099_1.SOB"),
    0x09c: ("implemented", "inert by design: load_room reads the room's .SEF"),
    0x09d: ("implemented", "inert by design: load_room reads the room's .SFO"),
    0x09e: ("implemented", "inert by design: load_room reads the room's .SCM"),
    0x09f: ("implemented", "inert by design: load_room reads the room's .SEN/.SEV"),
    0x0a0: ("implemented", "populate_room runs the root with ActorWorld (spawn_placements); the live re-run "
                           "is a no-op by design"),
    0x0a1: ("implemented", "inert by design: load_room reads the room's .SNF"),
    0x0a2: ("unhandled", "FUN_004348a0 near/far clip (1.0 / 4000-7000) in the S_x1 rooms [not modelled]"),
    0x0ac: ("implemented", "preload: SEs load on first use"),
    0x0ad: ("implemented", "preload: casts load on first use"),
    0x0ae: ("implemented", "preload: portraits load on first use"),
    0x0be: ("partial", "[approx] placed once at the actor, the engine's voice follows it (FUN_0045d7a0)"),
    0x0ce: ("unhandled", "post-fx globals 0x76a550/0x76a554 [not modelled: no zoom-blur pass]"),
    0x10a: ("partial", "the byte 0x76c1c5 is kept [not modelled: no glare/bloom pass]"),
    0x111: ("unhandled", "FUN_00571500 -> FUN_00593550 (EVENTVM_HANDLERS_4 §1); only after the S_3080 Shion duel "
                         "(M840 not ported) [not modelled]"),
    0x11d: ("unhandled", "FUN_00432e60 reflection-plane height [not modelled: no planar water reflection]"),
    0x122: ("implemented", "GoXx type 0x10 smoke emitter FUN_0058da80 / update FUN_0058daa0 (src/cleria/weather, "
                           "WEATHER.md): its shared-xorshift draws and puffs"),
    0x123: ("partial", "0x76a558..0x76a560 stepped as FUN_00427c90 does (weather::Blur); [not modelled] the "
                       "distortion pass that reads 0x7b249c"),
    0x125: ("implemented", "GoXx type 0x12 ember emitter FUN_0058e790 / update FUN_0058e7c0 (src/cleria/weather, "
                           "WEATHER.md): 10 shared-xorshift draws every 16 ticks, the sparkle sprites"),
    0x13d: ("implemented", "the player (13) only, which is every corpus site"),
    0x15c: ("partial", "the player only (every floor 1..3 site is 998 on a player-bound touch script); "
                       "SOB trigger-4 touch volumes start via a [STAND-IN] side-contact sphere"),
    0x15e: ("partial", "the player only; world actors are not pushed"),
    0x15f: ("partial", "the player only; world actors are not pushed"),
    0x163: ("implemented", "+0x130 bit 0x10000 (FUN_00571d60); the player pushes 0x15e / 0x15f honour it"),
    0x164: ("partial", "+0x130 bit 0x80000 (FUN_00571db0): the mover FUN_0045f4d0 keeps the current motion; "
                       "honoured by the view's cutscene (frozen) locomotion pick only"),
    0x167: ("unhandled", "+0x97c = op1 (FUN_00571ec0); reader not traced [not modelled]"),
    0x179: ("implemented", "FUN_005730b0: the placement rotation +0x3dc re-baked (meshobj.cpp; floor 4 2026-10-08)"),
    0x17c: ("partial", "[approx] spawned once at the mesh position, not attached; frame names not resolved"),
    0x8b: ("partial", "kinds 0/2/7/8/10/12 modelled; [not modelled] 11 and part of 12"),
}


def handler_map():
    """sub -> handler address in the event VM's jump table (build/xso_ops/op_handler_map.txt)."""
    p = os.path.join(ROOT, "build", "xso_ops", "op_handler_map.txt")
    out = {}
    if os.path.exists(p):
        for line in open(p, encoding='latin-1'):
            m = re.match(r'sub ([0-9a-f]{3}) -> ([0-9a-f]{8})', line)
            if m and int(m.group(1), 16) not in out:
                out[int(m.group(1), 16)] = "0x" + m.group(2).lstrip('0')
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--assets', default=os.path.join(ROOT, 'assets'))
    ap.add_argument('--xso', default=os.path.join(ROOT, 'build_diff', 'Release', 'cleria-xsoops.exe'))
    ap.add_argument('--md', default=None)
    ap.add_argument('--json', default=None)
    ap.add_argument('--vm-status-file', default=None)
    a = ap.parse_args()

    names = funclist(a.assets)
    files = collect(a.assets)
    sites = Counter()
    core_sites = Counter()
    rooms = defaultdict(set)
    scripts = defaultdict(Counter)
    for p, (g, room) in sorted(files.items()):
        r = parse(p)
        if not r:
            continue
        for pc, sub, ops in walk(r[0]):
            sites[sub] += 1
            if g in CORE:
                core_sites[sub] += 1
            rooms[sub].add(room)
            scripts[sub][os.path.basename(p)] += 1

    if a.vm_status_file:                             # a saved `cleria-xsoops` (e.g. the old VM)
        vst = {}
        for line in open(a.vm_status_file):
            m = re.match(r'\s*0x([0-9a-f]+)\s+(\w+)', line)
            if m:
                vst[int(m.group(1), 16)] = m.group(2)
    else:
        vst = vm_status(a.xso)
    hmap = handler_map()
    hm = host_methods()
    cs, fwd, world = overridden()

    rows = []
    for sub in sorted(sites):
        nm, sig = names.get(sub, ("?", ""))
        v = vst.get(sub, "unknown")
        meths = sorted(hm.get(sub, ()))
        if v == "unhandled":
            status = "unhandled"
        else:
            status = "implemented"
            dead = [m for m in meths if m not in cs or (m in fwd and m not in world)]
            if meths and len(dead) == len(meths):
                status = "stub"                      # forwarded to a host method nobody overrides
        note = ""
        if sub in NOTES:
            st, note = NOTES[sub]
            if status != "unhandled":                # a VM fallthrough always stays unhandled
                status = st
        rows.append(dict(sub=sub, name=nm, sig=sig, sites=sites[sub], core=core_sites[sub],
                         status=status, host=meths, handler=hmap.get(sub, ""), rooms=sorted(rooms[sub]),
                         scripts=[s for s, _ in scripts[sub].most_common(4)], note=note))

    tot = Counter()
    for r in rows:
        tot[r['status'] + '_ops'] += 1
        tot[r['status'] + '_sites'] += r['sites']
    print("scripts: %d (%d S_COMMON)" % (len(files), sum(1 for v in files.values() if v[0] == "S_COMMON")))
    print("distinct sub-ops: %d, call sites: %d" % (len(rows), sum(sites.values())))
    for k in ("implemented", "partial", "stub", "unhandled"):
        print("  %-12s %3d ops %6d sites" % (k, tot[k + '_ops'], tot[k + '_sites']))
    for r in rows:
        if r['status'] != 'implemented':
            print("  0x%03x %-28s %5d  %-10s %s" % (r['sub'], r['name'], r['sites'], r['status'],
                                                     ",".join(r['scripts'][:3])))
    if a.json:
        json.dump(dict(rows=rows, totals=tot, scripts=len(files)), open(a.json, 'w'), indent=1)
    if a.md:
        with open(a.md, 'w', encoding='utf-8', newline='\n') as f:
            f.write("| op | name(sig) | sites: all (S_10/20/30) | status | retail handler | rooms | scripts (top) "
                    "| host method / note |\n")
            f.write("|---|---|---|---|---|---|---|---|\n")
            for r in rows:
                rm = r['rooms']
                rms = ", ".join(rm[:5]) + (" +%d" % (len(rm) - 5) if len(rm) > 5 else "")
                f.write("| `0x%03x` | %s(%s) | %d (%d) | %s | `%s` | %s | %s | %s |\n" % (
                    r['sub'], r['name'], r['sig'], r['sites'], r['core'], r['status'], r['handler'], rms,
                    ", ".join(r['scripts'][:3]), (" ".join(r['host']) + " " + r['note']).strip()))


if __name__ == '__main__':
    main()
