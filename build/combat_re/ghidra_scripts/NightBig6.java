//@category YsOrigin
import ghidra.program.model.address.*;
import java.util.*;
public class NightBig6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] targets = {
            0x005f5e10L, 0x004185f0L, 0x005af980L,
            0x005a4de0L, 0x005f5910L, 0x005cd670L
        };
        for (long va : targets) {
            dumpFunc(va, "TARGET FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of FUN_" + Long.toHexString(va) + " ----");
            for (Address c : callersOf(va)) out.println("  " + c + "  [" + fnName(c) + "]");
        }
        close();
    }
}
