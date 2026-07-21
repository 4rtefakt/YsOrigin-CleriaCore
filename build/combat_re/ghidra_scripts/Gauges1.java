//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class Gauges1 extends Re2Lib {

    int ri(long va) throws Exception { return currentProgram.getMemory().getInt(toAddr(va)); }
    short rw(long va) throws Exception { return currentProgram.getMemory().getShort(toAddr(va)); }
    float rf(long va) throws Exception { return Float.intBitsToFloat(ri(va)); }

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

        out.println("############ Q1(b) SKILL COST TABLES ############");
        // cost float built from word[ PTR[char] + slot*2 ]; two tables (primary 0x68b0bc, hard-add 0x68b08c)
        wordArr(0x0068b0bcL, 4, 4, "PRIMARY cost table PTR_0068b0bc");
        wordArr(0x0068b08cL, 4, 4, "HARD-ADD cost table PTR_0068b08c");
        out.println("\n scalar 0x68b080 (int) = " + ri(0x0068b080L)
            + "  (word[0]=" + rw(0x0068b080L) + ")");
        out.println(" cost scale consts:");
        out.println("   0x6bd304 = " + rf(0x006bd304L));
        out.println("   0x6bd3a8 = " + rf(0x006bd3a8L));
        out.println("   0x6bd3ec = " + rf(0x006bd3ecL));
        out.println("   0x6bd3f0 = " + rf(0x006bd3f0L));
        out.println(" SP max 0x6bdf48 = " + rf(0x006bdf48L));

        dumpFunc(0x0066f350L, "FUN_0066f350 (float->int round helper for cost)");

        out.println("\n\n############ Q1(a) HUD DRAW candidates (SP/mana + EX gauges) ############");
        dumpFunc(0x0043c230L, "FUN_0043c230 (reads 0x76a738 + 0x76a630)");
        dumpFunc(0x0043e800L, "FUN_0043e800 (reads 0x76a738 x3 + 0x76a630)");
        dumpFunc(0x0043fe00L, "FUN_0043fe00 (reads 0x76c1b4 x4 + 0x76bb50 == EX gauge draw)");
        dumpFunc(0x0043f500L, "FUN_0043f500 (reads 0x76c1b4 once)");

        xrefs(0x0076a734L, "SP gauge MAX DAT_0076a734");
        xrefs(0x0076a730L, "DAT_0076a730 (adjacent, HP? gauge)");

        out.println("\n\n############ Q3 BOOST-GAUGE OTHER WRITERS ############");
        dumpFunc(0x00443390L, "FUN_00443390 (writes 0x76c1b4 = 30000 FULL)");
        dumpFunc(0x0054a890L, "FUN_0054a890 (writes 0x76c1b4 = EAX, @0x54c917)");

        close();
    }
}
