//@category YsOrigin
import ghidra.program.model.address.*;
import java.util.*;

public class SixCore extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {0x00562650L, 0x005eb0f0L, 0x0053c860L, 0x004394a0L, 0x00452130L, 0x004495b0L};
        for (long va : fns) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of FUN_" + Long.toHexString(va) + " ----");
            for (Address a : callersOf(va)) out.println("  " + a + "   [" + fnName(a) + "]");
        }
        close();
    }
}
