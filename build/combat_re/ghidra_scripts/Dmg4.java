// Dmg4: (a) dump the actor dispatch table at 0x69b8c0 that Ghidra cannot traverse,
//       (b) mine assert/source strings for combat vocabulary (the binary keeps .cpp names),
//       (c) dump FUN_0045a2f0 (the dispatcher).
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class Dmg4 extends Re2Lib {

    boolean isCode(long v) {
        if (v < 0x401000L || v > 0x660000L) return false;
        try { return currentProgram.getListing().getInstructionAt(toAddr(v)) != null
                 || currentProgram.getFunctionManager().getFunctionAt(toAddr(v)) != null; }
        catch (Exception e) { return false; }
    }

    void dumpPtrTable(long base, int max, String tag) {
        out.println("\n\n===== PTR TABLE " + tag + " @0x" + Long.toHexString(base) + " =====");
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < max; i++) {
            long a = base + i * 4L;
            long v;
            try { v = m.getInt(toAddr(a)) & 0xffffffffL; } catch (Exception e) { out.println("  [" + i + "] <unreadable>"); break; }
            Function f = currentProgram.getFunctionManager().getFunctionAt(toAddr(v));
            String note = isCode(v) ? (f != null ? f.getName() : "(code, no func)") : "<<< NOT CODE - table likely ends here";
            out.println(String.format("  [%3d] 0x%08x -> 0x%08x  %s", i, a, v, note));
            if (!isCode(v)) break;
        }
    }

    void mineStrings() {
        out.println("\n\n===== STRINGS: combat / source-file vocabulary =====");
        String[] keys = {"damage","dmg","attack","atk","hit","dead","death","die","kill",
                         "collision","hantei","kougeki","defen","guard","critical","knock",
                         "actor","enemy","player","hp",".cpp",".h\"","exp","item","drop","gold"};
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        int n = 0;
        while (di.hasNext()) {
            Data d = di.next();
            if (!(d.getDataType() instanceof StringDataType)) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            String lc = s.toLowerCase();
            boolean match = false;
            for (String k : keys) if (lc.contains(k)) { match = true; break; }
            if (!match) continue;
            n++;
            StringBuilder sb = new StringBuilder("  " + d.getAddress() + "  \"" + s + "\"");
            for (Reference r : getReferencesTo(d.getAddress()))
                sb.append("\n        <- " + r.getFromAddress() + " [" + fnName(r.getFromAddress()) + "]");
            out.println(sb);
        }
        out.println("  (total matched strings: " + n + ")");
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg4_out.txt");
        out.println("######## DMG4: dispatch table + string mining ########");
        dumpPtrTable(0x69b8c0L, 400, "actor dispatch (FUN_0045a2f0)");
        dumpFunc(0x0045a2f0L, "DISPATCHER FUN_0045a2f0");
        mineStrings();
        close();
    }
}
