//@category YsOrigin
import ghidra.program.model.address.*;
public class Big7Attr extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0052c680L, "FUN_0052c680 (caller of 0052b6e0)");
        xrefs(0x0052e2c0L, "FUN_0052e2c0 Pictimos brain");
        xrefs(0x00434d00L, "FUN_00434d00 actor lookup");
        xrefs(0x00450880L, "FUN_00450880 effect-volume spawn");
        close();
    }
}
