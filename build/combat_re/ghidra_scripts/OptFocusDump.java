//@category YsOrigin
import ghidra.program.model.address.*;
public class OptFocusDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005bae20L, "options focus/highlight FUN_005bae20");
        dumpFunc(0x005b9b30L, "options builder FUN_005b9b30");
        dumpFunc(0x005a39a0L, "row-plate csprite diffuse FUN_005a39a0");
        close();
    }
}
