//@category YsOrigin
import ghidra.program.model.address.*;
public class SevFmt3 extends Re2Lib {
    void callers(long va) throws Exception {
        out.println("\n===== CALLERS of FUN_" + Long.toHexString(va) + " =====");
        for (Address a : callersOf(va)) out.println("  " + a + "   [" + fnName(a) + "]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00433db0L, "SEV parser? FUN_00433db0 (contains 'SEV' bytes @0x00433e23)");
        callers(0x00433db0L);
        dumpFunc(0x00433a10L, "other appender caller FUN_00433a10");
        callers(0x00433a10L);
        close();
    }
}
