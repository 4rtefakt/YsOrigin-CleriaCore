//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn4Equip extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005b0860L, "status panel draw (6 stat rows) FUN_005b0860");
        dumpFunc(0x005ab0d0L, "item/equip content builder FUN_005ab0d0");
        close();
    }
}
