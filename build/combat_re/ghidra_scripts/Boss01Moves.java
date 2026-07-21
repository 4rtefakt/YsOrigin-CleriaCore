//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss01Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005095c0L, "boss01 state0 DECISION FUN_005095c0");
        dumpFunc(0x00509df0L, "boss01 state1 attack FUN_00509df0");
        dumpFunc(0x0050b650L, "boss01 state2 attack FUN_0050b650 (13KB)");
        dumpFunc(0x0050ea90L, "boss01 state3 attack FUN_0050ea90");
        dumpFunc(0x0050f280L, "boss01 state4 attack FUN_0050f280");
        dumpFunc(0x00510730L, "boss01 state5 attack FUN_00510730");
        dumpFunc(0x00511340L, "boss01 state6 attack FUN_00511340 (spawns)");
        dumpFunc(0x005116f0L, "boss01 state7 attack FUN_005116f0");
        dumpFunc(0x00512630L, "boss01 state8 attack FUN_00512630");
        dumpFunc(0x005084a0L, "boss01 collision/hitbox+phase FUN_005084a0");
        dumpFunc(0x00509260L, "boss01 commit-state FUN_00509260");
        close();
    }
}
