//@category YsOrigin
// FUN_00446550(aniObj = actor+0x8f0, sectionId, flag) is the ASO section setter.
// 1. list every call site with the ~8 preceding instructions (to catch the pushed id)
// 2. dump the signed-byte table at 0x0069bc14 indexed by action id (-> actor+0x404)
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ComboScan5 extends Re2Lib {

    void callSitesWithContext(long target, String tag, int back) throws Exception {
        out.println("\n===== CALL SITES OF " + tag + " @0x" + Long.toHexString(target) + " =====");
        Listing lst = currentProgram.getListing();
        List<Address> sites = new ArrayList<>();
        for (Reference r : getReferencesTo(toAddr(target)))
            if (r.getReferenceType().isCall()) sites.add(r.getFromAddress());
        Collections.sort(sites);
        for (Address s : sites) {
            out.println("\n  -- call at " + s + "   [" + fnName(s) + "]");
            List<String> ctx = new ArrayList<>();
            Instruction i = lst.getInstructionAt(s);
            for (int k = 0; k < back && i != null; k++) {
                i = i.getPrevious();
                if (i == null) break;
                ctx.add(0, "       " + i.getAddress() + "  " + i);
            }
            for (String c : ctx) out.println(c);
            out.println("     >" + s + "  " + lst.getInstructionAt(s));
        }
        if (sites.isEmpty()) out.println("  (none)");
    }

    void dumpTable(long base, int lo, int hi, String tag) throws Exception {
        out.println("\n===== SIGNED BYTE TABLE " + tag + " @0x" + Long.toHexString(base) + " =====");
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        for (int i = lo; i < hi; i++) {
            if ((i % 16) == 0) { out.println(sb); sb = new StringBuilder(String.format("  [0x%03x] ", i)); }
            byte b = m.getByte(toAddr(base + i));
            sb.append(String.format("%4d", (int) b));
        }
        out.println(sb);
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpTable(0x0069bc14L, 0x00, 0x20, "action -> actor+0x404 (low ids, for shape)");
        dumpTable(0x0069bc14L, 0xf0, 0x150, "action -> actor+0x404 (attack ids)");
        callSitesWithContext(0x00446550L, "ASO SetSection FUN_00446550", 10);
        close();
    }
}
