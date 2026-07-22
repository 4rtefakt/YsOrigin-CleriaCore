//@category YsOrigin
import ghidra.program.model.address.*;
public class SixSweepC extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00443150L, "FUN_00443150 object/resource 0x44");
        xrefs(0x0057b4b0L, "FUN_0057b4b0 window/arena 0x57");
        xrefs(0x004ff930L, "FUN_004ff930 player/effect 0x50");
        xrefs(0x004515f0L, "FUN_004515f0 object/GoEffect 0x45");
        xrefs(0x005cd270L, "FUN_005cd270 window/audio 0x5c");
        xrefs(0x005902d0L, "FUN_005902d0 effect/projectile 0x59");
        // resolve identity of callees / siblings
        xrefs(0x004444f0L, "FUN_004444f0 callee of 443150");
        xrefs(0x005e86c0L, "FUN_005e86c0 audio play callee");
        close();
    }
}
