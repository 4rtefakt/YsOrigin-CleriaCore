//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class MnOpt extends Re2Lib {
    void f(long va, String tag) {
        try {
            Address a = toAddr(va);
            int raw = currentProgram.getMemory().getInt(a);   // little-endian
            float fv = Float.intBitsToFloat(raw);
            out.println(String.format("  0x%08x  raw=0x%08x  float=%s  int=%d   %s",
                va, raw, Float.toString(fv), raw, tag));
        } catch (Exception e) {
            out.println(String.format("  0x%08x  <read fail: %s>  %s", va, e.getMessage(), tag));
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== OPTION-PAGE FLOAT DATA (precise reads) =====");
        f(0x006bda70L, "_DAT_006bda70  row pitch (per-row Y advance)");
        f(0x006bda48L, "_DAT_006bda48  gap before Return/Exit group (row8)");
        f(0x006bda64L, "DAT_006bda64  checkbox Y term (@005ba... 1148)");
        f(0x006be148L, "_DAT_006be148  slider visual scale (knob px/value)");
        f(0x006bdcd4L, "DAT_006bdcd4  slider track X offset (473.6)");
        f(0x006bdc90L, "DAT_006bdc90  (@005b9c3f ADDSS)");
        f(0x006bcfe8L, "DAT_006bcfe8  1/1024 UV scale");
        f(0x006bd9f0L, "DAT_006bd9f0  press/highlight scale (32.0)");
        f(0x006becd0L, "DAT_006becd0  checkbox/OpBtn UV base");
        f(0x006becccL, "DAT_006beccc  (near checkbox UV base)");
        f(0x006bd008L, "DAT_006bd008  gamma k=1/512");
        f(0x006bd304L, "DAT_006bd304  gamma 0.5");
        f(0x006bd438L, "DAT_006bd438  gamma 1.0");
        f(0x006d606dL, "_DAT_006d606d  control-bytes dword default source");
        out.println("\n===== VALUE->GAIN CURVE FUN_004217f0 =====");
        dumpFunc(0x004217f0L, "FUN_004217f0 volume value->gain helper");
        close();
    }
}
