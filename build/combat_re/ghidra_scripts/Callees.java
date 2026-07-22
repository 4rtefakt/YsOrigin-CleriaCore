//@category YsOrigin
import ghidra.program.model.address.*;
public class Callees extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0041f570L, "FUN_0041f570 helper-in-proximity");
        dumpFunc(0x00671000L, "FUN_00671000 math-CRTband");
        dumpFunc(0x00670d30L, "FUN_00670d30 math-CRTband");
        dumpFunc(0x0040f700L, "FUN_0040f700 effect-getter");
        dumpFunc(0x00583a00L, "FUN_00583a00 effect-getter2");
        close();
    }
}
