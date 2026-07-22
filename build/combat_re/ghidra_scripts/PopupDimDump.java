//@category YsOrigin
import ghidra.program.model.address.*;
public class PopupDimDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00578f80L, "go_wnddialog draw FUN_00578f80");
        dumpFunc(0x005784b0L, "popup open/caption FUN_005784b0");
        close();
    }
}
