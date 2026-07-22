//@category YsOrigin
import ghidra.program.model.address.*;
public class Big7Dump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00599770L, "FUN_00599770 effect/scenegraph");
        callersOf(0x00599770L);
        dumpFunc(0x005cac90L, "FUN_005cac90 window/menu");
        callersOf(0x005cac90L);
        dumpFunc(0x0052d6d0L, "FUN_0052d6d0 object/battle");
        callersOf(0x0052d6d0L);
        dumpFunc(0x00589f60L, "FUN_00589f60 effect");
        callersOf(0x00589f60L);
        dumpFunc(0x0052b6e0L, "FUN_0052b6e0 object/battle");
        callersOf(0x0052b6e0L);
        dumpFunc(0x0058fa00L, "FUN_0058fa00 effect/GoXx");
        callersOf(0x0058fa00L);
        dumpFunc(0x005416a0L, "FUN_005416a0 boss 0x54");
        callersOf(0x005416a0L);
        close();
    }
}
