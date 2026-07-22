//@category YsOrigin
import ghidra.program.model.address.*;

public class MnOpt3 extends Re2Lib {
    void f(long va, String tag) {
        try {
            int raw = currentProgram.getMemory().getInt(toAddr(va));
            out.println(String.format("  0x%08x  raw=0x%08x  float=%s   %s",
                va, raw, Float.toString(Float.intBitsToFloat(raw)), tag));
        } catch (Exception e) { out.println("  read fail " + Long.toHexString(va) + " " + e); }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== SFX log-gain constants =====");
        f(0x006bcfa4L, "_DAT_006bcfa4  voice*master normalization");
        f(0x006bd010L, "DAT_006bd010  silence threshold (0x3b03126f)");
        f(0x006bd428L, "_DAT_006bd428  full-volume threshold");
        f(0x006bda04L, "_DAT_006bda04  log multiplier (expect 20.0)");
        f(0x006bdafcL, "DAT_006bdafc  dB->centibel (100.0)");
        out.println("\n===== FUN_0066ff30 (expect log10) =====");
        dumpFunc(0x0066ff30L, "FUN_0066ff30 math (expect log10)");
        close();
    }
}
