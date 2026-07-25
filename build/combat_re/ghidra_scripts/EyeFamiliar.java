//@category YsOrigin
// Hugo's "Eye of Fact" familiars: cast 176 (0xb0) = CAST_M900, script M_900.ASO.
//   FUN_00470210 = the orbit placement
//   FUN_004731d0 = the familiar brain
// Context already established in build/combat_re/COMBO_CHAIN.md: the DAT_0076bb74==2
// (Hugo) branch of FUN_0046ea00 walks every actor with +0x124 == 0xb0 and keys off
// +0x8a4/+0x8a5, writing +0x7bc/+0x7c0/+0x7c4 on the player.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class EyeFamiliar extends Re2Lib {

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
        dumpFunc(0x00470210L, "eye orbit FUN_00470210");
        dumpFunc(0x004731d0L, "eye brain FUN_004731d0");
        scanDisp("0x8a4]", "familiar slot flag +0x8a4");
        scanDisp("0x7bc]", "player +0x7bc (familiar count/state)");
        out.println("\n===== CALLERS =====");
        for (long a : new long[]{0x00470210L, 0x004731d0L}) {
            out.println("  of FUN_" + Long.toHexString(a) + ":");
            for (Address c : callersOf(a)) out.println("     " + c + "   [" + fnName(c) + "]");
        }
        close();
    }
}
