//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class TriageCore extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] targets = {
            0x00653630L, // CRT triage
            0x005d6310L, // render layer
            0x005a0d10L, // effect/scenegraph
            0x0058a210L, // effect
            0x00416a40L, // core/boot check CRT-ish
            0x005da630L, // render layer
            0x004429f0L, // object/ASO/resource
            0x0058eda0L  // effect
        };
        for (long va : targets) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS OF FUN_" + Long.toHexString(va) + " ----");
            for (Address a : callersOf(va)) out.println("   " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
