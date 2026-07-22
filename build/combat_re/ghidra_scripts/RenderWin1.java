//@category YsOrigin
import ghidra.program.model.address.*;
public class RenderWin1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // A) CRT check
        dumpFunc(0x00620ba0L, "CRT-CHECK FUN_00620ba0");
        // B) render/window game funcs
        dumpFunc(0x00585ff0L, "FUN_00585ff0");
        callersOf(0x00585ff0L);
        dumpFunc(0x005aefd0L, "FUN_005aefd0");
        callersOf(0x005aefd0L);
        dumpFunc(0x005601e0L, "FUN_005601e0");
        callersOf(0x005601e0L);
        dumpFunc(0x0059bbd0L, "FUN_0059bbd0");
        callersOf(0x0059bbd0L);
        dumpFunc(0x005be780L, "FUN_005be780");
        callersOf(0x005be780L);
        close();
    }
}
