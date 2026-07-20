//@category YsOrigin
import ghidra.program.model.address.*;
public class Proj4 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00594230L, "bullet per-tick update FUN_00594230");
        close();
    }
}
