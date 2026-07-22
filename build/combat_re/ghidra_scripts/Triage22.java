//@category YsOrigin
import ghidra.program.model.address.*;
public class Triage22 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // CRT triage
        dumpFunc(0x0066bdaeL, "CRT-triage FUN_0066bdae");
        dumpFunc(0x00620130L, "libpng-triage FUN_00620130");
        // Game functions
        dumpFunc(0x005a02e0L, "window/pause 0x5a FUN_005a02e0");
        dumpFunc(0x005d7b00L, "render/scenegraph 0x5d FUN_005d7b00");
        dumpFunc(0x005bb540L, "window/pause 0x5b FUN_005bb540");
        dumpFunc(0x00599510L, "effect FUN_00599510");
        dumpFunc(0x005420b0L, "boss 0x54 FUN_005420b0");
        // callers
        callersOf(0x0066bdaeL);
        callersOf(0x00620130L);
        callersOf(0x005a02e0L);
        callersOf(0x005d7b00L);
        callersOf(0x005bb540L);
        callersOf(0x00599510L);
        callersOf(0x005420b0L);
        close();
    }
}
