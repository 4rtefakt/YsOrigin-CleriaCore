//@category YsOrigin
// FUN_00450410(effectId, pos, ROT, scale, ...) — orientation is a per-SPAWN rotation, so
// the field that lays the ward's quads flat is whatever its locate op passes as `rot`.
// FUN_0056ef90 = the 0x117/0x11a Eft_LocatePos handler (per src/cleria/xso.cpp).
// FUN_00593550 = the FireBullet spawn, for the projectile case.
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;

public class Ef2Orient3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0056ef90L, "Eft_LocatePos handler FUN_0056ef90 (op 0x117/0x11a)");
        out.println("\n===== CALLERS OF FUN_0056ef90 =====");
        for (Address c : callersOf(0x0056ef90L)) out.println("  " + c + "   [" + fnName(c) + "]");
        close();
    }
}
