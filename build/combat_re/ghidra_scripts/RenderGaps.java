//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class RenderGaps extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ============ TARGET A — csprite blend preset table ============
        dumpFunc(0x005eb5b0L, "A: FUN_005eb5b0 renderstate preset applier (csprite +0x64)");
        dumpFunc(0x005ee040L, "A: FUN_005ee040 claimed table-fill / setshader");
        dumpFunc(0x005a43a0L, "A: FUN_005a43a0 csprite submit (reads +0x64, cmp DAT_00767760)");
        xrefs(0x00767800L, "A: DAT_00767800 preset table");
        xrefs(0x00767804L, "A: DAT_00767804 preset table+4");
        xrefs(0x00767760L, "A: DAT_00767760 current-preset cache");
        // also cover the ent/dialog applier trigger constants
        dumpFunc(0x005ed600L, "A: FUN_005ed600 2D-pass begin");

        // ============ TARGET B — water selection + reflection RT ============
        dumpFunc(0x005fa780L, "B: FUN_005fa780 HW-VS shader-select (water/cube/etc)");
        dumpFunc(0x005f8de0L, "B: FUN_005f8de0 CPU-skinned shader-select");
        dumpFunc(0x005ec6f0L, "B: FUN_005ec6f0 reflection/mirror matrix (MatrixReflect)");
        out.println("\n\n##### callers of FUN_005ec6f0 (reflection matrix) #####");
        for (Address a : callersOf(0x005ec6f0L)) out.println("  caller " + a + "  [" + fnName(a) + "]");
        dumpFunc(0x005eca30L, "B: FUN_005eca30 perspective camera build");
        dumpFunc(0x005e2e70L, "B: FUN_005e2e70 renderer init (backbuffer/Z/RT create)");

        // Find SetRenderTarget-style device-vtable calls across the renderer band.
        // IDirect3DDevice9::SetRenderTarget raw vtable index 37 = +0x94; but wrapper differs.
        // Scan the engine renderer band for vtable calls on DAT_007651c0 with unusual offsets.
        out.println("\n\n##### scan: CALL dword ptr [reg + 0x8c..0x9c] in 0x5e0000-0x602000 (RT set candidates) #####");
        scanVtblCalls(0x005e0000L, 0x00602000L, 0x88, 0xa0);

        // Also xref the engine device object to find who fetches it around reflection code
        out.println("\n\n##### xrefs of DAT_007651c0 (engine device) — near-reflection callers #####");
        xrefs(0x007651c0L, "DAT_007651c0 device");

        close();
    }

    // scan for "CALL dword ptr [<reg> + disp]" where disp in [lo,hi], print addr+fn
    void scanVtblCalls(long start, long end, int lo, int hi) {
        InstructionIterator it = currentProgram.getListing().getInstructions(
            currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(start), true);
        while (it.hasNext()) {
            Instruction i = it.next();
            long a = i.getAddress().getOffset();
            if (a >= end) break;
            String m = i.getMnemonicString();
            if (!m.equalsIgnoreCase("CALL")) continue;
            String s = i.toString();
            if (!s.contains("[")) continue;
            // parse a hex displacement like + 0x94]
            int idx = s.indexOf("+ 0x");
            if (idx < 0) continue;
            try {
                int endh = s.indexOf(']', idx);
                String hx = s.substring(idx + 4, endh).trim();
                long disp = Long.parseLong(hx, 16);
                if (disp >= lo && disp <= hi) {
                    out.println("  " + i.getAddress() + "  " + s + "   [" + fnName(i.getAddress()) + "]");
                }
            } catch (Exception e) {}
        }
    }
}
