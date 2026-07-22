//@category YsOrigin
import ghidra.program.model.address.*;
public class BB9b extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00593b90L, "FUN_00593b90 (caller of lockon 596c70)");
        xrefs(0x00593b90L, "FUN_00593b90 callers");
        dumpFunc(0x00478470L, "FUN_00478470 (caller of wander 476f40)");
        xrefs(0x00478470L, "FUN_00478470 callers");
        dumpFunc(0x0042bb10L, "FUN_0042bb10 (caller of camcmd 42c4f0)");
        close();
    }
}
