//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;
public class BigBatch58 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] targets = {
            0x00584f90L, 0x0058d3a0L, 0x0058cd00L, 0x0059aa90L,
            0x00411820L, 0x004535d0L, 0x0040e940L
        };
        for (long va : targets) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS OF 0x" + Long.toHexString(va) + " ----");
            for (Address a : callersOf(va)) out.println("  " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
