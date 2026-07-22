//@category YsOrigin
import ghidra.program.model.address.*;
public class Task235 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x00536580L, 0x0051f280L, 0x005d83d0L,
            0x004d7420L, 0x005e6500L, 0x005e8120L
        };
        String[] tags = {
            "FUN_00536580 boss-0x53", "FUN_0051f280 obj-mesh-0x51", "FUN_005d83d0 render-0x5d",
            "FUN_004d7420 boss-brain-0x4d", "FUN_005e6500 render-0x5e", "FUN_005e8120 render-0x5e"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n----- CALLERS of " + tags[i] + " -----");
            for (Address a : callersOf(fns[i])) out.println("  " + a + "   [" + fnName(a) + "]");
        }
        close();
    }
}
