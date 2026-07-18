// Dmg7: dump the XSO VM handler blocks for the combat ops, and fully decompile+disassemble
// every function they CALL. Handler blocks live inside one giant VM function, so we dump a
// raw instruction run rather than the containing function.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class Dmg7 extends Re2Lib {

    Set<Long> calleeSet = new LinkedHashSet<>();

    // dump raw instructions starting at va until we hit a RET, or an unconditional JMP
    // that leaves the run, or maxN instructions.
    void dumpBlock(long va, int maxN, String tag) {
        out.println("\n\n================ HANDLER " + tag + " @0x" + Long.toHexString(va) + " ================");
        Listing L = currentProgram.getListing();
        Instruction i = L.getInstructionAt(toAddr(va));
        for (int n = 0; n < maxN && i != null; n++) {
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                if (tf != null) { sb.append("   ; -> " + tf.getName()); calleeSet.add(r.getToAddress().getOffset()); }
                Data d = L.getDataAt(r.getToAddress());
                if (d != null && d.getValue() != null) sb.append("   ; data= " + d.getValue());
            }
            out.println(sb);
            String m = i.getMnemonicString().toUpperCase();
            if (m.startsWith("RET")) { out.println("  ---- (RET, block end)"); break; }
            i = L.getInstructionAfter(i.getAddress());
        }
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg7_out.txt");
        out.println("######## DMG7: XSO combat op handlers ########");
        out.println("funcid = FUNCLIST.DEF line number - 1");

        dumpBlock(0x56c8b3L, 70, "op 0x15c Chr_Damage(i,f,i,i)");
        dumpBlock(0x56c262L, 60, "op 0x14a Chr_Attack(i,i)");
        dumpBlock(0x56c2a9L, 60, "op 0x14b Chr_Knockback(i,i)");
        dumpBlock(0x56d453L, 60, "op 0x17e Mesh_DropItem(s,i,i)");

        out.println("\n\n\n######## CALLEES (full decomp + raw disasm) ########");
        for (Long c : new ArrayList<>(calleeSet)) dumpFunc(c, "CALLEE 0x" + Long.toHexString(c));

        close();
    }
}
