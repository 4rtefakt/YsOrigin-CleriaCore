//@category YsOrigin
// Locate the readers of the ASO capability-flag block (actor+0xe0..0xe8) and the
// player's attack-section chooser.  Reads may go through a shifted base, so scan
// EVERY memory operand whose displacement lands in [0xe0,0xe9) regardless of size,
// and separately list functions that mention several [0x100,0x110) immediates.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.util.*;
import java.util.regex.*;

public class ComboScan2 extends Re2Lib {

    void scanRange(long lo, long hi, String tag) {
        out.println("\n===== MEM OPERANDS WITH DISPLACEMENT IN [0x" + Long.toHexString(lo)
                    + ",0x" + Long.toHexString(hi) + ")  (" + tag + ") =====");
        Pattern p = Pattern.compile("\\+ (0x[0-9a-f]+)\\]");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        Map<String, List<String>> byFn = new LinkedHashMap<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (s.contains("ESP") || s.contains("EBP")) continue;   // stack frames, not the actor
            Matcher m = p.matcher(s);
            boolean hit = false;
            while (m.find()) {
                long v = Long.parseLong(m.group(1).substring(2), 16);
                if (v >= lo && v < hi) hit = true;
            }
            if (!hit) continue;
            byFn.computeIfAbsent(fnName(i.getAddress()), k -> new ArrayList<>())
                .add(i.getAddress() + "  " + s);
        }
        for (Map.Entry<String, List<String>> e : byFn.entrySet()) {
            out.println("\n  [" + e.getKey() + "]");
            for (String s : e.getValue()) out.println("      " + s);
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        scanRange(0xe0L, 0xe9L, "ASO capability flags Actable/JumpEnable/Cancelable/Turnable/"
                                + "AttackReserve/AttackBreak/?/Untouchable/Durable");
        close();
    }
}
