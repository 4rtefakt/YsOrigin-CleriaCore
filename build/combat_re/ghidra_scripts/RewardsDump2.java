//@category YsOrigin
import ghidra.program.model.address.*;
public class RewardsDump2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00478510L, "death scatter / drop spawner? FUN_00478510 (reads combo ctr)");
        dumpFunc(0x004780e0L, "death handler FUN_004780e0");
        dumpFunc(0x00465170L, "combo-timer manager FUN_00465170 (writes chain ctr 0x76c1a8)");
        dumpFunc(0x00436fa0L, "RNG advance FUN_00436fa0");
        dumpFunc(0x00427c90L, "RNG advance FUN_00427c90");
        xrefs(0x0074e7e0L, "ENEMYINFO payload base DAT_0074e7e0 (find drop-table readers)");
        close();
    }
}
