//@category YsOrigin
import ghidra.program.model.address.*;
public class PoolAlloc extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0044b450L, "pool2 alloc? FUN_0044b450 (512x0x230)");
        dumpFunc(0x00458fc0L, "pool3 alloc? FUN_00458fc0 (32x0x1b0)");
        dumpFunc(0x00460d20L, "pool4 alloc? FUN_00460d20 (8x0x1e0)");
        dumpFunc(0x004308a0L, "pool5 RENDER alloc? FUN_004308a0 (512x0x4d0)");
        dumpFunc(0x0044b520L, "pool6 alloc? FUN_0044b520 (2048x0x2d0)");
        dumpFunc(0x0041fc70L, "pool7 alloc? FUN_0041fc70 (32x0x260)");
        dumpFunc(0x0042cdb0L, "pool8 alloc? FUN_0042cdb0 (256x0x2c0)");
        dumpFunc(0x0040b2e0L, "pool9 alloc? FUN_0040b2e0 (2048x0x230)");
        dumpFunc(0x00430a30L, "pool10 alloc? FUN_00430a30 (1x0x390)");
        dumpFunc(0x00427600L, "pool11 alloc? FUN_00427600 (2x0x2cd0)");
        dumpFunc(0x00442180L, "pool12 alloc? FUN_00442180 (1x0x140)");
        dumpFunc(0x004420e0L, "pool13 alloc? FUN_004420e0 (1x0x200)");
        dumpFunc(0x00573c70L, "pool14 alloc? FUN_00573c70 (1x0x500)");
        dumpFunc(0x00575540L, "pool15 alloc? FUN_00575540 (1x0x140)");
        dumpFunc(0x004274f0L, "pool16 alloc? FUN_004274f0 (256x0x28)");
        close();
    }
}
