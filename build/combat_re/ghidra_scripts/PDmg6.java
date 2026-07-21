//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00592af0L, "FUN_00592af0 (bullet fn -> FUN_004fbb40 apply)");
        xrefs(0x00592af0L, "callers of FUN_00592af0");
        dumpFunc(0x004fba30L, "FUN_004fba30 (overlap+apply resolver)");
        xrefs(0x004fba30L, "callers of FUN_004fba30");
        xrefs(0x00592d00L, "callers of FUN_00592d00");
        close();
    }
}
