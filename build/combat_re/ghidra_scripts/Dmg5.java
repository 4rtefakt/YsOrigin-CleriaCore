// Dmg5: the script-binding name strings (Chr_Damage, Mesh_DropItem, ...) have NO xrefs,
// so they live in a data table. Brute-force scan all initialized memory for dwords equal to
// those string VAs, then dump the surrounding table records with resolved fn pointers.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class Dmg5 extends Re2Lib {

    String readCStr(long va, int max) {
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < max; i++) {
                byte b = m.getByte(toAddr(va + i));
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) (b & 0xff));
            }
        } catch (Exception e) { return null; }
        return sb.length() == 0 ? null : sb.toString();
    }

    boolean isCode(long v) {
        if (v < 0x401000L || v > 0x660000L) return false;
        try { return currentProgram.getListing().getInstructionAt(toAddr(v)) != null; }
        catch (Exception e) { return false; }
    }

    String fnOf(long v) {
        Function f = currentProgram.getFunctionManager().getFunctionAt(toAddr(v));
        if (f != null) return f.getName();
        return isCode(v) ? "(code,nofunc)" : null;
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg5_out.txt");
        out.println("######## DMG5: locate the script-binding table ########");
        Memory m = currentProgram.getMemory();

        long[] targets = {0x69f914L, 0x69f96cL, 0x69fa84L, 0x69fb44L, 0x69fc80L, 0x69fcb0L, 0x69fd0cL};
        Set<Long> tset = new HashSet<>();
        for (long t : targets) tset.add(t);

        out.println("\n===== brute scan: dwords pointing at the binding-name strings =====");
        List<Long> ptrSites = new ArrayList<>();
        for (MemoryBlock blk : m.getBlocks()) {
            if (!blk.isInitialized()) continue;
            long start = blk.getStart().getOffset(), end = blk.getEnd().getOffset();
            for (long a = start; a + 4 <= end; a += 4) {
                long v;
                try { v = m.getInt(toAddr(a)) & 0xffffffffL; } catch (Exception e) { continue; }
                if (tset.contains(v)) {
                    out.println(String.format("  ptr @0x%08x -> 0x%08x \"%s\"   blk=%s", a, v, readCStr(v, 64), blk.getName()));
                    ptrSites.add(a);
                }
            }
        }
        if (ptrSites.isEmpty()) { out.println("  (NONE FOUND - names are not stored as plain pointers)"); }

        // For each found site, dump a window of the table around it to reveal stride/shape
        for (long site : ptrSites) {
            out.println("\n\n===== TABLE WINDOW around 0x" + Long.toHexString(site) + " =====");
            long lo = site - 0x60, hi = site + 0x60;
            for (long a = lo; a <= hi; a += 4) {
                long v;
                try { v = m.getInt(toAddr(a)) & 0xffffffffL; } catch (Exception e) { continue; }
                String s = readCStr(v, 48);
                String fn = fnOf(v);
                String note = "";
                if (s != null) note = "  str=\"" + s + "\"";
                else if (fn != null) note = "  FN=" + fn;
                out.println(String.format("  0x%08x : %08x%s%s", a, v, note, a == site ? "   <<<< HIT" : ""));
            }
        }
        close();
    }
}
