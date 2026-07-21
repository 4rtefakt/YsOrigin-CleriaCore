//@category YsOrigin
import ghidra.program.model.address.*;
public class EquipEff extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0076a624L, "EQUIP FLAG WORD (accessory+foot-resist bits) - find all consumers");
        xrefs(0x0076bbacL, "Mask-of-Eyes extracted bit - consumers");
        dumpFunc(0x004201d0L, "FUN_004201d0 weapon-tier setter (SetWeaponLevel worker)");
        close();
    }
}
