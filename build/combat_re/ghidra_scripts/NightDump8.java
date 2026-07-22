//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class NightDump8 extends Re2Lib {
    void callers(long va, String tag) {
        out.println("\n----- CALLERS OF " + tag + " @0x" + Long.toHexString(va) + " -----");
        List<Address> cs = callersOf(va);
        if (cs.isEmpty()) { out.println("  (none)"); return; }
        for (Address a : cs) out.println("  " + a + "   [" + fnName(a) + "]");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x00621c20L, 0x00589690L, 0x005291c0L, 0x0053c480L,
            0x0059c510L, 0x0041c150L, 0x00545000L, 0x0051c8c0L
        };
        String[] tags = {
            "CRT-triage FUN_00621c20", "effect FUN_00589690", "obj/battle FUN_005291c0",
            "boss/enemy FUN_0053c480", "effect/scenegraph FUN_0059c510", "core/boot FUN_0041c150",
            "boss FUN_00545000", "obj/mesh FUN_0051c8c0"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            callers(fns[i], tags[i]);
        }
        close();
    }
}
