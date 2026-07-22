//@category YsOrigin
import ghidra.program.model.address.*;
public class Task418 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x0051db70L, 0x00525790L, 0x00525150L,
            0x00464fc0L, 0x00405e50L, 0x00598d50L
        };
        String[] tags = {
            "FUN_0051db70 mesh0x51", "FUN_00525790 battle0x52", "FUN_00525150 battle0x52",
            "FUN_00464fc0 mover0x46", "FUN_00405e50 boot0x40", "FUN_00598d50 effect0x59"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n---- CALLERS of " + tags[i] + " ----");
            for (Address a : callersOf(fns[i]))
                out.println("  " + a + "   [" + fnName(a) + "]");
        }
        close();
    }
}
