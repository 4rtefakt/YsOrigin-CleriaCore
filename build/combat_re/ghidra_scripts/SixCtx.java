//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;
public class SixCtx extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // callers of ambiguous ones (identify subsystem)
        dumpFunc(0x0043fe00L, "caller of timecode fn FUN_0043fe00");
        dumpFunc(0x004281c0L, "caller of record-tracker FUN_004281c0");
        dumpFunc(0x00465170L, "caller of movement-sel FUN_00465170");
        // key callees
        dumpFunc(0x005a22e0L, "record helper FUN_005a22e0");
        dumpFunc(0x005caf40L, "caller of slot-init FUN_005caf40");
        // data tables / globals
        xrefs(0x0068b278L, "emotion/mover duration table DAT_0068b278");
        xrefs(0x0076b91cL, "movement anim availability table DAT_0076b91c");
        xrefs(0x0074e67cL, "record-tracker source DAT_0074e67c");
        close();
    }
}
