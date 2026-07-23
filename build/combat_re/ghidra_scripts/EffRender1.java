//@category YsOrigin
import ghidra.program.model.address.*;
public class EffRender1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("EFF_OUT"));
        // PART A: effect creator + transform + emitter loop + update + quad builder
        long[] a = {
            0x00450410L, // universal creator
            0x00451460L, // instance ctor
            0x00451a40L, // Scale x RotYPR x Translate
            0x004523e0L, // pool alloc
            0x0044fdd0L, // per-emitter/child loop
            0x00451e60L, // finalize
            0x00451f60L, // update (advance phase)
            0x00456da0L  // billboard quad geometry builder
        };
        for (long va : a) {
            dumpFunc(va, "A FUN_" + Long.toHexString(va));
            out.println("---- CALLERS ----");
            for (Address ca : callersOf(va)) out.println("  " + ca + "  [" + fnName(ca) + "]");
        }
        // PART B: the spawn op that tags group enemies red
        dumpFunc(0x00570400L, "B spawn-op FUN_00570400");
        out.println("---- CALLERS ----");
        for (Address ca : callersOf(0x00570400L)) out.println("  " + ca + "  [" + fnName(ca) + "]");
        close();
    }
}
