//@category YsOrigin
import ghidra.program.model.address.*;
public class NightTriage extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // CRT triage
        dumpFunc(0x00622cb0L, "CRT? FUN_00622cb0");
        dumpFunc(0x00671c1eL, "CRT? FUN_00671c1e");
        // GAME functions
        long[] g = {0x00560ab0L,0x00456da0L,0x00591850L,0x00598020L,0x005ae970L,0x0058e7c0L,0x0046e460L,0x0045d940L};
        for (long va : g) {
            dumpFunc(va, "GAME FUN_" + Long.toHexString(va));
            out.println("---- CALLERS ----");
            for (Address ca : callersOf(va)) out.println("  " + ca + "  [" + fnName(ca) + "]");
        }
        close();
    }
}
