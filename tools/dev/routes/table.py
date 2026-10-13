"""table.py [room regex]: the static route table from rooms.json + rooms/*.mesh (nav.py) -> table.json and a text
listing. Per room: every start (arrival point of a door into it) to every target (its exits, its scripted objects)."""
import json, os, re, sys, pickle
import numpy as np
import nav
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
R = json.load(open(os.path.join(HERE, "rooms.json")))
pat = re.compile(sys.argv[1] if len(sys.argv) > 1 else ".")
NODJ = {"S_4000", "S_4003", "S_4001", "S_4002"}      # walked before the double jump's chest (S_4002)
arr = {}
for r, v in R.items():
    for e in v["exits"]: arr.setdefault(e["dest"].upper(), []).append((r, e["arr"]))
out = {}
def need(d, h):
    """the least kit that makes a jump of width d and rise h, per character Y H T."""
    s = []
    for ch in "123":
        k = "-"
        for lab, dash, dbl in (("J", "0", False), ("JJ", "0", True), ("D+J", "1", False), ("D+JJ", "1", True)):
            if float(nav.reach(ch, dash, dbl, h)) >= d + 0.45: k = lab; break
        s.append(k)
    return "/".join(s)
for r in sorted(R):
    if not pat.search(r) or not R[r]["ok"]: continue
    v = R[r]
    scripted = {o["name"].lower() for o in v["objs"] if o["kind"].startswith("trigger")}
    rm = nav.Room(r, os.path.join(HERE, "rooms", r + ".mesh"), scripted)
    moved = []
    p2 = os.path.join(HERE, "rooms", r + ".mesh2")
    if os.path.exists(p2):
        o2, T2, _ = nav.load(p2); o1, T1, _ = nav.load(os.path.join(HERE, "rooms", r + ".mesh"))
        if len(T1) == len(T2):
            for (n, t0, t1) in o1:
                if t1 > t0 and np.abs(T1[t0:t1, 2:] - T2[t0:t1, 2:]).max() > 0.05: moved.append(n)
        else: moved.append("(triangle count changed)")
    starts = {}
    def twin(e):
        d = R.get(e["dest"])
        if not d: return None
        k = int(e["lab"].split("/")[1]) if "/" in e["lab"] else 0
        tw = [x for x in d["exits"] if x["dest"] == r]
        return tw[k] if k < len(tw) else None
    for e in v["exits"]:
        tw = twin(e)
        back = tw["arr"] if tw and any(abs(c) > 0.01 for c in tw["arr"]) else None
        starts["from " + e["lab"]] = rm.node_at(back, 5.0, 4.0) if back else rm.door_node(e["pos"])
    for src, a in arr.get(r.upper(), []):
        if not any(e["dest"] == src for e in v["exits"]) and any(abs(c) > 0.01 for c in a):
            n = rm.node_at(a, 5.0, 4.0)
            if n is None:                                            # an arrival in the air: the floor under it
                below = np.nonzero((~rm.blocked) & (rm.y <= a[1]) & (np.hypot(rm.X - a[0], rm.Z - a[2]) <= 0.8))[0]
                if len(below): n = int(below[np.argmax(rm.y[below])])
            starts["from " + src] = n
    if v["spawn"]: starts.setdefault("spawn", rm.node_at(v["spawn"]))
    targets = {}
    for e in v["exits"]: targets["to " + e["lab"]] = rm.door_node(e["pos"])
    for o in v["objs"]:
        if re.match(r"(box|switch|save|s40switch|step|jump|medaru|mirror)", o["name"].lower()) or o["kind"] == "action":
            targets.setdefault(o["name"], rm.node_at(o["pos"], 3.5, 4.0))
    modes = {}
    for mode, dbl, dash in (("J", False, "0"), ("JJ", True, "0"), ("DJ", False, "1"), ("DJJ", True, "1")):
        E = rm.jump_edges(dbl, dash)
        rows = []
        for sl, sn in starts.items():
            if sn is None: rows.append(dict(start=sl, target="*", cls="start not on a floor")); continue
            best = rm.search(sn, E)
            for tl, tn in targets.items():
                if tl[3:] == sl[5:] and tl.startswith("to "): continue
                if tn is None: rows.append(dict(start=sl, target=tl, cls="target not on a floor")); continue
                c = int(rm.comp[tn])
                if c not in best: rows.append(dict(start=sl, target=tl, cls="NO WAY")); continue
                w, path = best[c]
                if not path: rows.append(dict(start=sl, target=tl, cls="walk", ratio=0.0, sn=sn, tn=tn, jumps=[])); continue
                jumps = []
                for key in path:
                    ratio, i, j, d, h = E[key]
                    jumps.append(dict(ratio=round(ratio, 3), a=[round(float(x), 2) for x in (rm.X[i], rm.y[i], rm.Z[i])],
                                      b=[round(float(x), 2) for x in (rm.X[j], rm.y[j], rm.Z[j])], d=round(d, 2), h=round(h, 2), need=need(d, h), i=i, j=j))
                worst = max(jumps, key=lambda q: q["ratio"])
                comps = {int(rm.comp[sn])} | {b for a, b in path}
                objs_on = sorted({rm.objs[k][0] for k in set(rm.obj[np.isin(rm.comp, list(comps))].tolist()) if k in rm.scripted or (k >= 0 and rm.objs[k][0] in moved)})
                rows.append(dict(start=sl, target=tl, cls="jump", ratio=worst["ratio"], worst=worst, jumps=jumps, on=objs_on, sn=sn, tn=tn))
        modes[mode] = rows
    rows = modes["JJ"]; dbl = True
    surf = sorted({rm.surf.get(int(s), "?") for s in set(rm.sf.tolist())})
    out[r] = dict(rows=rows, modes=modes, moved=moved, opened=rm.opened, surfaces=surf, double=dbl,
                  hazards=sorted({o["script"] for o in v["objs"] if re.search("toge|yari|fall|jump", o["script"].lower())}))
    pickle.dump(rm, open(os.path.join(HERE, "rooms", r + ".nav"), "wb"))
    print(f"== {r} ({'double' if dbl else 'SINGLE'} jump) moved {moved} open {rm.opened} surfaces {surf} hazards {out[r]['hazards']}")
    walk = [q for q in rows if q.get("cls") == "walk"]
    print(f"   walk only: {len(walk)} pairs")
    for q in rows:
        if q["cls"] == "walk": continue
        if q["cls"] != "jump": print(f"   {q['start']} -> {q['target']}: {q['cls']}"); continue
        w = q["worst"]
        print(f"   {q['start']} -> {q['target']}: {len(q['jumps'])} jumps, worst {w['ratio']:.2f} gap {w['d']} rise {w['h']} at {w['a']} -> {w['b']} need {w['need']}" + (f" on {q['on']}" if q["on"] else ""))
    sys.stdout.flush()
json.dump(out, open(os.path.join(HERE, "table.json"), "w"), indent=1)
