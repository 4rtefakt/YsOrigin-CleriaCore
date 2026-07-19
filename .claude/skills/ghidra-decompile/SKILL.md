---
name: ghidra-decompile
description: Dump decompiled C + disassembly of specific yso_win.exe functions/addresses to close RE gaps. Use whenever an RE doc has an [UNKNOWN]/gap function, or you need a function's decomp that isn't already in a build/*_re/*_out.txt dump.
---

# Ghidra headless decompile dump (Ys Origin `yso_win.exe`)

The reverse-engineering in `build/*_re/` comes from a Ghidra project. When a needed function isn't in an
existing `*_out.txt`, dump it yourself — the project is already analyzed, so this is fast (~30–90s).

## Setup (already exists on this machine)
- Ghidra: `D:\ghidra-work\ghidra\ghidra_12.1.2_PUBLIC\`
- Project: `D:\ghidra-work\YsOproj` (program **`yso_win.exe`**, fully analyzed — always use `-noanalysis`)
- Script base class: `build/combat_re/ghidra_scripts/Re2Lib.java` (extends `GhidraScript`). Key methods:
  - `open(path)` / `close()` — open the output PrintWriter + decompiler
  - `dumpFunc(long va, String tag)` — decomp C **and** raw disasm of the function containing `va`. Prints
    `### NO FUNC @0x..` if `va` isn't inside a defined function (define it or dump a nearby known fn).
  - `xrefs(long va, String tag)` — all references to `va` (find callers / who reads a `DAT_`)
  - `callersOf(long va)` — list of caller addresses

## Steps
1. Write a script `build/combat_re/ghidra_scripts/<Name>.java` extending `Re2Lib`, e.g.:
   ```java
   //@category YsOrigin
   import ghidra.program.model.address.*;
   public class MyDump extends Re2Lib {
       public void run() throws Exception {
           open(System.getenv("NIGHT_OUT"));          // output path via env
           dumpFunc(0x005a68f0L, "some fn FUN_005a68f0");
           xrefs(0x0074c128L, "arena SP global (find writers)");
           close();
       }
   }
   ```
2. Run it **via PowerShell** (NOT `cmd //c` from Bash — the `.bat` path quoting breaks there):
   ```powershell
   $env:NIGHT_OUT = "C:\...\scratchpad\mydump_out.txt"
   & "D:\ghidra-work\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat" "D:\ghidra-work" YsOproj `
     -process yso_win.exe -noanalysis `
     -scriptPath "C:\Users\kores\repos\YsOrigin-CleriaCore\build\combat_re\ghidra_scripts" `
     -postScript MyDump.java 2>&1 | Select-Object -Last 6
   ```
   Success looks like `REPORT: Execute script: MyDump.java` + `Save succeeded`. Give it a 300–420s timeout.
3. Read the `*_out.txt` (decomp C block, then `---- RAW DISASM ----` with `; -> value` / `; call name`
   annotations). Cite `FUN_`/`DAT_` addresses in whatever doc you write.

## Pitfalls
- **Serialize access.** The project has a single-writer lock — only ONE headless run at a time. Don't launch
  concurrent Ghidra runs (or concurrent agents that each run it). If parallelizing, the orchestrator runs the
  dumps; subagents read the resulting `*_out.txt` read-only.
- **Bash can't run the `.bat`** cleanly (`cmd //c "...escaped quotes..."` fails). Use the PowerShell tool.
- **`-noanalysis`** every time (the program is already analyzed; re-analysis wastes minutes).
- Raw `*_out.txt` dumps are **gitignored** (`build/*_re/` whitelists `.md`/`.py`/`.java`, not `_out.txt`). They
  are local working files — commit the derived `.md` findings, not the dump.
- Addresses are little-endian VA (e.g. `0x005a73c0L`). `LAB_xxxx` labels: pass the address; `dumpFunc` resolves
  the containing function. Bare `DAT_` globals have no function — use `xrefs` to find who touches them.
