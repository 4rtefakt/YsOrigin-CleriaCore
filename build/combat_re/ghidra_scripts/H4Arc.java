//@category YsOrigin
import ghidra.program.model.address.*;
public class H4Arc extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0066fd70L, "trig A FUN_0066fd70 (sin?)");
        dumpFunc(0x00670d30L, "trig B FUN_00670d30 (cos?)");
        dumpFunc(0x0043a040L, "boost-ready flash/SE FUN_0043a040");
        close();
    }
}
