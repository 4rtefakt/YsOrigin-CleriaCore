//@category YsOrigin
import ghidra.program.model.address.*;
public class BigFn2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x005991f0L, 0x00458970L, 0x00404760L, 0x005f7b10L,
            0x004122d0L, 0x00589390L, 0x004ffab0L
        };
        for (long va : fns) {
            dumpFunc(va, "FUN_" + Long.toHexString(va));
            xrefs(va, "callers FUN_" + Long.toHexString(va));
        }
        close();
    }
}
