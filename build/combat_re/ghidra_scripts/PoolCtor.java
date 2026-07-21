//@category YsOrigin
import ghidra.program.model.address.*;
public class PoolCtor extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00583a00L, "sprite ctor (pool2/9) FUN_00583a00");
        dumpFunc(0x005710d0L, "pool3 spawner FUN_005710d0");
        dumpFunc(0x0044fdd0L, "pool6 spawner FUN_0044fdd0");
        dumpFunc(0x00443150L, "pool12 spawner FUN_00443150");
        dumpFunc(0x00443240L, "pool13 spawner FUN_00443240");
        dumpFunc(0x00573e60L, "pool14 spawner FUN_00573e60");
        close();
    }
}
