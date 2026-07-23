//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class EffRender2 extends Re2Lib {
    void vtable(long base, String tag, int n) throws Exception {
        out.println("\n===== VTABLE " + tag + " @0x" + Long.toHexString(base) + " =====");
        for (int k = 0; k < n; k++) {
            Address slot = toAddr(base + k*4L);
            try {
                int p = getInt(slot);
                long fp = p & 0xffffffffL;
                out.println("  [" + k + "] +0x" + Integer.toHexString(k*4) + " -> 0x"
                    + Long.toHexString(fp) + "  " + fnName(toAddr(fp)));
            } catch (Exception e) { out.println("  [" + k + "] (unreadable)"); }
        }
    }
    // scan every instruction for a memory-operand displacement matching needle
    void scan(String needle) throws Exception {
        out.println("\n===== INSTRUCTIONS CONTAINING " + needle + " =====");
        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        String lastFn = "";
        int hits = 0;
        while (fit.hasNext()) {
            Function f = fit.next();
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction i = it.next();
                String s = i.toString();
                if (s.contains(needle)) {
                    String fn = f.getName() + "@" + f.getEntryPoint();
                    if (!fn.equals(lastFn)) { out.println(" -- " + fn); lastFn = fn; }
                    out.println("    " + i.getAddress() + "  " + s);
                    hits++;
                }
            }
        }
        out.println("  total hits: " + hits);
    }
    public void run() throws Exception {
        open(System.getenv("EFF_OUT"));
        vtable(0x0069b1d4L, "GoEffect", 24);
        vtable(0x0069f088L, "GoParticle", 24);
        scan("0x2b4");
        scan("0x2a7");
        close();
    }
}
