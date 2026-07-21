//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss07Main extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0054a890L, "boss07 MAIN ATTACK MACHINE FUN_0054a890 (17KB)");
        dumpFunc(0x0054a340L, "boss07 FUN_0054a340");
        close();
    }
}
