//@category YsOrigin
import ghidra.program.model.address.*;
public class RewardsDump3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00587010L, "pickup/drop spawner FUN_00587010");
        xrefs(0x0069be20L, "orb id table A DAT_0069be20");
        xrefs(0x0069be38L, "orb modulus table DAT_0069be38");
        close();
    }
}
