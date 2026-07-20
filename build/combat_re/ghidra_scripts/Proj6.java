//@category YsOrigin
import ghidra.program.model.address.*;
public class Proj6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00583a00L, "entity allocator/ctor FUN_00583a00");
        dumpFunc(0x004faf20L, "bullet damage applier FUN_004faf20");
        close();
    }
}
