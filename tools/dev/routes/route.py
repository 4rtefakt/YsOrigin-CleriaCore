"""route.py: the global search over table.json. Nodes (room, door or object); a door joins its twin in the next room.
Phase 1: the single jump (Hugo, no dash) from S_4000's S_3097 door; when S_4002's box_02 is reached, phase 2: the double
jump from everything reached. Prints what each phase reaches, the pairs used (first reach) and what is never reached."""
import json, os, sys
from collections import deque
HERE = os.environ.get("CLERIA_ROUTE_DIR") or os.path.dirname(os.path.abspath(__file__))   # the data folder (rooms/, *.json)
T = json.load(open(os.path.join(HERE, "table.json"))); R = json.load(open(os.path.join(HERE, "rooms.json")))
def rows(room, mode):
    d = {}
    for q in T[room]["modes"][mode]:
        if q["cls"] == "walk" or (q["cls"] == "jump" and q["ratio"] <= 1.0): d.setdefault(q["start"][5:] if q["start"].startswith("from ") else q["start"], []).append(q)
    return d
OVER = {}
exec(open(os.path.join(HERE, 'over.py')).read())
def closure(seed, mode, parent):
    seen = set(seed); dq = deque(seed)
    while dq:
        room, lab = dq.popleft()
        if room not in T: continue
        nxt = []
        for q in rows(room, mode).get(lab, []):
            t = q["target"]; t = t[3:] if t.startswith("to ") else t
            nxt.append(((room, t), q))
        for (r_, a_, b_), why in OVER.items():
            if r_ == room and a_ == lab: nxt.append(((b_ if isinstance(b_, tuple) else (room, b_)), dict(cls="override", why=why)))
        d, _, k = lab.partition("/")
        if d in T and d in R:
            tw = [x for x in R[d]["exits"] if x["dest"] == room]
            k = int(k or 0)
            if k < len(tw): nxt.append(((d, tw[k]["lab"]), None))        # through the door, to its twin
            elif not tw: nxt.append(((d, room), None))                     # one-way: the arrival point "from <room>"
        for n, q in nxt:
            if n not in seen: seen.add(n); parent[n] = ((room, lab), q, mode); dq.append(n)
    return seen
parent = {}
s1 = closure([("S_4000", "S_3097")], "J", parent)
print("phase 1 (single jump):", len(s1), "nodes; rooms", sorted({r for r, l in s1}))
print("  the double jump (S_4002 box_01) reached:", ("S_4002", "box_01") in s1)
s2 = closure(sorted(s1), "JJ", parent)
print("phase 2 (double jump):", len(s2), "nodes")
s3 = closure(sorted(s2), "DJJ", parent)
print("phase 3 (dash + double jump) adds:", sorted(s3 - s2))
allt = set()
for room in T:
    for q in T[room]["modes"]["JJ"]:
        if q["target"] != "*": allt.add((room, q["target"][3:] if q["target"].startswith("to ") else q["target"]))
    for e in R[room]["exits"]: allt.add((room, e["lab"]))
rr = sorted({r for r, l in s3})
print("rooms reached:", " ".join(rr))
print("rooms not reached:", " ".join(sorted(set(T) - set(rr))))
print("in reached rooms, never reached:")
for room in rr:
    miss = sorted(l for r, l in allt - s3 if r == room)
    if miss: print("  ", room, "reached", sorted(l for r, l in s3 if r == room), "MISSING", miss)
json.dump({"|".join(k): [list(v[0]), v[1], v[2]] for k, v in parent.items()}, open(os.path.join(HERE, "route.json"), "w"))
used = {}
for n, (p, q, mode) in parent.items():
    if q is not None and q["cls"] == "jump": used.setdefault(n[0], []).append((mode, p[1], n[1], q["ratio"], q["worst"]["d"], q["worst"]["h"], q["worst"]["a"], q["worst"]["b"], len(q["jumps"]), q.get("on", [])))
print("pairs used with a jump, worst gate >= 0.6 of the reach:")
for room in sorted(used):
    for u in sorted(used[room], key=lambda u: -u[3]):
        if u[3] >= 0.6: print(f"  {room} [{u[0]}] {u[1]} -> {u[2]}: {u[3]:.2f} gap {u[4]} rise {u[5]} at {u[6]} -> {u[7]} ({u[8]} jumps) on {u[9]}")
