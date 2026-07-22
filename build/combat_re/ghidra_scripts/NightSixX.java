//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSixX extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x005a0140L, "FUN_005a0140 callers");
        xrefs(0x00417560L, "FUN_00417560 callers");
        xrefs(0x005c1600L, "FUN_005c1600 callers");
        xrefs(0x00546760L, "FUN_00546760 callers");
        xrefs(0x005a25e0L, "FUN_005a25e0 callers");
        xrefs(0x005e1c30L, "FUN_005e1c30 callers");
        // key globals to identify subsystem
        xrefs(0x0074c09cL, "DAT_0074c09c (target obj ptr)");
        xrefs(0x00794688L, "DAT_00794688 (menu sel idx)");
        xrefs(0x0076b8d8L, "DAT_0076b8d8 (enabled flags array)");
        close();
    }
}
