//@category YsOrigin
import ghidra.program.model.address.*;
public class SchedDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00479b00L, "SUSPECT action-player FUN_00479b00 (brain->executor, reads +0x710/+0x714 writes +0x6f8?)");
        close();
    }
}
