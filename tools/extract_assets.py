"""Robust asset extractor for CleriaCore.

The sibling project's ni_unpack aborts on CP932 filenames Windows can't create
(e.g. names containing '?'). This wrapper extracts every entry with a per-file
try/except and sanitizes illegal characters, so one bad name doesn't truncate
the whole extraction.

    python tools/extract_assets.py <data.ni> <outdir> [--filter .DDS]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(r"C:/Users/kores/repos/YsOrigin-Archipelago")))
from tools.ni_unpack import NIArchive, self_open  # noqa: E402

ILLEGAL = '<>:"|?*'


def safe(name: str) -> str:
    # keep the directory structure, sanitize each component
    out = []
    for part in name.replace("\\", "/").split("/"):
        out.append("".join("_" if c in ILLEGAL else c for c in part))
    return "/".join(out)


def main() -> int:
    ni = Path(sys.argv[1])
    outdir = Path(sys.argv[2])
    substr = None
    if "--filter" in sys.argv:
        substr = sys.argv[sys.argv.index("--filter") + 1]

    ar = NIArchive(ni)
    outdir.mkdir(parents=True, exist_ok=True)
    ok = skipped = 0

    # Match the filter against a normalised name. The archive stores Windows-style
    # paths ("MISC\\CASTINFO.DAT"), so the underlying filter's raw substring test
    # makes an obvious query like "--filter MISC/" match NOTHING and exit silently.
    # That quietly kept CASTINFO/ENEMYINFO/GAMEINFO out of assets/ for a long time.
    def wanted(entry) -> bool:
        if not substr:
            return True
        name = entry.clean_name.replace("\\", "/").lower()
        return substr.replace("\\", "/").lower() in name

    with self_open(ar) as na:
        for e in (x for x in ar.filtered(None) if wanted(x)):
            try:
                data = ar.read(e, na)
                dest = outdir / safe(e.clean_name)
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                ok += 1
                if ok % 1000 == 0:
                    print(f"  ... {ok}")
            except Exception as ex:
                skipped += 1
                if skipped <= 10:
                    print(f"  skip {e.clean_name!r}: {ex}")
    print(f"extracted {ok}, skipped {skipped} -> {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
