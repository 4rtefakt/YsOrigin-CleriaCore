//@category YsOrigin
import ghidra.program.model.address.*;
public class NightBigCallers extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0065e916L, "FUN_0065e916 callers");
        xrefs(0x0051aec0L, "FUN_0051aec0 callers");
        xrefs(0x0044b7a0L, "FUN_0044b7a0 callers");
        xrefs(0x0058c650L, "FUN_0058c650 callers");
        xrefs(0x005d9d40L, "FUN_005d9d40 callers");
        xrefs(0x004a2f90L, "FUN_004a2f90 callers");
        // identify key callees
        xrefs(0x00593550L, "FUN_00593550 (callee of 4a2f90) callers");
        xrefs(0x005df8f0L, "FUN_005df8f0 (callee of 58c650) callers");
        close();
    }
}
