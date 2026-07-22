//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class TaskDump1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x00661d94L,
            0x00598ef0L,
            0x005507a0L,
            0x005985e0L,
            0x0051b100L,
            0x00517f30L,
            0x00416690L
        };
        for (long va : fns) {
            dumpFunc(va, "TARGET FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of 0x" + Long.toHexString(va) + " ----");
            for (Address a : callersOf(va)) {
                out.println("  " + a + "   [" + fnName(a) + "]");
            }
        }
        close();
    }
}
