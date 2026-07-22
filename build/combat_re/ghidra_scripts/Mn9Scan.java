//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn9Scan extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005b3970L, "svld record populator? FUN_005b3970");
        close();
    }
}
