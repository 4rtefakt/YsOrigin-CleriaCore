//@category YsOrigin
import ghidra.program.model.address.*;

public class Gauges2 extends Re2Lib {
    int ri(long va) throws Exception { return currentProgram.getMemory().getInt(toAddr(va)); }
    short rw(long va) throws Exception { return currentProgram.getMemory().getShort(toAddr(va)); }

    void wordArr(long ptrTableVA, int nChars, int nSlots, String tag) throws Exception {
        out.println("\n--- " + tag + " : ptr-table @0x" + Long.toHexString(ptrTableVA)
            + " (" + nChars + " chars x " + nSlots + " slots) ---");
        for (int c = 0; c < nChars; c++) {
            long ptr = ((long) ri(ptrTableVA + c * 4)) & 0xffffffffL;
            StringBuilder sb = new StringBuilder("  char" + c + " -> 0x" + Long.toHexString(ptr) + " : [");
            for (int s = 0; s < nSlots; s++) {
                if (s > 0) sb.append(", ");
                try { sb.append(rw(ptr + s * 2)); } catch (Exception e) { sb.append("?"); }
            }
            sb.append("]");
            out.println(sb);
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("###### HUD DISPLAY cost tables (FUN_0043e800) — compare to mover SPEND tables ######");
        wordArr(0x0068a440L, 4, 4, "DISPLAY primary PTR_0068a440");
        wordArr(0x0068a410L, 4, 4, "DISPLAY hard-add PTR_0068a410");
        out.println("\n char0-special word[] @0x68a2bc : ["
            + rw(0x0068a2bcL) + ", " + rw(0x0068a2beL) + ", " + rw(0x0068a2c0L) + "]");
        out.println("\n(reminder) mover SPEND primary 0x68b0bc, hard-add 0x68b08c dumped in gauges1.");

        dumpFunc(0x0058f210L, "FUN_0058f210 area-reset (writes 0x76c1b4 = 0 and = 30000)");
        close();
    }
}
