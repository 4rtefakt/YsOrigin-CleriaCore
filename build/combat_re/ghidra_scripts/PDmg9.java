//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg9 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00597290L, "FUN_00597290 (tag-0xb kinds 1,2 mover: verify no damage)");
        dumpFunc(0x00464960L, "FUN_00464960 (GenerateLocus handler? sets tag?)");
        close();
    }
}
