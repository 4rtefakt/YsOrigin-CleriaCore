//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss07Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // 9 co-handlers (always-on + alive)
        dumpFunc(0x00546dc0L, "boss07 co FUN_00546dc0");
        dumpFunc(0x00547120L, "boss07 co FUN_00547120");
        dumpFunc(0x00549020L, "boss07 PART/HITBOX FUN_00549020");
        dumpFunc(0x005472e0L, "boss07 movement FUN_005472e0");
        dumpFunc(0x00549eb0L, "boss07 co FUN_00549eb0");
        dumpFunc(0x00546900L, "boss07 alive FUN_00546900");
        dumpFunc(0x0054ee30L, "boss07 attack2 FUN_0054ee30");
        dumpFunc(0x0054a340L, "boss07 alive FUN_0054a340");
        dumpFunc(0x0054fb20L, "boss07 DEATH FUN_0054fb20");
        close();
    }
}
