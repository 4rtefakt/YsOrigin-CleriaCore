// Dmg3: same as Dmg2 but EXCLUDES ESP/EBP-relative accesses (stack frames, which polluted Dmg2).
// Only heap-object accesses [EAX/EBX/ECX/EDX/ESI/EDI + off] count.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class Dmg3 extends Re2Lib {

    static final String[] SIG = {"0x94","0x98","0xa4","0xa8","0xac","0xb0","0xc4","0xc8","0x128","0x130","0x3c4","0x86c","0x2a7","0x60"};

    static class FnInfo {
        Set<String> touched = new TreeSet<>();
        Set<String> writes  = new TreeSet<>();
        Set<String> reads   = new TreeSet<>();
        List<String> hpStores = new ArrayList<>();
    }

    // match [REG + 0xNN] where REG is a general object register (not ESP/EBP), no index term
    static Pattern pat(String off) {
        return Pattern.compile("\\[(EAX|EBX|ECX|EDX|ESI|EDI)\\s*\\+\\s*" + off + "\\]");
    }
    static Map<String,Pattern> PATS = new HashMap<>();
    static { for (String o : SIG) PATS.put(o, pat(o)); }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg3_out.txt");
        out.println("######## DMG3: actor-band scoring, ESP/EBP excluded ########");

        Map<String,FnInfo> map = new TreeMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            int comma = s.indexOf(',');
            String dst = comma < 0 ? s : s.substring(0, comma);
            String src = comma < 0 ? ""  : s.substring(comma + 1);
            Function f = fnAt(i.getAddress());
            String key = f != null ? f.getName() + "@" + f.getEntryPoint() : "(nofunc)@" + i.getAddress();
            for (String off : SIG) {
                Pattern p = PATS.get(off);
                boolean w = p.matcher(dst).find(), r = p.matcher(src).find();
                if (!w && !r) continue;
                FnInfo fi = map.computeIfAbsent(key, k -> new FnInfo());
                fi.touched.add(off);
                if (w) { fi.writes.add(off); if (off.equals("0x98")) fi.hpStores.add(i.getAddress() + "  " + s); }
                if (r) fi.reads.add(off);
            }
        }

        List<Map.Entry<String,FnInfo>> L = new ArrayList<>(map.entrySet());
        L.sort((a,b) -> b.getValue().touched.size() - a.getValue().touched.size());

        out.println("\n===== RANKED (>=3 signature offsets) =====");
        for (Map.Entry<String,FnInfo> e : L) {
            FnInfo fi = e.getValue();
            if (fi.touched.size() < 3) continue;
            out.println(String.format("  %-34s n=%2d  touched=%s", e.getKey(), fi.touched.size(), fi.touched));
            out.println(String.format("        W=%s   R=%s", fi.writes, fi.reads));
            for (String h : fi.hpStores) out.println("        *** 0x98 STORE: " + h);
        }

        out.println("\n\n===== ALL 0x98 (curHP) OBJECT STORES, program-wide =====");
        for (Map.Entry<String,FnInfo> e : map.entrySet())
            for (String h : e.getValue().hpStores)
                out.println("  " + h + "    [" + e.getKey() + "]  touched=" + e.getValue().touched);

        for (String off : new String[]{"0xa4","0xa8","0xb0","0xac","0xc4"}) {
            out.println("\n===== FUNCTIONS READING +" + off + " =====");
            for (Map.Entry<String,FnInfo> e : map.entrySet())
                if (e.getValue().reads.contains(off))
                    out.println("  " + e.getKey() + "   touched=" + e.getValue().touched);
        }
        close();
    }
}
