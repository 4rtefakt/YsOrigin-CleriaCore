//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss04Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0052d1c0L, "boss04 BRAIN FUN_0052d1c0");
        dumpFunc(0x005295a0L, "boss04 MAIN ATTACK FUN_005295a0");
        dumpFunc(0x00528570L, "boss04 PART/HITBOX FUN_00528570");
        dumpFunc(0x00528ac0L, "boss04 per-frame FUN_00528ac0");
        dumpFunc(0x0052a2b0L, "boss04 movement FUN_0052a2b0");
        dumpFunc(0x0052c680L, "boss04 FUN_0052c680");
        dumpFunc(0x0052c8f0L, "boss04 DEATH FUN_0052c8f0");
        dumpFunc(0x0052aab0L, "boss04 FUN_0052aab0");
        dumpFunc(0x0052b1e0L, "boss04 FUN_0052b1e0");
        close();
    }
}
