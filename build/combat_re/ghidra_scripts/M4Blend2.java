//@category YsOrigin
import ghidra.program.model.address.*;

public class M4Blend2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // gamma apply path (reads DAT_00765164 GammaValue)
        dumpFunc(0x005d4de0L, "gamma apply FUN_005d4de0 (reads GammaValue)");
        dumpFunc(0x005d4f00L, "FUN_005d4f00 (called at top of renderState setter)");
        // config read of GammaValue (clamp range)
        dumpFunc(0x0040d7e0L, "config GammaValue reader FUN_0040d7e0");
        // TSS / blend-arg setter used by the switch in FUN_005eb5b0
        dumpFunc(0x005eb520L, "FUN_005eb520 (TSS/blendarg setter)");
        dumpFunc(0x005d3600L, "FUN_005d3600 (sampler/tss setter)");
        dumpFunc(0x00429c30L, "FUN_00429c30 (bit3 custom-blend path)");
        // shop insufficient-SP trigger context
        dumpFunc(0x005c6f40L, "shop text/purchase FUN_005c6f40 (Arena SP)");
        close();
    }
}
