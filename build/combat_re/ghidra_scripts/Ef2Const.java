//@category YsOrigin
import ghidra.program.model.mem.*;

public class Ef2Const extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] as = {0x006bd304L, 0x006bd948L, 0x006bd880L, 0x006bd438L, 0x006bd304L};
        Memory m = currentProgram.getMemory();
        for (long a : as) {
            int v = m.getInt(toAddr(a));
            out.println(String.format("  DAT_%08x = 0x%08x  float %.8g", a, v, Float.intBitsToFloat(v)));
        }
        close();
    }
}
