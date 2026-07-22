//@category YsOrigin
import ghidra.program.model.address.*;

public class MnOpt2 extends Re2Lib {
    void f(long va, String tag) {
        try {
            int raw = currentProgram.getMemory().getInt(toAddr(va));
            out.println(String.format("  0x%08x  raw=0x%08x  float=%s  int=%d   %s",
                va, raw, Float.toString(Float.intBitsToFloat(raw)), raw, tag));
        } catch (Exception e) { out.println("  read fail " + Long.toHexString(va) + " " + e); }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== consts used in default calc =====");
        f(0x006bd19cL, "DAT_006bd19c  (expect 0.1)");
        f(0x006bd868L, "DAT_006bd868  (expect 5.0)");
        out.println("\n===== math helper wrapped by FUN_004217f0 =====");
        dumpFunc(0x00671000L, "FUN_00671000 (double->double math)");
        out.println("\n===== BGM gain apply =====");
        dumpFunc(0x005ce7f0L, "FUN_005ce7f0 BGM gain apply");
        out.println("\n===== SFX gain apply =====");
        dumpFunc(0x005ce820L, "FUN_005ce820 SFX gain apply");
        close();
    }
}
