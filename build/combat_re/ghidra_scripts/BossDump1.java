//@category YsOrigin
import ghidra.program.model.address.*;

public class BossDump1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // entry handlers (each is the dispatched tick fn = EEX reader per BOSS_LOOKUP)
        dumpFunc(0x00514200L, "boss02 ENTRY FUN_00514200");
        dumpFunc(0x0051ffe0L, "boss03 ENTRY FUN_0051ffe0");
        dumpFunc(0x00527cc0L, "boss04 ENTRY FUN_00527cc0");
        dumpFunc(0x0052d9e0L, "boss05 ENTRY FUN_0052d9e0");
        dumpFunc(0x0053ab20L, "boss06 EEX-STATE FUN_0053ab20");
        dumpFunc(0x0053bbb0L, "boss06 TICK/BRAIN FUN_0053bbb0");
        dumpFunc(0x00545940L, "boss07 ENTRY FUN_00545940");
        dumpFunc(0x00550aa0L, "boss08 ENTRY FUN_00550aa0");
        // shared helpers referenced by the task
        dumpFunc(0x00477f00L, "SHARED hitbox-arm FUN_00477f00");
        dumpFunc(0x00476d70L, "SHARED spawner FUN_00476d70");
        close();
    }
}
