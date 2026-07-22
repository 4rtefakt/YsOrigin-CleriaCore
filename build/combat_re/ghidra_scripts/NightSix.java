//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSix extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x0059ffe0L, 0x00445220L, 0x0045e050L,
            0x004060b0L, 0x005cd860L, 0x005e1220L
        };
        String[] tags = {
            "FUN_0059ffe0 effect/scenegraph",
            "FUN_00445220 object/ASO",
            "FUN_0045e050 object/mover",
            "FUN_004060b0 core/GOG",
            "FUN_005cd860 window/audio",
            "FUN_005e1220 render/scenegraph"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n---- CALLERS of " + tags[i] + " ----");
            for (Address a : callersOf(fns[i])) out.println("   " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
