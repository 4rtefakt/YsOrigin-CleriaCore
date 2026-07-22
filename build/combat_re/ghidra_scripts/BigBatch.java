//@category YsOrigin
import ghidra.program.model.address.*;
public class BigBatch extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004192c0L, "FUN_004192c0");
        callersOf(0x004192c0L);
        dumpFunc(0x005002e0L, "FUN_005002e0");
        callersOf(0x005002e0L);
        dumpFunc(0x00540600L, "FUN_00540600");
        callersOf(0x00540600L);
        dumpFunc(0x005875c0L, "FUN_005875c0");
        callersOf(0x005875c0L);
        dumpFunc(0x005903b0L, "FUN_005903b0");
        callersOf(0x005903b0L);
        dumpFunc(0x00460470L, "FUN_00460470");
        callersOf(0x00460470L);
        close();
    }
}
