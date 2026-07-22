//@category YsOrigin
import ghidra.program.model.address.*;
public class NB1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        one(0x0065334bL, "A) CRT triage FUN_0065334b");
        one(0x00584270L, "B1) effect FUN_00584270");
        one(0x00457410L, "B2) actor 0x45 FUN_00457410");
        one(0x005b4e30L, "B3) window/saveload 0x5b FUN_005b4e30");
        one(0x005de7f0L, "B4) render 0x5d FUN_005de7f0");
        one(0x00552c30L, "B5) boss/enemy 0x55 FUN_00552c30");
        one(0x0057a410L, "B6) window/text 0x57 FUN_0057a410");
        close();
    }
    void one(long va, String tag) {
        dumpFunc(va, tag);
        out.println("\n---- CALLERS of 0x" + Long.toHexString(va) + " ----");
        for (Address a : callersOf(va)) out.println("  " + a + "  [" + fnName(a) + "]");
    }
}
