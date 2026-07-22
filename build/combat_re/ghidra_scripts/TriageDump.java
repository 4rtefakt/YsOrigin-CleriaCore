//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class TriageDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x0066f850L, // CRT triage libm band
            0x00621fd0L, // CRT triage zlib band
            0x0044e2e0L,
            0x0053f9e0L,
            0x00405940L,
            0x005014b0L,
            0x0059c8d0L,
            0x00407390L,
            0x0059fa30L
        };
        for (long va : fns) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS ----");
            for (Address a : callersOf(va)) out.println("  " + a + "  in " + fnName(a));
        }
        close();
    }
}
