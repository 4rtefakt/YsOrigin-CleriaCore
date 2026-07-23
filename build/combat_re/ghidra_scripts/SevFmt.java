//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import java.util.*;

public class SevFmt extends Re2Lib {

    // find defined strings whose text contains needle; print addr + xrefs
    void findStr(String needle) throws Exception {
        out.println("\n===== STRINGS containing \"" + needle + "\" =====");
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        int n = 0;
        while (di.hasNext() && n < 200) {
            Data d = di.next();
            Object v = d.getValue();
            if (v instanceof String) {
                String s = (String) v;
                if (s.contains(needle)) {
                    out.println("  @" + d.getAddress() + "  \"" + s.replace("\n","\\n") + "\"");
                    for (Reference r : getReferencesTo(d.getAddress())) {
                        out.println("      xref " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                    }
                    n++;
                }
            }
        }
        if (n == 0) out.println("  (none found as defined data)");
    }

    // raw memory scan for an ascii needle across initialized memory; report VAs
    void scanBytes(String needle) throws Exception {
        out.println("\n===== RAW BYTE SCAN for \"" + needle + "\" =====");
        byte[] pat = needle.getBytes("ASCII");
        Memory mem = currentProgram.getMemory();
        int hits = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            Address a = b.getStart();
            Address found = mem.findBytes(a, b.getEnd(), pat, null, true, mon);
            while (found != null && hits < 40) {
                out.println("  @" + found);
                for (Reference r : getReferencesTo(found)) {
                    out.println("      xref " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                }
                hits++;
                if (found.add(1).compareTo(b.getEnd()) >= 0) break;
                found = mem.findBytes(found.add(1), b.getEnd(), pat, null, true, mon);
            }
        }
        if (hits == 0) out.println("  (none)");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // 1. the loader that allocates + fills DAT_0076a2b4
        dumpFunc(0x00442260L, "SEV loader / named-box table fill FUN_00442260");
        // 2. the zeroer
        dumpFunc(0x004429f0L, "named-box table zeroer FUN_004429f0");

        // 3. who calls the loader (map init path)
        out.println("\n===== CALLERS of FUN_00442260 =====");
        for (Address a : callersOf(0x00442260L))
            out.println("  " + a + "   [" + fnName(a) + "]");

        // 4. the .SEV path / magic evidence
        findStr("SEV");
        findStr(".SEV");
        findStr("%s");
        scanBytes("SEV");

        // 5. globals used by the table
        xrefs(0x0076a2b4L, "box table base");
        xrefs(0x0076a2b8L, "box table count");

        close();
    }
}
