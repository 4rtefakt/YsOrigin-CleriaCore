//@category YsOrigin
import ghidra.program.model.address.*;
import java.util.*;

public class BigFuncs extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The big GAME function to fully document
        dumpFunc(0x004ca830L, "BIG-A FUN_004ca830");
        // Top-12 triage targets
        dumpFunc(0x006664bcL, "T FUN_006664bc");
        dumpFunc(0x004558c0L, "T FUN_004558c0");
        dumpFunc(0x00497bb0L, "T FUN_00497bb0");
        dumpFunc(0x0046a3e0L, "T FUN_0046a3e0");
        dumpFunc(0x004029d0L, "T FUN_004029d0");
        dumpFunc(0x00672329L, "T FUN_00672329");
        dumpFunc(0x0044f2e0L, "T FUN_0044f2e0");
        dumpFunc(0x00446750L, "T FUN_00446750");
        dumpFunc(0x005e6960L, "T FUN_005e6960");
        dumpFunc(0x0046d8c0L, "T FUN_0046d8c0");
        dumpFunc(0x0059d2e0L, "T FUN_0059d2e0");

        // Callers of the big one
        out.println("\n\n#### CALLERS OF FUN_004ca830 ####");
        for (Address a : callersOf(0x004ca830L)) out.println("  caller " + a + "  [" + fnName(a) + "]");
        xrefs(0x004ca830L, "FUN_004ca830");
        close();
    }
}
