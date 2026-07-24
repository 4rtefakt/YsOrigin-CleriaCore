//@category YsOrigin
import ghidra.program.model.address.*;
public class ManualDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005c17a0L, "Manual viewer dispatcher FUN_005c17a0 (subId 0x16)");
        dumpFunc(0x005c0f50L, "Manual builder FUN_005c0f50");
        dumpFunc(0x00568ad0L, "0x92 SetManualEnableIndex handler FUN_00568ad0");
        xrefs(0x0076b8d8L, "manual enable-byte array 0x76b8d8 (find READERS = display trigger)");
        close();
    }
}
