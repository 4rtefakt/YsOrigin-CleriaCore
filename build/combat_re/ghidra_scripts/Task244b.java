//@category YsOrigin
import ghidra.program.model.address.*;
public class Task244b extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005eecc0L, "angle helper FUN_005eecc0");
        dumpFunc(0x005f4e10L, "node-by-name FUN_005f4e10");
        close();
    }
}
