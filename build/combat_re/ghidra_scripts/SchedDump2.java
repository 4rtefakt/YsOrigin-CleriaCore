//@category YsOrigin
import ghidra.program.model.address.*;
public class SchedDump2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00479a70L, "ACTION-CMD player FUN_00479a70 (arg = +0x710 action id -> writes +0x6f8?)");
        dumpFunc(0x004758f0L, "ACTION-VARIANT player FUN_004758f0 (arg = +0x708 variant)");
        dumpFunc(0x00475a80L, "phase1 helper FUN_00475a80 (target, dist)");
        dumpFunc(0x004758a0L, "phase1 helper FUN_004758a0");
        dumpFunc(0x00476900L, "helper FUN_00476900 (arg = +0x748)");
        close();
    }
}
