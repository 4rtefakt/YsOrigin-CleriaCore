//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class Op13cRange extends Re2Lib {
    void range(long start, long end, String tag) throws Exception {
        out.println("\n===== " + tag + " =====");
        Address a = toAddr(start);
        Address e = toAddr(end);
        Listing lst = currentProgram.getListing();
        while (a != null && a.getOffset() <= e.getOffset()) {
            Instruction ins = lst.getInstructionAt(a);
            if (ins == null) { out.println(String.format("  %08x  (no instr)", a.getOffset())); a = a.add(1); continue; }
            String ref = "";
            for (var r : ins.getReferencesFrom()) {
                if (r.getReferenceType().isCall() || r.getReferenceType().isJump()) {
                    ghidra.program.model.listing.Function f = getFunctionAt(r.getToAddress());
                    ref = "  ; -> " + r.getToAddress() + (f!=null?" ("+f.getName()+")":"");
                }
            }
            out.println(String.format("  %08x  %-40s%s", a.getOffset(), ins.toString(), ref));
            a = ins.getMaxAddress().add(1);
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        range(0x0056bdaaL, 0x0056bdd1L, "sub 0x13c handler @56bdaa");
        range(0x0056be1bL, 0x0056be42L, "sub 0x13e handler @56be1b");
        close();
    }
}
