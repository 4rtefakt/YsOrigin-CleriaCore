//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn9Rd extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00430140L, "svld per-slot metadata reader FUN_00430140");
        dumpFunc(0x005b3700L, "svld cursor-state buffer builder FUN_005b3700");
        close();
    }
}
