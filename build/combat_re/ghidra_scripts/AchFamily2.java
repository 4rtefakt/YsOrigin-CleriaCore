//@category YsOrigin
import ghidra.program.model.address.*;

public class AchFamily2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] targets = { 0x00404dd0L, 0x004057d0L };
        for (long va : targets) {
            dumpFunc(va, "SIBLING FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of FUN_" + Long.toHexString(va) + " ----");
            for (Address c : callersOf(va)) out.println("    " + c + "   [" + fnName(c) + "]");
        }
        close();
    }
}
