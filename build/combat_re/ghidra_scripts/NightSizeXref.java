//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSizeXref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00600970L, "FUN_00600970 WAV parser");
        xrefs(0x00453e90L, "FUN_00453e90 sl2 loader");
        xrefs(0x005d51d0L, "FUN_005d51d0 preset reg");
        xrefs(0x0040db00L, "FUN_0040db00 ini writer");
        xrefs(0x005e75e0L, "FUN_005e75e0 introsort");
        xrefs(0x0053f820L, "FUN_0053f820 boss 0x53");
        close();
    }
}
