//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg7 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0058fcb0L, "FUN_0058fcb0 (tick that drives FUN_00592d00 damage)");
        xrefs(0x0058fcb0L, "callers of FUN_0058fcb0 / vtable refs");
        // What creates this object? scan the vtable area / ctor. Find its FireBullet linkage.
        xrefs(0x00592d00L, "xref FUN_00592d00 (all)");
        // The ASO bullet's other movers — spot-check for any FUN_004fbb40 / FUN_00592d00 / damage
        xrefs(0x00597290L, "callers of FUN_00597290 (kind1,2 mover)");
        close();
    }
}
