//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2Trk5 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004517c0L, "GoEffect emitter draw? FUN_004517c0");
        dumpFunc(0x00450880L, "FUN_00450880 (reads registry)");
        close();
    }
}
