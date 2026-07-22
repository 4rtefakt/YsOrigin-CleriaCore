//@category YsOrigin
import ghidra.program.model.address.*;
public class SixXr extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x005fab70L, "FUN_005fab70 (all refs)");
        xrefs(0x00565fc0L, "FUN_00565fc0 (all refs)");
        xrefs(0x005d0f70L, "FUN_005d0f70 (all refs)");
        xrefs(0x005d1150L, "FUN_005d1150 (all refs)");
        // callee identity - just names + size
        long[] callees = {0x005edf40L,0x0044f2e0L,0x00444e60L,0x00416190L,0x005d1cb0L,0x005d26e0L,0x00625584L};
        for (long c : callees) {
            Address a = toAddr(c);
            out.println("\nCALLEE FUN_" + Long.toHexString(c) + " -> " + fnName(a));
        }
        close();
    }
}
