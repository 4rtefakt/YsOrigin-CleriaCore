//@category YsOrigin
import ghidra.program.model.address.*;
public class PoolXref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] freeg = {
            0x00730094L, // pool1 ACTOR
            0x007300b8L, // pool2 512 0x230
            0x007300dcL, // pool3 32  0x1b0
            0x00730100L, // pool4 8   0x1e0
            0x00730220L, // pool5 512 0x4d0 RENDER
            0x00730124L, // pool6 2048 0x2d0
            0x007301fcL, // pool7 32  0x260
            0x00730148L, // pool8 256 0x2c0
            0x00730244L, // pool9 2048 0x230
            0x007302b0L, // pool10 1 0x390
            0x0073016cL, // pool11 2 0x2cd0
            0x0073028cL, // pool12 1 0x140
            0x00730268L, // pool13 1 0x200
            0x00730190L, // pool14 1 0x500
            0x007301b4L, // pool15 1 0x140
            0x007301d8L, // pool16 256 0x28
        };
        for (long g : freeg) xrefs(g, String.format("free-stack global 0x%08x", g));
        close();
    }
}
