//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk8 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00564110L, "GoParticle update vtbl[1] FUN_00564110");
        dumpFunc(0x00565170L, "GoParticle draw? vtbl[6] FUN_00565170");
        dumpFunc(0x0045e750L, "particle texture/uv/color setup FUN_0045e750");
        close();
    }
}
