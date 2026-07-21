//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004515f0L, "GoEffect vtbl[1] FUN_004515f0");
        dumpFunc(0x00451e40L, "GoEffect vtbl[2] FUN_00451e40");
        dumpFunc(0x0044b3f0L, "GoEffect vtbl[0]/[4] FUN_0044b3f0");
        close();
    }
}
