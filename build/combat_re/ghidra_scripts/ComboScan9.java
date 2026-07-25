//@category YsOrigin
// actor+0x6f0 is the combo STAGE index into the per-character section table
// (0x00732664[char]).  Find every read/write of it -> the advance & reset rule.
// Also dump the helpers around the chooser at 0x0046ef00.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ComboScan9 extends Re2Lib {

    void scanDisp(String needle, String tag) {
        out.println("\n===== INSTRUCTIONS MATCHING " + needle + "  (" + tag + ") =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        Map<String, List<String>> byFn = new LinkedHashMap<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (!s.contains(needle) || s.contains("ESP") || s.contains("EBP")) continue;
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
        scanDisp("0x6f0]", "actor+0x6f0 COMBO STAGE");
        scanDisp("0x6f4]", "actor+0x6f4 (neighbour - combo timer?)");
        dumpFunc(0x0046d560L, "combo-variant chooser FUN_0046d560");
        dumpFunc(0x0046d6e0L, "post-SetSection hook FUN_0046d6e0");
        close();
    }
}
