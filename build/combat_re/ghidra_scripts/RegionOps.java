//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class RegionOps extends Re2Lib {
    void window(long va, int len, String tag) {
        out.println("\n===== STUB WINDOW " + tag + " @0x" + Long.toHexString(va) + " =====");
        Address a = toAddr(va);
        Address end = toAddr(va + len);
        InstructionIterator it = currentProgram.getListing().getInstructions(a, true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (i.getAddress().compareTo(end) >= 0) break;
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                if (tf != null) sb.append("   ; call " + tf.getName());
            }
            out.println(sb);
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        window(0x0056bcadL, 0x95,  "op 0x13a Chr_IsInCylinder");
        window(0x0056bd42L, 0xb0,  "op 0x13b Chr_IsInBox");
        window(0x0056be1bL, 0x60,  "op 0x13e Chr_IsThisChr");
        // workers adjacent to the sphere test FUN_00570c10 — dump the block after it
        dumpFunc(0x00570c80L, "candidate worker A");
        dumpFunc(0x00570d20L, "candidate worker B");
        close();
    }
}
