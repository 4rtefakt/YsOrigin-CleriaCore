//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class Mn2Dump extends Re2Lib {

    // dump `count` 4-byte words at va, annotate string/func targets
    void dumpWords(long va, int count, String tag) throws Exception {
        out.println("\n===== DATA " + tag + " @0x" + Long.toHexString(va) + " (" + count + " dwords) =====");
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < count; i++) {
            Address a = toAddr(va + i*4L);
            int v;
            try { v = m.getInt(a); } catch (Exception e) { out.println("  [" + i + "] (unreadable)"); continue; }
            long uv = v & 0xffffffffL;
            String extra = "";
            // if it looks like a pointer, try to read a wide/ascii string there
            if (uv >= 0x400000L && uv <= 0x800000L) {
                try {
                    Address t = toAddr(uv);
                    StringBuilder sb = new StringBuilder();
                    // try ascii
                    for (int k = 0; k < 40; k++) { int c = m.getByte(toAddr(uv + k)) & 0xff; if (c == 0) break; if (c < 0x20 || c > 0x7e) { sb.setLength(0); break; } sb.append((char)c); }
                    if (sb.length() > 0) extra = "  ascii=\"" + sb + "\"";
                    else {
                        StringBuilder wb = new StringBuilder();
                        for (int k = 0; k < 40; k++) { int c = m.getShort(toAddr(uv + k*2)) & 0xffff; if (c == 0) break; if (c < 0x20 || c > 0x7e) { wb.setLength(0); break; } wb.append((char)c); }
                        if (wb.length() > 0) extra = "  wide=\"" + wb + "\"";
                    }
                } catch (Exception e) {}
            }
            out.println("  [" + i + "] 0x" + Long.toHexString(uv) + extra);
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // 1. char-select commit / writer of DAT_0076bb74 (bar cursor DAT_00742f7c -> char enum)
        dumpFunc(0x005c3790L, "char-select commit FUN_005c3790 (writes bb74)");

        // 2. who writes the char index -> find the whole char-select machine
        xrefs(0x0076bb74L, "char index DAT_0076bb74");
        // 3. bar cursor DAT_00742f7c readers/writers -> draw fn + nav
        xrefs(0x00742f7cL, "bar cursor DAT_00742f7c");

        // 4. difficulty word fn + its table
        dumpFunc(0x005b0860L, "difficulty word FUN_005b0860");
        dumpWords(0x006a1748L, 8, "difficulty word table 0x6a1748");

        // 5. go_wnddialog draw (popup frame + cursor) — confirm for the 3 popups
        dumpFunc(0x00578f80L, "go_wnddialog draw FUN_00578f80");

        close();
    }
}
