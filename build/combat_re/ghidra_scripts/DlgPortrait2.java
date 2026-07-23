//@category YsOrigin
import ghidra.program.model.address.*;
public class DlgPortrait2 extends Re2Lib {
    void floats(long base, int n, String tag) throws Exception {
        out.println("\n===== FLOATS @0x" + Long.toHexString(base) + "  (" + tag + ") =====");
        for (int i = 0; i < n; i++) {
            Address a = toAddr(base + i*4L);
            int raw = currentProgram.getMemory().getInt(a);
            float f = Float.intBitsToFloat(raw);
            out.printf("  [%2d] +0x%02x  %-14s  (0x%08x)%n", i, i*4, Float.toString(f), raw);
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Portrait quad (4 verts x 7 floats = 0x1c) drawn for the face
        floats(0x00732a80L, 0x1c, "PORTRAIT face quad DAT_00732a80 (4 verts)");
        // Frame strip (8 verts x 7 floats = 0x38) for comparison
        floats(0x00732af0L, 0x38, "FRAME strip DAT_00732af0 (8 verts)");
        // Box-open animation quad (0x20 floats)
        floats(0x00732bd0L, 0x20, "BOX-OPEN quad DAT_00732bd0");
        // Face-atlas / CHR-pack resolver system
        dumpFunc(0x00444850L, "face-atlas GET resource FUN_00444850");
        dumpFunc(0x00444dc0L, "face-index -> cell FUN_00444dc0");
        dumpFunc(0x00444f70L, "face RT create FUN_00444f70");
        dumpFunc(0x00444fe0L, "face cell blit FUN_00444fe0");
        close();
    }
}
