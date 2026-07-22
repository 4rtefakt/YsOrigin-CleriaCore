//@category YsOrigin
import ghidra.program.model.address.*;
public class SixSweepB extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00443150L, "FUN_00443150 object/resource 0x44");
        callersOf(0x00443150L);
        dumpFunc(0x0057b4b0L, "FUN_0057b4b0 window/arena 0x57");
        callersOf(0x0057b4b0L);
        dumpFunc(0x004ff930L, "FUN_004ff930 player/effect 0x50");
        callersOf(0x004ff930L);
        dumpFunc(0x004515f0L, "FUN_004515f0 object/GoEffect 0x45");
        callersOf(0x004515f0L);
        dumpFunc(0x005cd270L, "FUN_005cd270 window/audio 0x5c");
        callersOf(0x005cd270L);
        dumpFunc(0x005902d0L, "FUN_005902d0 effect/projectile 0x59");
        callersOf(0x005902d0L);
        close();
    }
}
