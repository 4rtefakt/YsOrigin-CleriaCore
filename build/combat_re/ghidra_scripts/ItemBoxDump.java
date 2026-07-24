//@category YsOrigin
import ghidra.program.model.address.*;
public class ItemBoxDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005781f0L, "Window_Treasure box builder FUN_005781f0 (x,y,itemId) — geometry");
        dumpFunc(0x00574410L, "0xd5 Window_Treasure handler FUN_00574410");
        close();
    }
}
