//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class Mn2b extends Re2Lib {

    void dumpWords(long va, int count, String tag) throws Exception {
        out.println("\n===== DATA " + tag + " @0x" + Long.toHexString(va) + " (" + count + " dwords) =====");
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < count; i++) {
            Address a = toAddr(va + i*4L);
            int v;
            try { v = m.getInt(a); } catch (Exception e) { out.println("  [" + i + "] (unreadable)"); continue; }
            long uv = v & 0xffffffffL;
            String extra = "";
            if (uv >= 0x400000L && uv <= 0x800000L) {
                try {
                    StringBuilder wb = new StringBuilder();
                    for (int k = 0; k < 48; k++) { int c = m.getShort(toAddr(uv + k*2)) & 0xffff; if (c == 0) break; if (c < 0x20 || c > 0x7e) { wb.setLength(0); break; } wb.append((char)c); }
                    if (wb.length() > 0) extra = "  wide=\"" + wb + "\"";
                    else {
                        StringBuilder sb = new StringBuilder();
                        for (int k = 0; k < 48; k++) { int c = m.getByte(toAddr(uv + k)) & 0xff; if (c == 0) break; if (c < 0x20 || c > 0x7e) { sb.setLength(0); break; } sb.append((char)c); }
                        if (sb.length() > 0) extra = "  ascii=\"" + sb + "\"";
                    }
                } catch (Exception e) {}
            }
            out.println("  [" + i + "] 0x" + Long.toHexString(uv) + extra);
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // char-select build/init (bar cursor reset + bar array init, IMUL 0xac)
        dumpFunc(0x005c2110L, "char-select init FUN_005c2110");
        // portrait/art loader by selected char index
        dumpFunc(0x005c1e70L, "char portrait loader FUN_005c1e70");
        // per-char portrait/art id map (&DAT_00743afc)[bb74]
        dumpWords(0x00743afcL, 4, "char->art id map 0x743afc");

        // difficulty word pointer tables (indexed by DAT_0076bb5c)
        dumpWords(0x0068a470L, 8, "diff word table A 0x68a470");
        dumpWords(0x0068a47cL, 8, "diff word table B 0x68a47c");

        // who reads the diff word table -> the difficulty popup builder & status draw
        xrefs(0x0068a470L, "diff word table A");
        xrefs(0x0068a47cL, "diff word table B");
        // difficulty run value
        xrefs(0x0076bb5cL, "difficulty DAT_0076bb5c");

        close();
    }
}
