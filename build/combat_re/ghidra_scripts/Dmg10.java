// Dmg10: identify the EBX/param_1 object of FUN_004fd350 by looking at ALL its callers,
// map the four damage appliers, and scan actor+0x734 (i-frame candidate) accesses.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class Dmg10 extends Re2Lib {

    void callSites(long va, String tag) {
        out.println("\n\n===== CALL SITES OF " + tag + " @0x" + Long.toHexString(va) + " =====");
        Listing L = currentProgram.getListing();
        int n = 0;
        for (Reference r : getReferencesTo(toAddr(va))) {
            Instruction i = L.getInstructionAt(r.getFromAddress());
            if (i == null || !i.getMnemonicString().toUpperCase().startsWith("CALL")) continue;
            n++;
            out.println("\n  ---- call at " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "] ----");
            // 22 instructions of lead-in shows how ECX (param_1) and EDX are loaded
            List<Instruction> pre = new ArrayList<>();
            Instruction p = i;
            for (int k = 0; k < 22; k++) {
                p = L.getInstructionBefore(p.getAddress());
                if (p == null) break;
                pre.add(0, p);
            }
            for (Instruction q : pre) out.println("        " + q.getAddress() + "  " + q);
            out.println("     >> " + i.getAddress() + "  " + i);
        }
        out.println("  total call sites: " + n);
    }

    void scanOffset(String off, String tag) {
        out.println("\n\n===== ALL ACCESSES TO [reg + " + off + "]  (" + tag + ") =====");
        Pattern p = Pattern.compile("\\[(EAX|EBX|ECX|EDX|ESI|EDI)\\s*\\+\\s*" + off + "\\]");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int n = 0;
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (!p.matcher(s).find()) continue;
            out.println("  " + i.getAddress() + "  " + s + "    [" + fnName(i.getAddress()) + "]");
            n++;
        }
        out.println("  total: " + n);
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg10_out.txt");
        out.println("######## DMG10: who calls the damage applier? ########");
        callSites(0x004fd350L, "FUN_004fd350 (main damage applier)");
        callSites(0x004fbb40L, "FUN_004fbb40 (applier #2)");
        callSites(0x004fe1a0L, "FUN_004fe1a0 (applier #3)");
        scanOffset("0x734", "i-frame / stun timer candidate");
        dumpFunc(0x00446550L, "FUN_00446550 (set animation state)");
        close();
    }
}
