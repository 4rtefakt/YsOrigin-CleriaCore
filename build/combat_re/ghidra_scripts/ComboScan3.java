//@category YsOrigin
// The player action/combo state machine: FUN_00465170 mentions 38 immediates in
// [0x100,0x110) -- the attack ASO section ids.  Dump it, plus every instruction
// touching actor+0x994 (the "current clip / action id" field) so the section
// setter is identified.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class ComboScan3 extends Re2Lib {

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
        scanDisp("0x994]", "actor+0x994 current action/clip id");
        dumpFunc(0x00465170L, "player action/combo state machine FUN_00465170");
        close();
    }
}
