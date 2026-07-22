//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSizeTri extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00600970L, "A: gap fn FUN_00600970 (vorbis/zlib gap)");
        callersOf(0x00600970L);
        dumpFunc(0x00453e90L, "B1: FUN_00453e90 object/gfx 0x45");
        callersOf(0x00453e90L);
        dumpFunc(0x005d51d0L, "B2: FUN_005d51d0 render/scenegraph 0x5d");
        callersOf(0x005d51d0L);
        dumpFunc(0x0040db00L, "B3: FUN_0040db00 core/boot 0x40");
        callersOf(0x0040db00L);
        dumpFunc(0x005e75e0L, "B4: FUN_005e75e0 render/scenegraph 0x5e");
        callersOf(0x005e75e0L);
        dumpFunc(0x0053f820L, "B5: FUN_0053f820 boss 0x53 near Zava");
        callersOf(0x0053f820L);
        close();
    }
}
