//@category YsOrigin
import ghidra.program.model.address.*;
import java.util.*;
public class BigCluster2 extends Re2Lib {
    void climb(long va, String tag) {
        out.println("\n===== CALLERS OF " + tag + " @0x" + Long.toHexString(va) + " =====");
        for (Address a : callersOf(va)) out.println("  " + a + "   [" + fnName(a) + "]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00499c90L, "M540 handler (T1 caller) FUN_00499c90");
        dumpFunc(0x00498830L, "M540 listed brain FUN_00498830");
        dumpFunc(0x00465170L, "T2 caller FUN_00465170");
        climb(0x00465170L, "FUN_00465170");
        dumpFunc(0x0046ea00L, "T3 caller FUN_0046ea00");
        climb(0x0046ea00L, "FUN_0046ea00");
        close();
    }
}
