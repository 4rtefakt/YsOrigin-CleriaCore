//@category YsOrigin
import ghidra.program.model.address.*;
public class Task398 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = { 0x004045e0L, 0x0041d9a0L, 0x00590010L, 0x0058aeb0L, 0x0041bd90L, 0x0053a5e0L };
        String[] tags = { "FUN_004045e0 core/Steam 0x40", "FUN_0041d9a0 core/launcher 0x41",
            "FUN_00590010 effect 0x59", "FUN_0058aeb0 effect 0x58",
            "FUN_0041bd90 core/launcher 0x41", "FUN_0053a5e0 boss/effect 0x53" };
        for (int k = 0; k < fns.length; k++) {
            dumpFunc(fns[k], tags[k]);
            out.println("\n---- CALLERS of 0x" + Long.toHexString(fns[k]) + " ----");
            for (Address a : callersOf(fns[k])) out.println("  " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
