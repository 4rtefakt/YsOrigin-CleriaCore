//@category YsOrigin
import ghidra.program.model.address.*;
public class NightXrefs extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x005d15f0L, "FUN_005d15f0");
        xrefs(0x005df9b0L, "FUN_005df9b0");
        xrefs(0x005d8ab0L, "FUN_005d8ab0");
        xrefs(0x005a1380L, "FUN_005a1380");
        xrefs(0x00583b50L, "FUN_00583b50");
        close();
    }
}
