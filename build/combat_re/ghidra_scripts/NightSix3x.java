//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSix3x extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x005ee980L, "FUN_005ee980");
        xrefs(0x00527b50L, "FUN_00527b50");
        xrefs(0x005ec580L, "FUN_005ec580");
        xrefs(0x00577fc0L, "FUN_00577fc0");
        xrefs(0x005404a0L, "FUN_005404a0");
        xrefs(0x005cf420L, "FUN_005cf420");
        close();
    }
}
