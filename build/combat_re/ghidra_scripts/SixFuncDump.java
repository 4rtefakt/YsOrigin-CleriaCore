//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class SixFuncDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x005ddf40L, 0x00406440L, 0x00568a00L,
            0x004178b0L, 0x005d2aa0L, 0x0045a3a0L
        };
        for (long va : fns) {
            dumpFunc(va, "TARGET FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of FUN_" + Long.toHexString(va) + " ----");
            List<Address> cs = callersOf(va);
            if (cs.isEmpty()) out.println("  (no callers - caller-less?)");
            for (Address a : cs) out.println("  " + a + "   [" + fnName(a) + "]");
        }
        close();
    }
}
