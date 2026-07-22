//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn5Plaque extends Re2Lib {
    void floats(long va, int n, String tag) throws Exception {
        Address a = toAddr(va);
        StringBuilder sb = new StringBuilder("\n" + tag + " @0x" + Long.toHexString(va) + "\n");
        for (int i = 0; i < n; i++) {
            int raw = currentProgram.getMemory().getInt(a.add(i*4));
            float f = Float.intBitsToFloat(raw);
            sb.append(String.format("  [%2d] +0x%02x  0x%08x  f=%.4f  i=%d\n", i, i*4, raw, f, raw));
        }
        out.println(sb.toString());
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Plaque table &DAT_00732e64, stride 0xAC, 9 entries. Entry 0 = Equip, 1 = Item.
        floats(0x00732e64L, 24, "plaque[0] Equip csprite");
        floats(0x00732e64L + 0xACL, 24, "plaque[1] Item csprite");
        // The w_Invent plate + first cell already known; dump inset globals to reconfirm
        floats(0x006bd860L, 1, "DAT_006bd860 cell offset (expect 4.8)");
        floats(0x006bd89cL, 1, "DAT_006bd89c cursor inset (expect 6.4)");
        floats(0x006bdaa8L, 1, "DAT_006bdaa8 row pitch (expect 64)");
        close();
    }
}
