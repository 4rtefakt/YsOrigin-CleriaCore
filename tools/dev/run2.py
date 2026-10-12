"""run2.py <tag> <cleria-view.exe> [case...] | --cmp <tagA> <tagB>: every drive case (or the named ones) through
drive_regress's own runner, each full log kept as <scratch>/all_<tag>/<case>.log (temp folders and ports blanked);
--cmp lists the cases whose logs differ. For "the story game is unaffected": run a tag on the old head's exe (copy
the exe folder first), one on the new, compare. Scratch = CLERIA_DEV_SCRATCH (default: the system temp folder);
assets = CLERIA_ASSETS; jobs = JOBS, else 2 while a cleria-view.exe under build\\Release (a play build) runs, else 4."""
import concurrent.futures, os, re, subprocess, sys, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRATCH = os.environ.get("CLERIA_DEV_SCRATCH", tempfile.gettempdir())
sys.path.insert(0, os.path.join(ROOT, "tools"))
import drive_regress as dr

def norm(s):
    s = re.sub(r"[A-Za-z]:[\\/][^\s'\"]*?[\\/]tmp[a-z0-9_]{6,}", "<TMP>", s)
    return re.sub(r"(127\.0\.0\.1|localhost):\d+", r"\1:<PORT>", s)

def jobs():
    if os.environ.get("JOBS"): return int(os.environ["JOBS"])
    r = subprocess.run(["powershell", "-NoProfile", "-Command",
                        "(Get-CimInstance Win32_Process -Filter \"name='cleria-view.exe'\" | Where-Object "
                        "{ $_.ExecutablePath -like '*CleriaCore\\build\\Release*' } | Measure-Object).Count"],
                       capture_output=True, text=True)
    return 2 if r.stdout.strip() not in ("", "0") else 4

if sys.argv[1] == "--cmp":
    a, b = (os.path.join(SCRATCH, "all_" + t) for t in sys.argv[2:4])
    names = sorted(set(os.listdir(a)) | set(os.listdir(b)))
    diff = []
    for n in names:
        pa, pb = os.path.join(a, n), os.path.join(b, n)
        if not (os.path.exists(pa) and os.path.exists(pb)): diff.append(n + " (missing)")
        elif open(pa, "rb").read() != open(pb, "rb").read(): diff.append(n)
    print(f"{len(names) - len(diff)}/{len(names)} identical")
    for n in diff: print("  DIFF", n)
    sys.exit(1 if diff else 0)

tag, exe = sys.argv[1], os.path.abspath(sys.argv[2])
names = sys.argv[3:] or list(dr.CASES)
assets = os.environ.get("CLERIA_ASSETS", dr.ASSETS)
out_dir = os.path.join(SCRATCH, "all_" + tag)
os.makedirs(out_dir, exist_ok=True)
os.environ["CLERIA_MUTE"] = "1"
orig = dr.check
def check(name, case, r, out, say):
    open(os.path.join(out_dir, name + ".log"), "w", encoding="utf-8", errors="replace").write(norm(out))
    return orig(name, case, r, out, say)
dr.check = check
t0 = time.time(); bad = []; j = jobs()
with concurrent.futures.ThreadPoolExecutor(j) as pool:
    order = sorted(names, key=lambda n: n not in dr.LONG)
    futs = {pool.submit(dr.run, n, dr.CASES[n], exe, assets): n for n in order}
    for f in concurrent.futures.as_completed(futs):
        n = futs[f]
        try: ok, msg, secs = f.result()
        except Exception as e: ok, msg = False, [f"FAIL {n}: {e}"]
        if not ok: bad.append(n); print("\n".join(msg), flush=True)
print(f"{tag}: {len(names) - len(bad)}/{len(names)} passed ({time.time() - t0:.0f}s, jobs {j})")
sys.exit(1 if bad else 0)
