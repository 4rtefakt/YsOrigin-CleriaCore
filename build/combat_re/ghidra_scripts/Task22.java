//@category YsOrigin
import ghidra.program.model.address.*;
public class Task22 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // CRT triage
        long[] crt = {0x0065b151L, 0x0066941bL, 0x0065bdccL};
        for (long va : crt) {
            dumpFunc(va, "CRT? FUN_" + Long.toHexString(va));
            out.println("---- CALLERS ----");
            for (Address ca : callersOf(va)) out.println("  " + ca + "  [" + fnName(ca) + "]");
        }
        // GAME functions
        long[] g = {0x005627a0L, 0x0046d6e0L, 0x00454c30L};
        for (long va : g) {
            dumpFunc(va, "GAME FUN_" + Long.toHexString(va));
            out.println("---- CALLERS ----");
            for (Address ca : callersOf(va)) out.println("  " + ca + "  [" + fnName(ca) + "]");
        }
        close();
    }
}
