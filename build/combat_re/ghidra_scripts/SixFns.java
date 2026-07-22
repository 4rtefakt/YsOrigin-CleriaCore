//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class SixFns extends Re2Lib {
    void withCallers(long va, String tag) {
        dumpFunc(va, tag);
        out.println("---- CALLERS ----");
        for (Address a : callersOf(va)) out.println("  " + a + "   [" + fnName(a) + "]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        withCallers(0x0058ab50L, "FUN_0058ab50 effect-0x58");
        withCallers(0x005da3d0L, "FUN_005da3d0 render-0x5d");
        withCallers(0x00528fb0L, "FUN_00528fb0 object-0x52");
        withCallers(0x00426e30L, "FUN_00426e30 core-0x42");
        withCallers(0x00536940L, "FUN_00536940 render-0x53");
        withCallers(0x0045f380L, "FUN_0045f380 mover-0x45");
        close();
    }
}
