//@category YsOrigin
import ghidra.program.model.address.*;
import java.util.*;
public class RenderWndDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x005f2770L, 0x005d9b60L, 0x005f6c30L,
            0x005f4110L, 0x005cebc0L, 0x00579d50L
        };
        String[] tags = {
            "FUN_005f2770 render/D3D 0x5f",
            "FUN_005d9b60 render/scenegraph 0x5d",
            "FUN_005f6c30 render/D3D 0x5f",
            "FUN_005f4110 render/D3D 0x5f",
            "FUN_005cebc0 window/menu 0x5c",
            "FUN_00579d50 window/go_wnd 0x57"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n---- CALLERS of " + tags[i] + " ----");
            for (Address a : callersOf(fns[i])) out.println("  " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
