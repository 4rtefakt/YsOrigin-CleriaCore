//@category YsOrigin
import ghidra.program.model.address.*;

public class BossDump3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00562990L, "PER-FRAME DISPATCH FUN_00562990 (switch obj+0x28d -> brain)");
        dumpFunc(0x00503ef0L, "UPDATE ENTRY FUN_00503ef0 (vtable)");
        close();
    }
}
