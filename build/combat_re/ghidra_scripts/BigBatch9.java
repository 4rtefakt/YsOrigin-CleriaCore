//@category YsOrigin
import ghidra.program.model.address.*;
public class BigBatch9 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0058e2d0L, "FUN_0058e2d0");
        xrefs(0x0058e2d0L, "FUN_0058e2d0 callers");
        dumpFunc(0x005ed000L, "FUN_005ed000");
        xrefs(0x005ed000L, "FUN_005ed000 callers");
        dumpFunc(0x00476f40L, "FUN_00476f40");
        xrefs(0x00476f40L, "FUN_00476f40 callers");
        dumpFunc(0x0040ccf0L, "FUN_0040ccf0");
        xrefs(0x0040ccf0L, "FUN_0040ccf0 callers");
        dumpFunc(0x005f7230L, "FUN_005f7230");
        xrefs(0x005f7230L, "FUN_005f7230 callers");
        dumpFunc(0x00454310L, "FUN_00454310");
        xrefs(0x00454310L, "FUN_00454310 callers");
        dumpFunc(0x0042c4f0L, "FUN_0042c4f0");
        xrefs(0x0042c4f0L, "FUN_0042c4f0 callers");
        dumpFunc(0x00596c70L, "FUN_00596c70");
        xrefs(0x00596c70L, "FUN_00596c70 callers");
        dumpFunc(0x00403e80L, "FUN_00403e80");
        xrefs(0x00403e80L, "FUN_00403e80 callers");
        close();
    }
}
