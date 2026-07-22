//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class Mn8Tbl extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Memory mem = currentProgram.getMemory();
        long base = 0x00701730L;
        out.println("=== DAT_00701730 region marker table: 22 records x 0x28 (10 dwords) ===");
        out.println("rec:  +00x    +04y    +08     +0c    | +10  +14  +18  +1c  +20  +24  (ints)");
        for (int r = 0; r < 22; r++) {
            long rec = base + (long)r * 0x28;
            StringBuilder sb = new StringBuilder();
            sb.append(String.format("%2d @%08x: ", r, rec));
            for (int d = 0; d < 4; d++) {
                int bits = mem.getInt(toAddr(rec + d*4));
                sb.append(String.format("%8.1f ", Float.intBitsToFloat(bits)));
            }
            sb.append(" | ");
            for (int d = 4; d < 10; d++) {
                int v = mem.getInt(toAddr(rec + d*4));
                sb.append(String.format("%4d ", v));
            }
            out.println(sb.toString());
        }
        // also dump the warp-enable flag byte array default state
        out.println("\n=== (&DAT_0076c11c) warp-enable flags default (24 bytes) ===");
        StringBuilder fb = new StringBuilder();
        for (int i = 0; i < 24; i++) fb.append(String.format("%02x ", mem.getByte(toAddr(0x0076c11cL + i)) & 0xff));
        out.println(fb.toString());
        close();
    }
}
