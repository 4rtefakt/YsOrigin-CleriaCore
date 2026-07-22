//@category YsOrigin
import ghidra.program.model.address.*;
public class SixCal extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0044f2e0L, "FUN_0044f2e0 (draw callee of 44cbd0)");
        dumpFunc(0x00444e60L, "FUN_00444e60 (go_script open, callee of 565fc0)");
        dumpFunc(0x005edf40L, "FUN_005edf40 (callee of 5fab70)");
        close();
    }
}
