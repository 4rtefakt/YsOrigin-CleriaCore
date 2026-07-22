//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn5Item extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Item page draw fn (case 1 draw, PAUSEBOOK §1.3)
        dumpFunc(0x005aad90L, "Item page DRAW FUN_005aad90");
        // Item page builder (opener param_2==0 runs it; nightgaps6: reads item records + map markers)
        dumpFunc(0x005ab0d0L, "Item grid BUILDER FUN_005ab0d0");
        // Item page update / cursor-nav
        dumpFunc(0x005aa370L, "Item UPDATE FUN_005aa370");
        // Helper reading DAT_0076bb74 near item draw (FUN_005aac10)
        dumpFunc(0x005aac10L, "Item helper FUN_005aac10");
        // Compare: Equip grid builder (already partly known) for shared-code check
        dumpFunc(0x005a9d10L, "Equip BUILDER FUN_005a9d10 (compare)");
        close();
    }
}
