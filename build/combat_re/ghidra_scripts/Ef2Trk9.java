//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk9 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00564c10L, "GoParticle main update vtbl[5] FUN_00564c10 (17-track eval)");
        dumpFunc(0x00564a60L, "render node FUN_00564a60");
        close();
    }
}
