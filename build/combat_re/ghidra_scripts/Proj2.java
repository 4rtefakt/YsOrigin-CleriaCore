//@category YsOrigin
import ghidra.program.model.address.*;
public class Proj2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00449cb0L, "cand ASO FireBullet handler FUN_00449cb0");
        dumpFunc(0x0044ebb0L, "cand handler FUN_0044ebb0");
        dumpFunc(0x00593b90L, "bullet real spawner FUN_00593b90");
        close();
    }
}
