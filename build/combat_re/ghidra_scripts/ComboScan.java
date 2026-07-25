//@category YsOrigin
// Find (a) every instruction that touches actor+0xe4 (anime_AttackReserve byte) or
// +0xe5 (AttackBreak) or +0xe2 (Cancelable), and (b) every instruction anywhere that
// materialises an immediate in [0x100,0x110) -- the player attack ASO section ids --
// so the combo/next-section chooser can be located.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.util.*;

public class ComboScan extends Re2Lib {

    void scanDisp(String needle, String tag) {
        out.println("\n===== INSTRUCTIONS MATCHING " + needle + "  (" + tag + ") =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int n = 0;
        Map<String, Integer> byFn = new LinkedHashMap<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (!s.contains(needle)) continue;
            String fn = fnName(i.getAddress());
            out.println("  " + i.getAddress() + "  " + s + "    [" + fn + "]");
            byFn.merge(fn, 1, Integer::sum);
            if (++n > 400) { out.println("  ... truncated"); break; }
        }
        out.println("  -- per function --");
        for (Map.Entry<String, Integer> e : byFn.entrySet())
            out.println("     " + e.getValue() + "x  " + e.getKey());
        if (n == 0) out.println("  (none)");
    }

    void scanSectionConsts() {
        out.println("\n===== IMMEDIATES IN [0x100,0x110) (attack section ids) =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        Map<String, List<String>> byFn = new LinkedHashMap<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            boolean hit = false;
            for (int o = 0; o < i.getNumOperands(); o++) {
                Object[] objs = i.getOpObjects(o);
                for (Object ob : objs) {
                    if (ob instanceof Scalar) {
                        long v = ((Scalar) ob).getUnsignedValue();
                        if (v >= 0x100 && v < 0x110) hit = true;
                    }
                }
            }
            if (!hit) continue;
            String fn = fnName(i.getAddress());
            byFn.computeIfAbsent(fn, k -> new ArrayList<>())
                .add(i.getAddress() + "  " + i);
        }
        for (Map.Entry<String, List<String>> e : byFn.entrySet()) {
            if (e.getValue().size() < 2) continue;   // want fns that mention several
            out.println("\n  [" + e.getKey() + "]  " + e.getValue().size() + " hits");
            for (String s : e.getValue()) out.println("      " + s);
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        scanDisp("0xe4]", "actor+0xe4 = AttackReserve");
        scanDisp("0xe5]", "actor+0xe5 = AttackBreak");
        scanDisp("0xe2]", "actor+0xe2 = Cancelable");
        scanSectionConsts();
        close();
    }
}
