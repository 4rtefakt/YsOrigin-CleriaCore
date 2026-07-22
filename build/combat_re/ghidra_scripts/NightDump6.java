//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class NightDump6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x0042b350L, // camera 0x42
            0x0053f6e0L, // boss 0x53
            0x00596b30L, // effect/projectile 0x59
            0x00474cd0L, // object/enemy 0x47
            0x00579f10L, // window/go_wnd 0x57
            0x005e3fc0L  // render/scenegraph 0x5e
        };
        String[] tags = {
            "camera 0x42 FUN_0042b350",
            "boss 0x53 FUN_0053f6e0",
            "effect 0x59 FUN_00596b30",
            "object/enemy 0x47 FUN_00474cd0",
            "window 0x57 FUN_00579f10",
            "render 0x5e FUN_005e3fc0"
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
