//@category YsOrigin
import ghidra.program.model.address.*;
public class ManualDump2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005c1600L, "manual page-build helper FUN_005c1600 (reads enable byte)");
        dumpFunc(0x005c1d20L, "manual sub FUN_005c1d20");
        dumpFunc(0x0041fd10L, "new-game init writer FUN_0041fd10 (sets enable=0x101)");
        close();
    }
}
