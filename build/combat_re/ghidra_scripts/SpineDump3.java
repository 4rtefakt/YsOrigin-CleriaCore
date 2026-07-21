//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class SpineDump3 extends Re2Lib {
    String wstrAt(long va) {
        try {
            Address a = toAddr(va);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 260; i++) {
                int lo = currentProgram.getMemory().getByte(a.add(i*2)) & 0xff;
                int hi = currentProgram.getMemory().getByte(a.add(i*2+1)) & 0xff;
                int ch = lo | (hi<<8);
                if (ch == 0) break;
                sb.append((char)ch);
            }
            return sb.toString();
        } catch (Exception e) { return "<err>"; }
    }
    void tableDump(long base, int n, String tag) throws Exception {
        out.println("\n===== MOVIE NAME TABLE " + tag + " @0x" + Long.toHexString(base) + " =====");
        for (int i = 0; i < n; i++) {
            try {
                long ptr = currentProgram.getMemory().getInt(toAddr(base + i*4)) & 0xffffffffL;
                if (ptr == 0) { out.println("  ["+i+"] (null)"); continue; }
                out.println("  ["+i+"] 0x" + Long.toHexString(ptr) + "  L\"" + wstrAt(ptr) + "\"");
            } catch (Exception e) { out.println("  ["+i+"] <err>"); }
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0040e700L, "WinMain FUN_0040e700");
        dumpFunc(0x00415d70L, "config-launcher caller FUN_00415d70");
        dumpFunc(0x0040b600L, "init A FUN_0040b600 (device/window?)");
        dumpFunc(0x0040bf50L, "init B FUN_0040bf50 (sound?)");
        dumpFunc(0x005d3ab0L, "init C FUN_005d3ab0 (device create?)");
        tableDump(0x006a0650L, 12, "default/en 6a0650");
        tableDump(0x006a0630L, 8, "en 6a0630");
        tableDump(0x006a0610L, 8, "fr 6a0610");
        close();
    }
}
