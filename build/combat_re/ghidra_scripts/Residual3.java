//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class Residual3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // The ONLY caller of the tag-0x18 ctor FUN_00591f60 -> identify it
        dumpFunc(0x00449ed0L, "FUN_00449ed0 = spawns tag-0x18 projectile (calls FUN_00591f60)");
        out.println("\n===== CALLERS of FUN_00449ed0 =====");
        for (Address a : callersOf(0x00449ed0L))
            out.println("  " + a + "   [" + fnName(a) + "]");

        // Is FUN_00449ed0 an ASO/anime opcode? dump its sibling ASO tag-setter too
        dumpFunc(0x004495b0L, "FUN_004495b0 = sibling (also sets +0x12 tag)");
        out.println("\n===== CALLERS of FUN_004495b0 =====");
        for (Address a : callersOf(0x004495b0L))
            out.println("  " + a + "   [" + fnName(a) + "]");

        // FUN_004a7030 context: is param_1 a GoActor? (the float-threshold +0x9cc reader)
        // already dumped in pass2; here get its callers to see if it's an enemy brain
        out.println("\n===== CALLERS of FUN_004a7030 (+0x9cc float-cmp reader) =====");
        for (Address a : callersOf(0x004a7030L))
            out.println("  " + a + "   [" + fnName(a) + "]");

        // The enemy->player melee resolvers, to confirm the melee damage route to player
        out.println("\n===== CALLERS of FUN_00504b00 / FUN_00505640 (melee resolvers -> FUN_004fd350) =====");
        for (Address a : callersOf(0x00504b00L)) out.println("  504b00 <- " + a + "   [" + fnName(a) + "]");
        for (Address a : callersOf(0x00505640L)) out.println("  505640 <- " + a + "   [" + fnName(a) + "]");

        // Who calls FUN_004fed50 (the outgoing attack scan) - player + enemies?
        out.println("\n===== CALLERS of FUN_004fed50 (outgoing attack-box scan) =====");
        for (Address a : callersOf(0x004fed50L)) out.println("  " + a + "   [" + fnName(a) + "]");

        close();
    }
}
