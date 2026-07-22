//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Task418b extends Re2Lib {
    void dumpWithCallers(long va, String tag) {
        dumpFunc(va, tag);
        out.println("\n---- CALLERS of 0x" + Long.toHexString(va) + " ----");
        for (Address a : callersOf(va)) out.println("  " + a + "   [" + fnName(a) + "]");
        // callees
        out.println("---- CALLEES (call targets in body) ----");
        Function f = fnAt(toAddr(va));
        if (f != null) {
            Set<String> seen = new HashSet<>();
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction i = it.next();
                for (Reference r : i.getReferencesFrom()) {
                    if (r.getReferenceType().isCall()) {
                        Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                        String nm = tf != null ? tf.getName() : r.getToAddress().toString();
                        if (seen.add(nm)) out.println("  " + i.getAddress() + " -> " + nm);
                    }
                }
            }
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpWithCallers(0x0051db70L, "FUN_0051db70 mesh region 0x51");
        dumpWithCallers(0x00525790L, "FUN_00525790 battle region 0x52");
        dumpWithCallers(0x00525150L, "FUN_00525150 battle region 0x52");
        dumpWithCallers(0x00464fc0L, "FUN_00464fc0 player/mover region 0x46");
        dumpWithCallers(0x00405e50L, "FUN_00405e50 core/boot region 0x40");
        dumpWithCallers(0x00598d50L, "FUN_00598d50 effect region 0x59");
        close();
    }
}
