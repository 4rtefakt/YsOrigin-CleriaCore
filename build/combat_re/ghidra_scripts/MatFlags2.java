//@category YsOrigin
import ghidra.program.model.address.*;
public class MatFlags2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005fa780L, "FUN_005fa780 (two 0x1000 tests)");
        dumpFunc(0x005f8de0L, "FUN_005f8de0 (AND 0x1000)");
        dumpFunc(0x005fb0e0L, "FUN_005fb0e0 (TEST 0x1000)");
        dumpFunc(0x005faca0L, "PASS CLASSIFIER FUN_005faca0 (MATERIAL.md)");
        xrefs(0x005f92b0L, "draw FUN_005f92b0 callers");
        close();
    }
}
