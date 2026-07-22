//@category YsOrigin
import ghidra.program.model.address.*;
public class NightBatch extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00622680L, "CRT triage FUN_00622680");
        callersOf(0x00622680L);
        dumpFunc(0x005d15f0L, "render FUN_005d15f0");
        callersOf(0x005d15f0L);
        dumpFunc(0x005df9b0L, "draw-enqueue FUN_005df9b0");
        callersOf(0x005df9b0L);
        dumpFunc(0x005d8ab0L, "render FUN_005d8ab0");
        callersOf(0x005d8ab0L);
        dumpFunc(0x005a1380L, "window FUN_005a1380");
        callersOf(0x005a1380L);
        dumpFunc(0x00583b50L, "effect FUN_00583b50");
        callersOf(0x00583b50L);
        close();
    }
}
