//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class RenderGaps2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // reflection-pass orchestrators (callers of the MatrixReflect builder)
        dumpFunc(0x0042a080L, "B: FUN_0042a080 reflection-pass orchestrator (calls FUN_005ec6f0)");
        dumpFunc(0x00431260L, "B: FUN_00431260 room/scene render (calls FUN_005ec6f0)");

        // the render-target setter (SetRenderTarget +0x94 / SetDepthStencilSurface +0x9c)
        dumpFunc(0x005dfea0L, "B: FUN_005dfea0 SetRenderTarget/DepthStencil switch");
        dumpFunc(0x005fc990L, "B: FUN_005fc990 second +0x94/+0x9c RT user");

        // where does param_1 (variant index) into the shader-select come from?
        out.println("\n\n##### callers of FUN_005fa780 (HW shader-select) #####");
        for (Address a : callersOf(0x005fa780L)) out.println("  caller " + a + "  [" + fnName(a) + "]");
        out.println("\n##### callers of FUN_005f8de0 (skinned shader-select) #####");
        for (Address a : callersOf(0x005f8de0L)) out.println("  caller " + a + "  [" + fnName(a) + "]");

        // the mesh-draw that binds the reflection texture stage + calls shader-select
        dumpFunc(0x005fd750L, "B: FUN_005fd750 mesh draw (SetTexture stages, shader-select caller)");

        // RT pool globals
        xrefs(0x007655a8L, "B: DAT_007655a8 RT pool[0].color");
        xrefs(0x007b3450L, "B: DAT_007b3450 reflected view-proj matrix");

        close();
    }
}
