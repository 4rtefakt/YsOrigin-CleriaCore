//@category YsOrigin
import ghidra.program.model.address.*;
public class RenderGaps3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005fb0e0L, "B: FUN_005fb0e0 per-submesh draw (passes param_1 variant to shader-select)");
        xrefs(0x007b2e3cL, "B: DAT_007b2e3c reflection-present gate");
        xrefs(0x00765598L, "B: DAT_00765598 reflection RT color surface");
        xrefs(0x00765594L, "B: DAT_00765594 scene/back RT color surface");
        close();
    }
}
