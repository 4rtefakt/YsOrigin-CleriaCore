//@category YsOrigin
import ghidra.program.model.address.*;
import java.util.*;

public class BigFuncs2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004cc070L, "CALLER FUN_004cc070");
        out.println("\n#### CALLERS OF FUN_004cc070 ####");
        for (Address a : callersOf(0x004cc070L)) out.println("  caller " + a + "  [" + fnName(a) + "]");
        xrefs(0x004cc070L, "FUN_004cc070 (find vtable / installer)");
        // constants seen in the big fn: owner type ids 0x10d,0x110; sibling 0xb7
        // Find code that writes 0xb7 (183) into +0x124 near this region - the ctor sets type id.
        close();
    }
}
