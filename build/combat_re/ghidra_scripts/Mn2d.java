//@category YsOrigin
import ghidra.program.model.address.*;

public class Mn2d extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0057d2a0L, "new-game setup popups (Type/Diff/SP) FUN_0057d2a0");
        close();
    }
}
