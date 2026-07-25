//@category YsOrigin
// Hugo's projectile art: FireBullet arg[1]=130 -> EFTLIST 130 -> pc\pc02\PC2_DEF_ATTACK.eft.
// Open questions: what arg[12] (the "scale sentinel") does, and whether the EF2 emitter
// sx/sy is an emission AREA or a sprite size on the projectile path.
//   FUN_0044c250 = the B2 track evaluator
//   FUN_00565320 = the GoParticle draw
//   FUN_00593550 = the FireBullet spawn (21 args, from ASOVM)
//   FUN_00450410 = the effect-spawn entry the bullet id resolves through
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class BulletArt extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0044c250L, "B2 track evaluator FUN_0044c250");
        dumpFunc(0x00565320L, "GoParticle draw FUN_00565320");
        dumpFunc(0x00593550L, "FireBullet spawn FUN_00593550");
        out.println("\n===== CALLERS OF FUN_00565320 =====");
        for (Address c : callersOf(0x00565320L)) out.println("  " + c + "   [" + fnName(c) + "]");
        out.println("\n===== CALLERS OF FUN_0044c250 =====");
        for (Address c : callersOf(0x0044c250L)) out.println("  " + c + "   [" + fnName(c) + "]");
        close();
    }
}
