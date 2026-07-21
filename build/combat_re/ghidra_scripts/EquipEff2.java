//@category YsOrigin
import ghidra.program.model.address.*;
public class EquipEff2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00462550L, "FUN_00462550 (foot bits 0x2000/0x4000000 + Blue Necklace 0x20000)");
        dumpFunc(0x00460dc0L, "FUN_00460dc0 survival/revive gate (Daybreak Talisman 0x1000)");
        dumpFunc(0x004fd350L, "FUN_004fd350 physical hurt applier (foot resist 0x200)");
        dumpFunc(0x004a12a0L, "FUN_004a12a0 enemy AI handler M560 (Silver Chimes 0x8000)");
        dumpFunc(0x0045a510L, "FUN_0045a510 anim/event update (foot bits 0x1000000/0x2000000 + Earth Dragon 0x4000)");
        close();
    }
}
