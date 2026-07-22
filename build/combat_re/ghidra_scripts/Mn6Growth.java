//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn6Growth extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Raval cost mapper (SetRavalCostToFlag opcode 0xb0)
        dumpFunc(0x005691fdL, "SetRavalCostToFlag FUN_005691fd (cost tables 0x56e800/0x56e830 -> 0x76bc84)");
        // Raval level setter (SetRavalLevel opcode 0x83) and weapon-tier setter
        dumpFunc(0x00420200L, "SetRavalLevel FUN_00420200 (growth level setter)");
        dumpFunc(0x004201d0L, "weapon-tier setter FUN_004201d0");
        // who reads the growth cost flag (who displays it)
        xrefs(0x0076bc84L, "growth cost flag 0x76bc84 (find readers/display)");
        // who reads/writes the story-progress bitset used by growth menus
        xrefs(0x0076bc80L, "story-progress bitset 0x76bc80 (IsXXX/SetXXX)");
        // Raval level objects (armor/boots ids?)
        xrefs(0x0076bb7cL, "0x76bb7c (Raval slot A)");
        xrefs(0x0076bb80L, "0x76bb80 (Raval slot B)");
        close();
    }
}
