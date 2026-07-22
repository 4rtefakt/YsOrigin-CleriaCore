//@category YsOrigin
import ghidra.program.model.address.*;
public class NightRender6b extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00563d80L, "FUN_00563d80 (proximity test)");
        xrefs(0x005d9730L, "FUN_005d9730 (plane/ray)");
        xrefs(0x007b38f0L, "DAT_007b38f0 (0x4a0-table index)");
        xrefs(0x0073014cL, "DAT_0073014c (GoScript registry list)");
        xrefs(0x00764b94L, "DAT_00764b94 (input dev joystick?)");
        xrefs(0x00764b8cL, "DAT_00764b8c (input dev keyboard?)");
        xrefs(0x00764b88L, "DAT_00764b88 (input dev mouse?)");
        // Poll/Acquire/GetDeviceState helper identity: what creates these globals?
        dumpFunc(0x0041f570L, "FUN_0041f570 (sqrt wrapper?)");
        close();
    }
}
