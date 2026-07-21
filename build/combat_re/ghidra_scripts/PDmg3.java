//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The other two bullet-side FUN_004faf20 callers — the real generic damage?
        dumpFunc(0x00592d00L, "FUN_00592d00 (bullet fn -> FUN_004faf20)");
        dumpFunc(0x00599a30L, "FUN_00599a30 (per-kind mover -> FUN_004faf20)");
        // The shared attacker->victim scan
        dumpFunc(0x004fed50L, "FUN_004fed50 (outgoing attack scan)");
        // Registry ADD counterpart: who writes/increments the bucket count and sets +0x2c0
        xrefs(0x00767f10L, "DAT_00767f10 (registry array-base bucket 0)");
        close();
    }
}
