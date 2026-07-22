//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSix extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005a0140L, "FUN_005a0140 window/pause-book 0x5a");
        callersOf(0x005a0140L);
        dumpFunc(0x00417560L, "FUN_00417560 core/launcher 0x41");
        callersOf(0x00417560L);
        dumpFunc(0x005c1600L, "FUN_005c1600 window/char-select 0x5c");
        callersOf(0x005c1600L);
        dumpFunc(0x00546760L, "FUN_00546760 boss 0x54");
        callersOf(0x00546760L);
        dumpFunc(0x005a25e0L, "FUN_005a25e0 window/pause-book 0x5a");
        callersOf(0x005a25e0L);
        dumpFunc(0x005e1c30L, "FUN_005e1c30 render/scenegraph 0x5e");
        callersOf(0x005e1c30L);
        close();
    }
}
