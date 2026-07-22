//@category YsOrigin
import ghidra.program.model.address.*;
public class Big7B extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] targets = {
            0x0052af30L, 0x0059d040L, 0x0059cda0L, 0x00404320L,
            0x0057ad90L, 0x0052a820L, 0x005e8a20L
        };
        for (long va : targets) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS OF FUN_" + Long.toHexString(va) + " ----");
            for (Address a : callersOf(va)) out.println("  " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
