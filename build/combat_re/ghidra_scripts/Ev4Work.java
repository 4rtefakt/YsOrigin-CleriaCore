//@category YsOrigin
import ghidra.program.model.address.*;

public class Ev4Work extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00571500L, "FireBullet worker FUN_00571500 (0x111)");
        dumpFunc(0x00572770L, "DebugArena worker FUN_00572770 (0x10d)");
        close();
    }
}
