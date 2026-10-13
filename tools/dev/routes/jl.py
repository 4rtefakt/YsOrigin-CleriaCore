"""jl.py <room> <start> <target> [mode]: the static way's jumps (a -> b, width, rise, share of Hugo's reach) and a hops string."""
import json, os, sys
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
T = json.load(open(os.path.join(HERE, "table.json"))); R = json.load(open(os.path.join(HERE, "rooms.json")))
room, st, tg = sys.argv[1:4]; mode = sys.argv[4] if len(sys.argv) > 4 else "JJ"
for q in T[room]["modes"][mode]:
    if q["start"] == st and q["target"] == tg:
        print(q["cls"], q.get("ratio"), "on", q.get("on"))
        pts = []
        for j in q.get("jumps", []):
            if j["d"] > 0.6 or abs(j["h"]) > 1.2: print(f"  {j['a']} -> {j['b']} width {j['d']} rise {j['h']} share {j['ratio']} {j['need']}")
            pts += [j["a"], j["b"]]
        e = next((e for e in R[room]["exits"] if "to " + e["lab"] == tg), None)
        if e: pts.append(e["pos"])
        print("hops=" + "/".join(f"{p[0]:.1f},{p[2]:.1f}" for p in pts))
        s = next((e for e in R[room]["exits"] if "from " + e["lab"] == st), None)
        print("start door", s["pos"] if s else None)
