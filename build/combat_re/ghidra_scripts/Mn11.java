//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Mn11 extends Re2Lib {

    // read a little-endian dword
    long rd32(long va) throws Exception {
        byte[] b = new byte[4];
        currentProgram.getMemory().getBytes(toAddr(va), b);
        return (b[0]&0xffL) | ((b[1]&0xffL)<<8) | ((b[2]&0xffL)<<16) | ((b[3]&0xffL)<<24);
    }

    void dumpDwords(long base, int n, String tag) throws Exception {
        out.println("\n===== DWORDS @0x" + Long.toHexString(base) + " (" + tag + ") =====");
        for (int i = 0; i < n; i++) {
            long va = base + i*4L;
            long v = rd32(va);
            out.println(String.format("  [%2d] 0x%08x = %d (0x%x)", i, va, (int)v, v));
        }
    }

    // read an ASCII string at va (max len)
    String rdStr(long va, int max) throws Exception {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < max; i++) {
            byte c = currentProgram.getMemory().getByte(toAddr(va+i));
            if (c == 0) break;
            if (c >= 32 && c < 127) sb.append((char)c);
            else sb.append('.');
        }
        return sb.toString();
    }

    // treat a table as pointers-to-string, dump
    void dumpStrPtrs(long base, int n, String tag) throws Exception {
        out.println("\n===== STR-PTR TABLE @0x" + Long.toHexString(base) + " (" + tag + ") =====");
        for (int i = 0; i < n; i++) {
            long p = rd32(base + i*4L) & 0xffffffffL;
            String s = "";
            try { s = rdStr(p, 48); } catch (Exception e) { s = "(bad ptr)"; }
            out.println(String.format("  [%2d] ->0x%08x  \"%s\"", i, p, s));
        }
    }

    // scan all defined strings for any of the needles, report addr + xrefs
    void scanStrings(String[] needles) throws Exception {
        out.println("\n===== STRING SCAN " + Arrays.toString(needles) + " =====");
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        int hits = 0;
        while (di.hasNext() && hits < 400) {
            Data d = di.next();
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            String low = s.toLowerCase();
            boolean match = false;
            for (String nd : needles) if (low.contains(nd.toLowerCase())) { match = true; break; }
            if (!match) continue;
            hits++;
            StringBuilder xr = new StringBuilder();
            int nx = 0;
            for (Reference r : getReferencesTo(d.getAddress())) {
                if (nx++ > 6) { xr.append(" ..."); break; }
                xr.append(" ").append(r.getFromAddress()).append("[").append(fnName(r.getFromAddress())).append("]");
            }
            String sp = s.length() > 60 ? s.substring(0,60) : s;
            out.println("  @" + d.getAddress() + "  \"" + sp.replace("\n","\\n") + "\"  xr:" + xr);
        }
        out.println("  (" + hits + " hits)");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // 1. DAT_0068c460 per-character stage-count table (and neighbours)
        dumpDwords(0x0068c460L, 8, "DAT_0068c460 per-char stage-count table");
        dumpDwords(0x0068c4f8L, 24, "0x0068c4f8 stage table region (dwords)");
        dumpDwords(0x0068c5a0L, 8, "0x0068c5a0 region");

        // 2. stage-name pointer tables (guessed as str-ptr tables)
        dumpStrPtrs(0x006b49bcL, 16, "0x006b49bc Arena-Stage name table?");
        dumpStrPtrs(0x006a0e90L, 16, "0x006a0e90 Arena-Stage name table (EN)?");

        // 3. key front-end functions
        dumpFunc(0x0057bb20L, "stage/mission select FUN_0057bb20");
        dumpFunc(0x0057cee0L, "stage-select DRAW callback FUN_0057cee0");
        dumpFunc(0x0057b5f0L, "run header/overlay FUN_0057b5f0");
        dumpFunc(0x0057d2a0L, "run header/overlay FUN_0057d2a0");
        dumpFunc(0x005802d0L, "reads DAT_0074ab9f x5 (stage cursor/preview) FUN_005802d0");
        dumpFunc(0x00406210L, "ArenaSave stage-6 milestone FUN_00406210");

        // 4. record / score / time-attack result screen — find via strings
        scanStrings(new String[]{"record", "Best", "Rank", "Score", "Result",
                                 "%02d:%02d", "TIME", "Boss Rush", "Arena Stage", "Time Attack"});

        // 5. record.* file I/O — find the filename strings
        scanStrings(new String[]{"record", ".dat", "record\\", "userdata"});

        close();
    }
}
