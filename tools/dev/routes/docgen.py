"""docgen.py: the per-room table of the route docs from table.json + route.json (one row a room: the pairs the search
used, the hardest gate among them, its share of Hugo's and Yunica's reach)."""
import json, os, sys
import nav
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
T = json.load(open(os.path.join(HERE, "table.json"))); P = json.load(open(os.path.join(HERE, "route.json")))
WALK = {}
exec(open(os.path.join(HERE, "walked.py")).read())
used = {}
for k, (p, q, mode) in P.items():
    room, lab = k.split("|", 1)
    if p[0] != room: continue
    used.setdefault(room, []).append((p[1], lab, q, mode))
print("| Room | Pairs the route uses (from -> to) | Hardest gate on them [approx +-0.5]: width, rise, where | Share of reach H / Y | On the way | Walked with real inputs |")
print("|---|---|---|---|---|---|")
for room in sorted(T):
    u = used.get(room, [])
    if not u: continue
    jumps = [(q["ratio"], a, b, q, mode) for a, b, q, mode in u if q and q.get("cls") == "jump"]
    over = [(a, b, q["why"]) for a, b, q, mode in u if q and q.get("cls") == "override"]
    pairs = ", ".join(f"{a} -> {b}" for a, b, q, mode in u)
    if len(pairs) > 110: pairs = pairs[:107] + ".."
    if jumps:
        r, a, b, q, mode = max(jumps, key=lambda x: x[0])
        w = q["worst"]; dbl = "JJ" in mode
        ry = float(nav.reach("1", "0", dbl, w["h"])); rh = float(nav.reach("2", "0", dbl, w["h"]))
        tall = max((j["h"] for _, _, _, qq, _ in jumps for j in qq["jumps"]), default=0)
        gate = f"{w['d']:.1f} wide, rise {w['h']:+.1f} at ({w['a'][0]:.0f}, {w['a'][1]:.0f}, {w['a'][2]:.0f}), {a} -> {b}" + (f"; tallest rise {tall:.1f}" if tall >= 2.0 and tall > w["h"] else "")
        top = 5.37 if dbl else 2.63
        share = f"{max(w['d'] / rh, max(w['h'], 0) / top):.2f} / {max(w['d'] / ry, max(w['h'], 0) / top):.2f}" + ("" if dbl else " (single jump)")
    else: gate, share = "none: walks and drops", "—"
    on = sorted({o for _, _, q, _ in u if q for o in q.get("on", [])})
    extra = "; ".join(f"{a} -> {b}: {why}" for a, b, why in over)
    info = T[room]
    notes = ", ".join(on[:6]) + (".." if len(on) > 6 else "")
    hz = ", ".join(h for h in info["hazards"])
    sf = ", ".join(s for s in info["surfaces"] if s in ("sand", "sand2", "blood", "ice", "water"))
    way = "; ".join(x for x in (notes, ("hazards: " + hz) if hz else "", ("surfaces: " + sf) if sf else "", ("not in the static pass: " + extra) if extra else "") if x) or "—"
    print(f"| {room} | {pairs} | {gate} | {share} | {way} | {WALK.get(room, 'no')} |")
