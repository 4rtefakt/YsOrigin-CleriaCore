//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0044fdd0L, "per-A spawn FUN_0044fdd0 (one-quad-vs-N)");
        dumpFunc(0x004523e0L, "pool alloc FUN_004523e0");
        xrefs(0x007300b8L, "GoEffect draw list DAT_007300b8 (find render iterator)");
        close();
    }
}
