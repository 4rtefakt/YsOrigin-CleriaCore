//@category YsOrigin
import ghidra.program.model.address.*;
public class XrefBatchX extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00621a00L, "FUN_00621a00");
        xrefs(0x0051d700L, "FUN_0051d700");
        xrefs(0x0053eab0L, "FUN_0053eab0");
        xrefs(0x00552510L, "FUN_00552510");
        xrefs(0x00502cb0L, "FUN_00502cb0");
        xrefs(0x005cf090L, "FUN_005cf090");
        // key callees to identify
        xrefs(0x00450880L, "FUN_00450880 (callee of 53eab0)");
        xrefs(0x006259e0L, "FUN_006259e0 (alloc? callee)");
        xrefs(0x0040f700L, "FUN_0040f700 (callee of 502cb0)");
        xrefs(0x00583a00L, "FUN_00583a00 (callee of 502cb0)");
        xrefs(0x005a1a10L, "FUN_005a1a10 (callee of 502cb0)");
        close();
    }
}
