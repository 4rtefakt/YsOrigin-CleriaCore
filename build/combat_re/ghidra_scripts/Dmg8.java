// Dmg8: follow the damage chain FUN_00571ae0 -> ... and dump the 0x571xxx script-op band.
// Recursively dump callees up to a depth, decomp + RAW disasm (raw is authoritative: Ghidra
// drops __fastcall ECX/EDX and XMM args).
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class Dmg8 extends Re2Lib {

    Set<Long> done = new LinkedHashSet<>();

    void rec(long va, int depth, int maxDepth, String tag) {
        if (depth > maxDepth) return;
        if (done.contains(va)) { out.println("\n(already dumped 0x" + Long.toHexString(va) + ")"); return; }
        done.add(va);
        dumpFunc(va, "d" + depth + " " + tag);
        // collect callees
        Function f = fnAt(toAddr(va));
        if (f == null) return;
        List<Long> callees = new ArrayList<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (!i.getMnemonicString().toUpperCase().startsWith("CALL")) continue;
            for (Reference r : i.getReferencesFrom()) {
                Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                if (tf != null) callees.add(r.getToAddress().getOffset());
            }
        }
        for (Long c : callees) rec(c, depth + 1, maxDepth, "callee-of-" + Long.toHexString(va));
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg8_out.txt");
        out.println("######## DMG8: the damage chain ########");
        rec(0x00571ae0L, 0, 2, "FUN_00571ae0 (Chr_Damage core)");
        out.println("\n\n######## neighbours in the script-op band ########");
        dumpFunc(0x00571bf0L, "FUN_00571bf0 (writes +0x98)");
        close();
    }
}
