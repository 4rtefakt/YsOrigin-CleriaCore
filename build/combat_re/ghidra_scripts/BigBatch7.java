//@category YsOrigin
import ghidra.program.model.address.*;
public class BigBatch7 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {0x0052c2c0L,0x005cbe40L,0x005d00f0L,0x005ae5e0L,0x005d8e50L,0x00559a50L,0x005bc260L};
        String[] tags = {"FUN_0052c2c0 obj/battle","FUN_005cbe40 window/menu","FUN_005d00f0 render",
            "FUN_005ae5e0 window/shop","FUN_005d8e50 render","FUN_00559a50 boss/enemy","FUN_005bc260 pausebook"};
        for (int i=0;i<fns.length;i++) dumpFunc(fns[i], tags[i]);
        out.println("\n\n########## CALLERS ##########");
        for (int i=0;i<fns.length;i++) {
            out.println("\n-- callers of " + tags[i] + " --");
            for (Address a : callersOf(fns[i])) out.println("  " + a + "  [" + fnName(a) + "]");
        }
        close();
    }
}
