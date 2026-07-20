//@category YsOrigin
import ghidra.program.model.address.*;
public class Proj1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00593550L, "FireBullet core FUN_00593550");
        xrefs(0x00593550L, "callers of FUN_00593550");
        close();
    }
}
