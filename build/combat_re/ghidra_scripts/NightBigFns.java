//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class NightBigFns extends Re2Lib {
    void withCallers(long va, String tag) {
        dumpFunc(va, tag);
        out.println("---- CALLERS of 0x" + Long.toHexString(va) + " ----");
        for (Address a : callersOf(va)) out.println("   " + a + "  [" + fnName(a) + "]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        withCallers(0x006237c0L, "A CRT? _tr_flush_block FUN_006237c0");
        withCallers(0x00585cb0L, "B1 effect FUN_00585cb0");
        withCallers(0x005e3a20L, "B2 render/scenegraph FUN_005e3a20");
        withCallers(0x005a30f0L, "B3 window/menu FUN_005a30f0");
        withCallers(0x00561f40L, "B4 window/text FUN_00561f40");
        withCallers(0x005c4950L, "B5 window/shop FUN_005c4950");
        withCallers(0x005f3c70L, "B6 render/D3D FUN_005f3c70");
        close();
    }
}
