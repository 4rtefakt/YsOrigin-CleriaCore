//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.lang.Register;
public class PDmg8 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00593550L, "FUN_00593550 (FireBullet core ctor: kind/tag/update wiring)");
        dumpFunc(0x00593b90L, "FUN_00593b90 (bullet spawner: tag +0x12 write)");
        // FUN_00592d00 full body (tag-0x18 update) — need die-on-hit vs pierce logic + dedup lifetime
        dumpFunc(0x00592d00L, "FUN_00592d00 (tag-0x18 damaging projectile update)");
        close();
    }
}
