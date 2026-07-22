//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;
public class WndGap5 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = { 0x005cf580L, 0x0058daa0L, 0x005bd480L, 0x005a0550L, 0x0059de00L };
        for (long va : fns) {
            dumpFunc(va, "TARGET FUN_" + Long.toHexString(va));
            out.println("\n===== CALLERS OF FUN_" + Long.toHexString(va) + " =====");
            for (Address a : callersOf(va)) out.println("  " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
