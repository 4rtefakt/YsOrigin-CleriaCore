//@category YsOrigin
import ghidra.program.model.address.*;
public class MathConsts extends Re2Lib {
    void f(long va, String tag) throws Exception {
        Address a = toAddr(va);
        int bits = currentProgram.getMemory().getInt(a);
        out.println(String.format("  %08x  %s = %.9g  (0x%08x)", va, tag, (double)Float.intBitsToFloat(bits), bits));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        f(0x006bda38L, "_DAT_006bda38 rad->LUTidx scale");
        f(0x006bda38L+4, "  next");
        out.println("  -- DAT_0068f350 LUT samples (256-entry) --");
        for (int i : new int[]{0,1,32,64,96,128,192,255}) f(0x0068f350L + i*4L, "TBL["+i+"]");
        close();
    }
}
