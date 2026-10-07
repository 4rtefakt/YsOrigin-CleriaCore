# Run the retail weather-emitter updates (FUN_0058e7c0 = 0x125 FireDrop's GoXx type 0x12,
# FUN_0058daa0 = 0x122 FrameSmoke's GoXx type 0x10) on the real exe bytes, tick by tick, and
# print what they do: every shared-xorshift draw (state after each tick), every child descriptor
# handed to FUN_00583d90, the emitter's own fields. The output is the test oracle for
# src/cleria/weather.cpp (apps/cleria-weathertest).
import sys, json
from x86emu import Emu, f32, bits32

THIS = 0x10000000
MAPARR, MAPOBJ = 0x10100000, 0x10200000
CHILD0 = 0x11000000

def make(kind, cap, rng, eye, at, noise, alloc_fail_at=()):
    """A fresh machine: the globals the bodies read, an emitter at THIS, stubs for the pool alloc
    (FUN_0040f700, `ret 4`; NULL on the alloc numbers in alloc_fail_at), the GoXx ctor FUN_00583a00
    and the child init FUN_00583d90 (`ret 4`, records its 27-dword descriptor)."""
    e = Emu()
    e.r["esp"] = 0x00200000
    e.wd(e.fs0, 0)
    for a, v in ((0x74ab8f, 0), (0x74c0f0, 1)): e.wd(a, v, 1)
    e.wd(0x730224, MAPARR); e.wd(MAPARR, MAPOBJ); e.wd(MAPOBJ + 0x268, 0x1234)
    e.wd(0x7b38f0, 0)
    for k in range(3): e.setf(0x7b33f4 + 4 * k, eye[k]); e.setf(0x7b3400 + 4 * k, at[k])
    for k in range(4): e.wd(0x74622c + 4 * k, rng[k])
    e.wd(0x76bda4, 1 if noise else 0)
    e.wd(0x749dc4, 0xAAAA0030); e.wd(0x749dc8, 0xAAAA0031)
    e.wd(0x74a5e0, 0xBBBB0022); e.wd(0x749e3c, 0xBBBB003C)
    for k in range(0, 0x230, 4): e.wd(THIS + k, 0)
    e.wd(THIS + 0x12c, cap)
    st = {"next": CHILD0, "allocs": 0, "emits": [], "fail": set(alloc_fail_at)}
    def alloc(em):
        st["allocs"] += 1
        if st["allocs"] in st["fail"]:
            em.r["eax"] = 0
        else:
            em.r["eax"] = st["next"]
            for k in range(0, 0x230, 4): em.wd(st["next"] + k, 0)
            st["next"] += 0x1000
        # ret 4: the callee pops the return address (done by our call model) and one arg
        return 4
    def ctor(em):
        em.r["eax"] = em.r["ecx"]; return 0
    def emit(em):
        desc = em.rd(em.r["esp"])
        st["emits"].append({"child": em.r["ecx"], "d": [em.rd(desc + 4 * k) for k in range(27)]})
        return 4
    e.calls = {0x40f700: alloc, 0x583a00: ctor, 0x583d90: emit}
    return e, st

def rng_state(e): return [e.rd(0x74622c + 4 * k) for k in range(4)]

def run(kind, cap, ticks, rng, eye, at, noise, fail=()):
    e, st = make(kind, cap, rng, eye, at, noise, fail)
    fn = 0x58e7c0 if kind == "ember" else 0x58daa0
    out = []
    for t in range(ticks):
        st["emits"].clear()
        e.r["ecx"] = THIS
        e.run(fn)   # a stub's return value = the bytes its `ret n` pops (x86emu's CALL model)
        rec = {"t": t + 1, "rng": rng_state(e), "dead": bool(e.rd(THIS + 0x11, 1) & 2),
               "emits": [dict(x) for x in st["emits"]]}
        if kind == "smoke":
            rec["off"] = [e.rd(THIS + 0x140 + 4 * k) for k in range(3)]
            rec["f138"] = e.rd(THIS + 0x138)
            c = e.rd(THIS + 0x13c)
            rec["child_pos"] = [e.rd(c + 0x50 + 4 * k) for k in range(3)] if c else None
        out.append(rec)
    return out

if __name__ == "__main__":
    kind = sys.argv[1]; cap = int(sys.argv[2]); ticks = int(sys.argv[3]); noise = int(sys.argv[4])
    rng = [123456789, 362436069, 521288629, 916191069]
    eye = (10.5, 20.25, -30.75); at = (2.0, 1.5, -4.0)
    res = run(kind, cap, ticks, rng, eye, at, noise)
    json.dump(res, sys.stdout)
