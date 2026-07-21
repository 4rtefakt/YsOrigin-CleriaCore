//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss05Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0052e2c0L, "boss05 GIANT BRAIN FUN_0052e2c0 (27KB)");
        dumpFunc(0x0052d980L, "boss05 SUMMON SPAWN helper FUN_0052d980");
        dumpFunc(0x00538820L, "boss05 attack driver FUN_00538820");
        dumpFunc(0x00537160L, "boss05 attack driver FUN_00537160");
        dumpFunc(0x00536170L, "boss05 FUN_00536170");
        dumpFunc(0x00536a30L, "boss05 FUN_00536a30");
        dumpFunc(0x0053a760L, "boss05 gate-clear FUN_0053a760");
        close();
    }
}
