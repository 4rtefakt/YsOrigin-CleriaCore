"""apply.py <file>=<edits.py> ...: exact text replacements in repo files (paths relative to the repo), keeping each
file's line endings (sed -i strips CRLF). edits.py defines EDITS = [(old, new), ...] with \\n newlines; each `old`
must occur exactly once."""
import io, os, runpy, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
for arg in sys.argv[1:]:
    f, edits = arg.split("=", 1)
    path = os.path.join(ROOT, f)
    s = io.open(path, encoding="utf-8", newline="").read()
    crlf = "\r\n" in s
    if crlf:
        assert s.count("\r\n") == s.count("\n"), "mixed line endings"
        s = s.replace("\r\n", "\n")
    n0 = len(s)
    for old, new in runpy.run_path(edits)["EDITS"]:
        assert s.count(old) == 1, (s.count(old), old[:90])
        s = s.replace(old, new)
    assert len(s) > n0 // 2
    if crlf:
        s = s.replace("\n", "\r\n")
    io.open(path, "w", encoding="utf-8", newline="").write(s)
    print("patched", path, len(s) - n0)
