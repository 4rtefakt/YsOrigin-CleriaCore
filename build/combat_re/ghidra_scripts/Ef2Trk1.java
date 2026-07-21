//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00451e60L, "effect update A FUN_00451e60");
        dumpFunc(0x00451f60L, "effect update B FUN_00451f60 (rate 0x1f0 / phase 0x204)");
        dumpFunc(0x00451460L, "instance ctor FUN_00451460");
        close();
    }
}
