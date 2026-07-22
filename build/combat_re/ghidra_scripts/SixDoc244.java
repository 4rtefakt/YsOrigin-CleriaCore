//@category YsOrigin
import ghidra.program.model.address.*;
public class SixDoc244 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005fc5b0L, "render/D3D FUN_005fc5b0");
        callersOf(0x005fc5b0L);
        dumpFunc(0x004580c0L, "object/gfx FUN_004580c0");
        callersOf(0x004580c0L);
        dumpFunc(0x0053b780L, "effect/boss FUN_0053b780");
        callersOf(0x0053b780L);
        dumpFunc(0x00410280L, "core/boot FUN_00410280");
        callersOf(0x00410280L);
        dumpFunc(0x00570cf0L, "window/text FUN_00570cf0");
        callersOf(0x00570cf0L);
        dumpFunc(0x005505d0L, "boss FUN_005505d0");
        callersOf(0x005505d0L);
        close();
    }
}
