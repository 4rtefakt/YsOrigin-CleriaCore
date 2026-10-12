"""dl.py <tagA> <tagB> <case> [maxlines]: a unified diff of one case's two run2.py logs (first lines), with the count."""
import difflib, os, sys, tempfile
SCRATCH = os.environ.get("CLERIA_DEV_SCRATCH", tempfile.gettempdir())
a, b, c = sys.argv[1:4]
n = int(sys.argv[4]) if len(sys.argv) > 4 else 24
A = open(os.path.join(SCRATCH, "all_" + a, c + ".log"), encoding="utf-8", errors="replace").read().splitlines()
B = open(os.path.join(SCRATCH, "all_" + b, c + ".log"), encoding="utf-8", errors="replace").read().splitlines()
d = [l for l in difflib.unified_diff(A, B, lineterm="", n=0) if not l.startswith(("---", "+++"))]
print(f"== {c}: {sum(l[0] in '+-' for l in d)} changed lines of {len(A)}")
for l in d[:n]: print("  ", l[:190])
