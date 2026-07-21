#!/usr/bin/env python3
"""RE-coverage metric for CleriaCore.

Estimates how much of yso_win.exe has been reverse-engineered, on the objective
basis of the exe's own functions. A function counts as "documented" when its
address is referenced in a build/*_re/*.md doc (we have written about it), and
"dumped" when it appears in a Ghidra *_out.txt decompilation dump (we have at
least pulled it). Both counts are reported, by function count AND by code bytes
(size-weighting: a 2 KB function is more of the game than a 20-byte stub).

It is deliberately an ESTIMATE - a referenced address is a proxy for understanding,
library/CRT boilerplate is only heuristically separated, and prose can mention an
address without truly explaining it. Good enough to watch the trend.

Denominator: D:/ghidra-work/all_functions.txt (produced by FuncDump.java). Run that
Ghidra script first; without it the tool reports numerator-only.

Usage: python tools/re_coverage.py [--write]
  --write  regenerate build/RE_COVERAGE.md and build/re_coverage.json
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Prefer the committed copy (recompute without Ghidra); fall back to the fresh dump.
FUNCLIST_REPO = os.path.join(ROOT, "build", "re_coverage", "all_functions.txt")
FUNCLIST_GHIDRA = "D:/ghidra-work/all_functions.txt"
FUNCLIST = FUNCLIST_REPO if os.path.exists(FUNCLIST_REPO) else FUNCLIST_GHIDRA
DUMPS_GLOB = "D:/ghidra-work/*_out.txt"

ADDR_RE = re.compile(r"(?:FUN_|0x)0*([0-9a-fA-F]{5,8})")
# A dump header naming the PRIMARY decompiled function, across the dump styles seen:
#   "======== FUN_xxxxxxxx = ... @ 004494e0 ===="  and  "##### BLOCK ... @ 00448468 #####".
# Counting these (not every callee address in the body) is what "we decompiled this".
DUMP_HEADER_RE = re.compile(r"^[=#].*@\s*0*([0-9a-fA-F]{6,8})")


def norm(addr_hex):
    """Normalise a hex address to a lowercase int key (drop leading zeros/0x/FUN_)."""
    return int(addr_hex, 16)


def load_functions():
    """{addr:int -> (size:int, name:str, thunk:bool, external:bool)} or None."""
    if not os.path.exists(FUNCLIST):
        return None
    funcs = {}
    with open(FUNCLIST, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            if len(parts) < 5:
                continue
            addr, size, name, thunk, external = parts[0], parts[1], parts[2], parts[3], parts[4]
            try:
                funcs[norm(addr)] = (int(size), name,
                                     thunk.strip().lower() == "true",
                                     external.strip().lower() == "true")
            except ValueError:
                continue
    return funcs


def is_library(name):
    """Heuristic: a Ghidra-NAMED function (not FUN_/thunk_) is CRT/STL/D3D boilerplate
    identified by FunctionID; game code stays as FUN_ until we name it. Imperfect."""
    if name.startswith("FUN_"):
        return False
    if name.startswith(("thunk_FUN_", "LAB_")):
        return False
    return True  # has a real symbol name -> library-identified


def scan_addresses(paths):
    """Every distinct function address referenced across the given files (prose docs:
    any FUN_/0x mention counts; non-function hex is filtered later by intersecting
    with the real function list)."""
    seen = set()
    for p in paths:
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        for m in ADDR_RE.finditer(text):
            try:
                seen.add(norm(m.group(1)))
            except ValueError:
                pass
    return seen


def scan_dump_primaries(paths):
    """Only the PRIMARY decompiled function of each dump block (its @-header line),
    not every callee address in the body -- so this counts functions we actually
    decompiled, not merely referenced."""
    seen = set()
    for p in paths:
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = DUMP_HEADER_RE.match(line)
                    if m:
                        try:
                            seen.add(norm(m.group(1)))
                        except ValueError:
                            pass
        except OSError:
            continue
    return seen


def subsystem_of(path):
    """build/<name>_re/<doc>.md -> "<name>"."""
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    m = re.match(r"build/([^/]+?)_re/", rel)
    return m.group(1) if m else "(other)"


def main():
    write = "--write" in sys.argv
    docs = glob.glob(os.path.join(ROOT, "build", "*_re", "*.md"))
    dumps = glob.glob(DUMPS_GLOB)

    documented = scan_addresses(docs)
    dumped = scan_dump_primaries(dumps)
    funcs = load_functions()

    # Per-subsystem documented-address counts (numerator detail, always available).
    per_sys = {}
    for d in docs:
        sys_name = subsystem_of(d)
        addrs = scan_addresses([d])
        per_sys.setdefault(sys_name, set()).update(addrs)

    report = {
        "documented_functions": len(documented),
        "dumped_functions": len(dumped),
        "docs_scanned": len(docs),
        "dumps_scanned": len(dumps),
    }

    lines = []
    lines.append("# CleriaCore - RE coverage of yso_win.exe")
    lines.append("")
    lines.append("Auto-generated by `tools/re_coverage.py` - an ESTIMATE (see the script header for")
    lines.append("caveats). A function is *documented* if its address appears in a `build/*_re/*.md`")
    lines.append("doc, *dumped* if it appears in a Ghidra `*_out.txt`. Regenerate with")
    lines.append("`python tools/re_coverage.py --write`.")
    lines.append("")

    if funcs is None:
        lines.append("> **Denominator missing.** Run the Ghidra script `FuncDump.java` to produce")
        lines.append("> `D:/ghidra-work/all_functions.txt`, then rerun. Numerator-only for now.")
        lines.append("")
        lines.append(f"- Functions **documented** (referenced in RE docs): **{len(documented)}**")
        lines.append(f"- Functions **dumped** (in Ghidra decompilation output): **{len(dumped)}**")
    else:
        # Split library-identified from game/unknown code, by count and by bytes.
        game = {a: v for a, v in funcs.items()
                if not is_library(v[1]) and not v[2] and not v[3]}
        total_all = len(funcs)
        total_game = len(game)
        bytes_all = sum(v[0] for v in funcs.values())
        bytes_game = sum(v[0] for v in game.values())

        doc_in_game = documented & set(game)
        dump_in_game = dumped & set(game)
        doc_bytes = sum(game[a][0] for a in doc_in_game)
        dump_bytes = sum(game[a][0] for a in dump_in_game)

        pct = lambda a, b: (100.0 * a / b) if b else 0.0
        report.update({
            "total_functions": total_all,
            "game_functions": total_game,
            "game_code_bytes": bytes_game,
            "documented_game_functions": len(doc_in_game),
            "dumped_game_functions": len(dump_in_game),
            "pct_documented_by_count": round(pct(len(doc_in_game), total_game), 2),
            "pct_dumped_by_count": round(pct(len(dump_in_game), total_game), 2),
            "pct_documented_by_bytes": round(pct(doc_bytes, bytes_game), 2),
            "pct_dumped_by_bytes": round(pct(dump_bytes, bytes_game), 2),
        })

        lines.append(f"**Headline: ~{report['pct_documented_by_bytes']:.1f}% of the game code is documented, "
                     f"~{report['pct_dumped_by_bytes']:.1f}% at least decompiled** (by code volume).")
        lines.append("")
        lines.append("| metric | documented | dumped | of |")
        lines.append("|---|---|---|---|")
        lines.append(f"| by function count | **{report['pct_documented_by_count']:.1f}%** "
                     f"| {report['pct_dumped_by_count']:.1f}% | {total_game} game functions |")
        lines.append(f"| by code bytes | **{report['pct_documented_by_bytes']:.1f}%** "
                     f"| {report['pct_dumped_by_bytes']:.1f}% | {bytes_game:,} bytes |")
        lines.append("")
        lines.append(f"- Total functions in exe: **{total_all}** "
                     f"({total_game} game/unknown, {total_all - total_game} library-identified)")
        lines.append(f"- Documented game functions: **{len(doc_in_game)}** / {total_game}")
        lines.append(f"- Dumped game functions: **{len(dump_in_game)}** / {total_game}")

        # Self-directing target list: the biggest functions we have NOT documented are
        # the highest coverage-per-function Ghidra targets. Regenerating this file keeps
        # the "what to RE next" list current as coverage grows.
        undoc = sorted(((game[a][0], a) for a in set(game) - documented), reverse=True)
        undoc_bytes = sum(s for s, _ in undoc)
        lines.append(f"- Undocumented: **{len(undoc)}** game functions, **{undoc_bytes:,}** bytes")
        lines.append("")
        lines.append("### Biggest undocumented functions (best next Ghidra targets)")
        lines.append("")
        lines.append("| function | bytes |")
        lines.append("|---|---|")
        for sz, a in undoc[:25]:
            lines.append(f"| FUN_{a:08x} | {sz:,} |")

    lines.append("")
    lines.append("## By subsystem (documented functions per `build/*_re/`)")
    lines.append("")
    lines.append("| subsystem | documented fns |")
    lines.append("|---|---|")
    for sys_name in sorted(per_sys, key=lambda s: -len(per_sys[s])):
        lines.append(f"| {sys_name} | {len(per_sys[sys_name])} |")

    out_md = "\n".join(lines) + "\n"
    print(out_md)

    if write:
        with open(os.path.join(ROOT, "build", "RE_COVERAGE.md"), "w", encoding="utf-8") as f:
            f.write(out_md)
        with open(os.path.join(ROOT, "build", "re_coverage.json"), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
        print("wrote build/RE_COVERAGE.md + build/re_coverage.json")


if __name__ == "__main__":
    main()
