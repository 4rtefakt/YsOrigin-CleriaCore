//@category YsOrigin
import ghidra.program.model.address.*;
public class TriageBatch extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // A) CRT triage
        dumpFunc(0x00628ff4L, "CRT triage FUN_00628ff4");
        callersOf(0x00628ff4L);
        dumpFunc(0x00652b3eL, "CRT triage FUN_00652b3e");
        callersOf(0x00652b3eL);
        // B) game functions
        dumpFunc(0x005886b0L, "GAME FUN_005886b0 [effect]");
        callersOf(0x005886b0L);
        dumpFunc(0x0053ff70L, "GAME FUN_0053ff70 [boss region]");
        callersOf(0x0053ff70L);
        dumpFunc(0x005bb1b0L, "GAME FUN_005bb1b0 [window/menu]");
        callersOf(0x005bb1b0L);
        dumpFunc(0x0059b430L, "GAME FUN_0059b430 [effect/scenegraph]");
        callersOf(0x0059b430L);
        dumpFunc(0x005f8a50L, "GAME FUN_005f8a50 [render/D3D]");
        callersOf(0x005f8a50L);
        close();
    }
}
