//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class EffRender3 extends Re2Lib {
    void dumpMaybe(long va, String tag) throws Exception {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) { f = createFunction(a, null); }
        if (f == null) { out.println("\n### could not make func @0x" + Long.toHexString(va) + " (" + tag + ")"); return; }
        dumpFunc(va, tag);
    }
    public void run() throws Exception {
        open(System.getenv("EFF_OUT"));
        // Part B: the render consumer of the marker colour + random byte
        dumpMaybe(0x00473c60L, "B READER FUN_00473c60");
        out.println("---- CALLERS ----");
        for (Address ca : callersOf(0x00473c60L)) out.println("  " + ca + "  [" + fnName(ca) + "]");
        // Part A: GoEffect vtable methods (tick / draw)
        dumpMaybe(0x004515f0L, "A GoEffect vtbl1 FUN_004515f0");
        dumpMaybe(0x00451e40L, "A GoEffect vtbl2/tick FUN_00451e40");
        dumpMaybe(0x0044b410L, "A GoEffect vtbl5 FUN_0044b410");
        // Part A: GoParticle vtable methods
        dumpMaybe(0x00564110L, "A GoParticle vtbl1 FUN_00564110");
        dumpMaybe(0x005644c0L, "A GoParticle vtbl2 FUN_005644c0");
        dumpMaybe(0x00564c10L, "A GoParticle vtbl5 FUN_00564c10");
        dumpMaybe(0x00565170L, "A GoParticle vtbl6/draw FUN_00565170");
        // Part A: the child transform setup called from FUN_0044fdd0
        dumpMaybe(0x0044c250L, "A child-xform FUN_0044c250");
        close();
    }
}
