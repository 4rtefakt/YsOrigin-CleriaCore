//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk4 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0074e674L, "EF2 registry DAT_0074e674 (find draw/other readers)");
        xrefs(0x007651c0L, "csprite renderer DAT_007651c0");
        xrefs(0x00730134L, "effect pool cap DAT_00730134 (find pool iterate)");
        xrefs(0x0073012cL, "effect pool count DAT_0073012c");
        close();
    }
}
