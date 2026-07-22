//@category YsOrigin
import ghidra.program.model.address.*;
public class SizeBand398 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005ea5b0L, "render/scenegraph FUN_005ea5b0");
        dumpFunc(0x004550c0L, "object/gfx FUN_004550c0");
        dumpFunc(0x0051d970L, "object/mesh FUN_0051d970");
        dumpFunc(0x004f9680L, "player/combat FUN_004f9680");
        dumpFunc(0x005cd9b0L, "window/audio FUN_005cd9b0");
        dumpFunc(0x00534cc0L, "boss FUN_00534cc0");
        out.println("\n\n########## CALLERS ##########");
        for (long va : new long[]{0x005ea5b0L,0x004550c0L,0x0051d970L,0x004f9680L,0x005cd9b0L,0x00534cc0L}) {
            xrefs(va, "FUN_" + Long.toHexString(va));
        }
        close();
    }
}
