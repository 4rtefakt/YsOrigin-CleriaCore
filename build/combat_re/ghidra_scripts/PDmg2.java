//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // WHO calls the damage applier FUN_004faf20 — the central hazard/projectile scan?
        xrefs(0x004faf20L, "callers of FUN_004faf20 (damage applier)");
        // The DAT_00767f14 array manager (hazard registry?)
        dumpFunc(0x0044b5d0L, "FUN_0044b5d0 (DAT_00767f14 array manager)");
        // The damage number formula
        dumpFunc(0x004fa550L, "FUN_004fa550 (damage number formula)");
        // The bullet update — need parry branch + any hazard registration for generic kinds
        dumpFunc(0x00594230L, "bullet update FUN_00594230 (parry branch + generic)");
        close();
    }
}
