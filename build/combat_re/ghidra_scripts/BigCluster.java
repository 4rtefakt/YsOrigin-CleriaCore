//@category YsOrigin
import ghidra.program.model.address.*;
public class BigCluster extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00497bb0L, "T1 suspected enemy brain FUN_00497bb0");
        xrefs(0x00497bb0L, "FUN_00497bb0");
        dumpFunc(0x0046a3e0L, "T2 suspected effect spawner FUN_0046a3e0");
        xrefs(0x0046a3e0L, "FUN_0046a3e0");
        dumpFunc(0x0046d8c0L, "T3 suspected attack setup FUN_0046d8c0");
        xrefs(0x0046d8c0L, "FUN_0046d8c0");
        close();
    }
}
