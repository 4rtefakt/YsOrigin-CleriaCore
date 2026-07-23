//@category YsOrigin
import ghidra.program.model.address.*;
public class SevFmt2 extends Re2Lib {
    void callers(long va) throws Exception {
        out.println("\n===== CALLERS of FUN_" + Long.toHexString(va) + " =====");
        for (Address a : callersOf(va)) out.println("  " + a + "   [" + fnName(a) + "]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0042d8f0L, "box appender / filler FUN_0042d8f0 (writes descriptor, count++)");
        callers(0x0042d8f0L);
        dumpFunc(0x0042cf00L, "box iterate/parse FUN_0042cf00 (reads base+count)");
        callers(0x0042cf00L);
        dumpFunc(0x0042ce30L, "box reset/loader FUN_0042ce30 (writes base)");
        callers(0x0042ce30L);
        close();
    }
}
