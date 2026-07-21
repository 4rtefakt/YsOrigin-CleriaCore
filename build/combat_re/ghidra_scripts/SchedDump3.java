//@category YsOrigin
import ghidra.program.model.address.*;
public class SchedDump3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004776e0L, "MOVER-CMD setter FUN_004776e0 (walk/run) -> writes +0x6f8?");
        dumpFunc(0x00477720L, "MOVER-CMD setter FUN_00477720 (face/turn a,b) -> writes +0x6f8?");
        dumpFunc(0x00476200L, "FUN_00476200 (extra motion, +0x12c&0x400000)");
        close();
    }
}
