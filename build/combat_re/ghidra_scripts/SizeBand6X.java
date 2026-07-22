//@category YsOrigin
import ghidra.program.model.address.*;
public class SizeBand6X extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x005de050L, "FUN_005de050");
        xrefs(0x00411f50L, "FUN_00411f50");
        xrefs(0x00404c60L, "FUN_00404c60");
        xrefs(0x00475e60L, "FUN_00475e60");
        xrefs(0x0040af50L, "FUN_0040af50");
        xrefs(0x004f8d30L, "FUN_004f8d30");
        close();
    }
}
