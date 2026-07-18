// Dmg2: identify ACTOR-BAND functions by co-occurrence of the actor's signature offsets,
// then report which of them WRITE +0x98 (curHP) and which READ +0xa4/+0xa8/+0xb0 (stat fields).
// Rationale: +0x98 alone collides across unrelated structs (260 raw store sites).
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class Dmg2 extends Re2Lib {

    // actor-signature offsets that are distinctive
    static final String[] SIG = {"0x94","0x98","0xa4","0xa8","0xac","0xb0","0xc4","0xc8","0x128","0x130","0x3c4","0x86c","0x2a7"};

    static class FnInfo {
        Set<String> touched = new TreeSet<>();
        Set<String> writes  = new TreeSet<>();
        Set<String> reads   = new TreeSet<>();
        List<String> hpStores = new ArrayList<>();
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg2_out.txt");
        out.println("######## DMG2: actor-band scoring ########");

        Map<String,FnInfo> map = new TreeMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            int comma = s.indexOf(',');
            String dst = comma < 0 ? s : s.substring(0, comma);
            String src = comma < 0 ? ""  : s.substring(comma + 1);
            Function f = fnAt(i.getAddress());
            String key = f != null ? f.getName() + "@" + f.getEntryPoint() : "(nofunc)";
            for (String off : SIG) {
                String pat = "+ " + off + "]";
                boolean w = dst.contains(pat), r = src.contains(pat);
                if (!w && !r) continue;
                FnInfo fi = map.computeIfAbsent(key, k -> new FnInfo());
                fi.touched.add(off);
                if (w) { fi.writes.add(off); if (off.equals("0x98")) fi.hpStores.add(i.getAddress() + "  " + s); }
                if (r) fi.reads.add(off);
            }
        }

        // rank by number of distinct signature offsets touched
        List<Map.Entry<String,FnInfo>> L = new ArrayList<>(map.entrySet());
        L.sort((a,b) -> b.getValue().touched.size() - a.getValue().touched.size());

        out.println("\n===== FUNCTIONS RANKED BY #signature offsets touched (>=3) =====");
        for (Map.Entry<String,FnInfo> e : L) {
            FnInfo fi = e.getValue();
            if (fi.touched.size() < 3) continue;
            out.println(String.format("  %-34s n=%2d  touched=%s", e.getKey(), fi.touched.size(), fi.touched));
            out.println(String.format("        writes=%s", fi.writes));
            out.println(String.format("        reads =%s", fi.reads));
            if (!fi.hpStores.isEmpty()) { out.println("        *** WRITES 0x98 (curHP):"); for (String h : fi.hpStores) out.println("             " + h); }
        }

        out.println("\n\n===== EVERY FUNCTION THAT READS +0xa4 =====");
        for (Map.Entry<String,FnInfo> e : map.entrySet()) if (e.getValue().reads.contains("0xa4")) out.println("  " + e.getKey() + "   touched=" + e.getValue().touched);
        out.println("\n===== EVERY FUNCTION THAT READS +0xa8 =====");
        for (Map.Entry<String,FnInfo> e : map.entrySet()) if (e.getValue().reads.contains("0xa8")) out.println("  " + e.getKey() + "   touched=" + e.getValue().touched);
        out.println("\n===== EVERY FUNCTION THAT READS +0xb0 =====");
        for (Map.Entry<String,FnInfo> e : map.entrySet()) if (e.getValue().reads.contains("0xb0")) out.println("  " + e.getKey() + "   touched=" + e.getValue().touched);
        out.println("\n===== EVERY FUNCTION THAT READS +0xc4 (EXP) =====");
        for (Map.Entry<String,FnInfo> e : map.entrySet()) if (e.getValue().reads.contains("0xc4")) out.println("  " + e.getKey() + "   touched=" + e.getValue().touched);

        close();
    }
}
