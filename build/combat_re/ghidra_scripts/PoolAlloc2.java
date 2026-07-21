//@category YsOrigin
import ghidra.program.model.address.*;
public class PoolAlloc2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004523e0L, "pool2 ALLOC FUN_004523e0 (512x0x230)");
        dumpFunc(0x0045e1b0L, "pool3 ALLOC FUN_0045e1b0 (32x0x1b0)");
        dumpFunc(0x00464960L, "pool4 ALLOC FUN_00464960 (8x0x1e0)");
        dumpFunc(0x00435080L, "pool5 RENDER ALLOC FUN_00435080 (512x0x4d0)");
        dumpFunc(0x004525a0L, "pool6 ALLOC FUN_004525a0 (2048x0x2d0)");
        dumpFunc(0x00421640L, "pool7 ALLOC FUN_00421640 (32x0x260)");
        dumpFunc(0x0042de00L, "pool8 ALLOC FUN_0042de00 (256x0x2c0)");
        dumpFunc(0x0040f700L, "pool9 ALLOC FUN_0040f700 (2048x0x230)");
        dumpFunc(0x00575690L, "pool10 reader FUN_00575690 (1x0x390)");
        dumpFunc(0x00429ad0L, "pool11 ALLOC FUN_00429ad0 (2x0x2cd0)");
        dumpFunc(0x004444f0L, "pool12 ALLOC FUN_004444f0 (1x0x140)");
        dumpFunc(0x004443a0L, "pool13 ALLOC FUN_004443a0 (1x0x200)");
        dumpFunc(0x005752c0L, "pool14 ALLOC FUN_005752c0 (1x0x500)");
        dumpFunc(0x00577ce0L, "pool15 ALLOC FUN_00577ce0 (1x0x140)");
        dumpFunc(0x004550c0L, "pool16 ALLOC FUN_004550c0 (256x0x28)");
        close();
    }
}
