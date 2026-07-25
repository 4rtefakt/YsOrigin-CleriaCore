//@category YsOrigin
// Read the ACTUAL .rdata float values the eye-familiar placement uses. Ghidra's inline
// operand annotations are unreliable (PLAYER.md trap #2) -- read memory directly.
import ghidra.program.model.mem.*;

public class EyeConsts extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Memory m = currentProgram.getMemory();
        long[] as = {0x006bd510L, 0x006bd880L, 0x006bd7c0L, 0x006bd03cL, 0x006bd010L,
                     0x006bd0acL, 0x006bd054L, 0x006bd4a4L, 0x006bd474L, 0x006bd930L,
                     0x006bdc74L, 0x006bd0dcL, 0x006bd438L, 0x006bd0f8L};
        for (long a : as) {
            int v = m.getInt(toAddr(a));
            out.println(String.format("  DAT_%08x = 0x%08x  float %.8g", a, v, Float.intBitsToFloat(v)));
        }
        close();
    }
}
