//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2TrkD extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005645b0L, "billboard particle per-frame update FUN_005645b0");
        dumpFunc(0x005ee040L, "blend/render-state setter FUN_005ee040");
        dumpFunc(0x005ed4b0L, "render-state setter FUN_005ed4b0");
        close();
    }
}
