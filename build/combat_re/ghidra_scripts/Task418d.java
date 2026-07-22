//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class Task418d extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0051ea30L, "advance FUN_0051ea30");
        dumpFunc(0x005f5c80L, "playMotion FUN_005f5c80");
        dumpFunc(0x00514200L, "owner-of-51db70-vector FUN_00514200");
        xrefs(0x0076a2c4L, "vector-begin DAT_0076a2c4");
        close();
    }
}
