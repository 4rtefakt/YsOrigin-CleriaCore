//@category YsOrigin
import ghidra.program.model.address.*;
public class SixSweep extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x005fab70L, 0x00417dc0L, 0x0044cbd0L,
            0x00565fc0L, 0x005d0f70L, 0x005d1150L
        };
        for (long va : fns) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("---- CALLERS ----");
            for (Address c : callersOf(va)) out.println("  caller: " + fnName(c));
        }
        close();
    }
}
