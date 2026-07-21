//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2TrkA extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005644c0L, "GoParticle per-frame update vtbl[2] FUN_005644c0");
        dumpFunc(0x00565320L, "GoParticle draw callback FUN_00565320 (quad+blend+tracks)");
        close();
    }
}
