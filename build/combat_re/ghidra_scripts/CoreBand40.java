//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class CoreBand40 extends Re2Lib {
    long[] targets = {
        0x00409e50L,
        0x00404ad0L,
        0x00540310L,
        0x005f2a10L,
        0x005e87c0L,
        0x004fa940L,
    };
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        for (long va : targets) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of FUN_" + Long.toHexString(va) + " ----");
            for (Address c : callersOf(va)) {
                out.println("   " + c + "   [" + fnName(c) + "]");
            }
        }
        close();
    }
}
