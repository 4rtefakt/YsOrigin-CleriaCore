//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss08Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0055ddc0L, "boss08 BRAIN FUN_0055ddc0");
        dumpFunc(0x00552ef0L, "boss08 GIANT ATTACK BANK FUN_00552ef0 (21KB)");
        dumpFunc(0x00558290L, "boss08 FUN_00558290");
        dumpFunc(0x0055aa80L, "boss08 FUN_0055aa80");
        dumpFunc(0x0055a150L, "boss08 FUN_0055a150");
        dumpFunc(0x00559dd0L, "boss08 case1 FUN_00559dd0");
        dumpFunc(0x0055a0d0L, "boss08 case2 FUN_0055a0d0");
        dumpFunc(0x00559780L, "boss08 swim FUN_00559780");
        dumpFunc(0x0055d3d0L, "boss08 DEATH FUN_0055d3d0");
        dumpFunc(0x0055cb70L, "boss08 FUN_0055cb70");
        dumpFunc(0x0055bf50L, "boss08 FUN_0055bf50");
        close();
    }
}
