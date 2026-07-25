//@category YsOrigin
// FUN_005a7580 computes each equip-grid cell as
//   x = DAT_007335d0 + DAT_006bd96c + col*DAT_006bdaa8 + DAT_006bd860
//   y = DAT_007335d4 + DAT_006bd9a8 + row*DAT_006bdac4 + DAT_006bd860
// (the 51.2 quad uses DAT_006bd89c in place of DAT_006bd860). Read the real values.
import ghidra.program.model.mem.*;

public class GridPitch extends Re2Lib {
    void f(long a, String tag) throws Exception {
        int v = currentProgram.getMemory().getInt(toAddr(a));
        out.println(String.format("  DAT_%08x = 0x%08x  float %-12.6g  %s",
                                  a, v, Float.intBitsToFloat(v), tag));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        f(0x006bdaa8L, "column pitch");
        f(0x006bdac4L, "row pitch");
        f(0x006bd96cL, "x offset from plate origin");
        f(0x006bd9a8L, "y offset from plate origin");
        f(0x006bd860L, "extra offset (54.4 SQUARE quad)");
        f(0x006bd89cL, "extra offset (51.2 icon quad)");
        f(0x007335d0L, "plate origin X (runtime .data)");
        f(0x007335d4L, "plate origin Y (runtime .data)");
        f(0x00735e38L, "item-page plate rect (runtime .data)");
        close();
    }
}
