//@category YsOrigin
import ghidra.program.model.address.*;
public class BigXref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x004192c0L, "FUN_004192c0 config-dlg-wndproc");
        xrefs(0x005002e0L, "FUN_005002e0 effect-update");
        xrefs(0x00540600L, "FUN_00540600 link-scan");
        xrefs(0x005875c0L, "FUN_005875c0 homing-proj");
        xrefs(0x005903b0L, "FUN_005903b0 lightning-ctor");
        xrefs(0x00460470L, "FUN_00460470 interact");
        close();
    }
}
