//@category YsOrigin
// The boost-skill path proves the engine keeps PER-CHARACTER u16 tables of ASO section
// ids (PTR_DAT_0068afd0/0068afe0 indexed by character DAT_0076bb74).  Find EVERY such
// table: scan initialised memory for runs of >=3 consecutive u16 values in [0x100,0x120)
// -- the player attack-section band -- and print them with their xrefs.
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ComboScan7 extends Re2Lib {

    boolean inBand(int v) { return v >= 0x100 && v < 0x120; }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Memory m = currentProgram.getMemory();
        out.println("===== u16 RUNS IN [0x100,0x120) (>=3 long) =====");
        for (MemoryBlock b : m.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            long start = b.getStart().getOffset(), end = b.getEnd().getOffset();
            out.println("\n-- block " + b.getName() + " 0x" + Long.toHexString(start)
                        + "..0x" + Long.toHexString(end));
            long a = start;
            while (a + 1 < end) {
                int v = m.getShort(toAddr(a)) & 0xffff;
                if (!inBand(v)) { a += 2; continue; }
                long runStart = a;
                List<Integer> run = new ArrayList<>();
                while (a + 1 < end) {
                    int w = m.getShort(toAddr(a)) & 0xffff;
                    if (!inBand(w)) break;
                    run.add(w); a += 2;
                }
                if (run.size() >= 3) {
                    StringBuilder sb = new StringBuilder();
                    for (int v2 : run) sb.append(String.format("0x%x ", v2));
                    out.println("  @0x" + Long.toHexString(runStart) + "  n=" + run.size() + " : " + sb);
                    for (Reference r : getReferencesTo(toAddr(runStart)))
                        out.println("        xref " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                }
            }
        }
        // the boost tables, resolved
        out.println("\n===== POINTER TABLES @0x68afd0 / 0x68afe0 =====");
        for (long base : new long[]{0x0068afd0L, 0x0068afe0L}) {
            out.println("  table 0x" + Long.toHexString(base));
            for (int i = 0; i < 4; i++) {
                long p = m.getInt(toAddr(base + i * 4)) & 0xffffffffL;
                StringBuilder sb = new StringBuilder();
                if (p > 0x400000 && p < 0x800000)
                    for (int k = 0; k < 6; k++) sb.append(String.format("0x%x ", m.getShort(toAddr(p + k * 2)) & 0xffff));
                out.println("    [" + i + "] -> 0x" + Long.toHexString(p) + "   " + sb);
            }
        }
        close();
    }
}
