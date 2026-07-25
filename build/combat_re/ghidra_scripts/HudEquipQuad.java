//@category YsOrigin
// The HUD's four equipment-slot icons. HUD_PORTRAIT_SLOTS.md says the icon quad is
// {132+40i, 682, +40, +40} -- same size as the 40x40 frame at {126+40i, 676}, which would
// overhang bottom-right by 6px. The equip PAGE turned out to have its two quad sizes
// swapped (COMBO_CHAIN-era finding), so re-derive the HUD quad's SIZE from the code
// instead of trusting that note.
//   FUN_0043fe00 = builder D (draws the equipped icons + the Lv badge)
//   FUN_0043c230 = builder A (draws the 40x40 slot frames)
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class HudEquipQuad extends Re2Lib {
    void f(long a, String tag) throws Exception {
        Memory m = currentProgram.getMemory();
        int v = m.getInt(toAddr(a));
        out.println(String.format("  DAT_%08x = 0x%08x  float %.6g  int %d   %s",
                                  a, v, Float.intBitsToFloat(v), v, tag));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== known layout constants =====");
        f(0x006bdb44L, "_DAT_006bdb44 (icon x base, doc says 132)");
        f(0x006bdb38L, "_DAT_006bdb38 (frame x base, doc says 126)");
        dumpFunc(0x0043fe00L, "HUD builder D FUN_0043fe00 (equipped icons + Lv badge)");
        close();
    }
}
