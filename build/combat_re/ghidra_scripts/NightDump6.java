//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class NightDump6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x00620790L, // CRT triage
            0x005a1180L, // window/pause-book
            0x00584860L, // effect
            0x0055c960L, // boss region
            0x005f6e50L, // render/D3D
            0x0041dd00L  // core/window
        };
        String[] tags = {
            "A CRT triage FUN_00620790",
            "B1 window FUN_005a1180",
            "B2 effect FUN_00584860",
            "B3 boss FUN_0055c960",
            "B4 render FUN_005f6e50",
            "B5 core FUN_0041dd00"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n---- CALLERS of " + tags[i] + " ----");
            for (Address a : callersOf(fns[i])) {
                out.println("   caller " + a + "  [" + fnName(a) + "]");
            }
        }
        close();
    }
}
