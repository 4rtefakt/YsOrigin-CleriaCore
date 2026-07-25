//@category YsOrigin
// The per-character attack-section tables: pointers live at 0x00732664.. and point at
// u16 arrays starting {0x100,0x103,0x104,...}.  Dump the pointer array, the raw u16
// around every candidate table, and every code xref that indexes them.
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ComboScan8 extends Re2Lib {

    void words(long lo, long hi, String tag) throws Exception {
        out.println("\n-- u16 " + tag + " 0x" + Long.toHexString(lo) + "..0x" + Long.toHexString(hi));
        Memory m = currentProgram.getMemory();
        for (long a = lo; a < hi; a += 16) {
            StringBuilder sb = new StringBuilder(String.format("   0x%06x: ", a));
            for (int k = 0; k < 8 && a + k * 2 < hi; k++)
                sb.append(String.format("%04x ", m.getShort(toAddr(a + k * 2)) & 0xffff));
            out.println(sb);
        }
    }

    void dwords(long lo, long hi, String tag) throws Exception {
        out.println("\n-- u32 " + tag + " 0x" + Long.toHexString(lo) + "..0x" + Long.toHexString(hi));
        Memory m = currentProgram.getMemory();
        for (long a = lo; a < hi; a += 4) {
            long v = m.getInt(toAddr(a)) & 0xffffffffL;
            StringBuilder sb = new StringBuilder(String.format("   0x%06x: 0x%08x", a, v));
            if (v > 0x400000 && v < 0x7de000) {
                sb.append("   -> ");
                for (int k = 0; k < 10; k++) sb.append(String.format("%04x ", m.getShort(toAddr(v + k * 2)) & 0xffff));
            }
            out.println(sb);
            for (Reference r : getReferencesTo(toAddr(a)))
                out.println("        xref " + r.getFromAddress() + " " + r.getReferenceType()
                            + "  [" + fnName(r.getFromAddress()) + "]");
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dwords(0x00732640L, 0x00732690L, "pointer array to per-character section tables");
        words(0x0068a7d0L, 0x0068a820L, "table @0x68a7f4 neighbourhood");
        words(0x0068af70L, 0x0068b040L, "tables @0x68af88/0x68afa0/0x68afb0/0x68afb8");
        for (long a : new long[]{0x0068a7f4L, 0x0068afa0L, 0x0068afb0L, 0x0068afb8L, 0x0068af88L})
            xrefs(a, "section table @0x" + Long.toHexString(a));
        close();
    }
}
