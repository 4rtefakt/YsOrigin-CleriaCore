//@category YsOrigin
import ghidra.program.model.address.*;
public class Proj5 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00597290L, "shared bullet mover/collider FUN_00597290 (kinds 1,2)");
        dumpFunc(0x00596a00L, "FUN_00596a00");
        close();
    }
}
