//@category YsOrigin
import ghidra.program.model.address.*;
public class SchedDump4 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004c1260L, "M230 BRAIN FUN_004c1260 (cross-check +0x710/+0x708/+0x714 writes)");
        dumpFunc(0x00478080L, "EXECUTOR FUN_00478080 (reads +0x6f8 -> mover flags)");
        close();
    }
}
