// Dmg1: find every instruction that STORES to [reg + 0x98] and [reg + 0x94].
// Trap-aware: constants are immediates, so we walk instruction operands, not xrefs.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class Dmg1 extends Re2Lib {

    // print N instructions of context around an address
    void context(Address a, int before, int after) {
        Listing L = currentProgram.getListing();
        List<Instruction> pre = new ArrayList<>();
        Instruction cur = L.getInstructionAt(a);
        if (cur == null) { out.println("    (no instr)"); return; }
        Instruction p = cur;
        for (int i = 0; i < before; i++) {
            p = L.getInstructionBefore(p.getAddress());
            if (p == null) break;
            pre.add(0, p);
        }
        for (Instruction q : pre) out.println("        " + q.getAddress() + "  " + q);
        out.println("     >> " + cur.getAddress() + "  " + cur);
        Instruction n = cur;
        for (int i = 0; i < after; i++) {
            n = L.getInstructionAfter(n.getAddress());
            if (n == null) break;
            out.println("        " + n.getAddress() + "  " + n);
        }
    }

    void scanStores(String dispHex) {
        out.println("\n\n############ STORES TO [reg + " + dispHex + "] ############");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int n = 0;
        List<Address> hits = new ArrayList<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            String m = i.getMnemonicString().toUpperCase();
            // destination = first operand. match "[REG + 0x98]," right after mnemonic
            int comma = s.indexOf(',');
            if (comma < 0) continue;
            String dst = s.substring(0, comma);
            if (!dst.contains("+ " + dispHex + "]")) continue;
            if (!(m.startsWith("MOV") || m.startsWith("FST") || m.startsWith("ADD")
                  || m.startsWith("SUB") || m.startsWith("AND") || m.startsWith("OR")
                  || m.startsWith("XOR") || m.startsWith("INC") || m.startsWith("DEC"))) continue;
            hits.add(i.getAddress());
            n++;
        }
        out.println("  total store sites: " + n);
        for (Address a : hits) {
            out.println("\n  ---- " + a + "   [" + fnName(a) + "] ----");
            context(a, 14, 6);
        }
    }

    // Also: reads. MOVSS XMM,[reg+0x98]  -- source side
    void scanReads(String dispHex) {
        out.println("\n\n############ READS FROM [reg + " + dispHex + "] (source operand) ############");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        Map<String,Integer> byFn = new TreeMap<>();
        List<Address> hits = new ArrayList<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            int comma = s.indexOf(',');
            if (comma < 0) continue;
            String src = s.substring(comma + 1);
            if (!src.contains("+ " + dispHex + "]")) continue;
            hits.add(i.getAddress());
            byFn.merge(fnName(i.getAddress()), 1, Integer::sum);
        }
        out.println("  total read sites: " + hits.size());
        for (Map.Entry<String,Integer> e : byFn.entrySet())
            out.println("    " + e.getKey() + "  x" + e.getValue());
        out.println("\n  -- sites --");
        for (Address a : hits) out.println("    " + a + "  " + currentProgram.getListing().getInstructionAt(a) + "   [" + fnName(a) + "]");
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg1_out.txt");
        out.println("######## DMG1: writers/readers of actor+0x94 (maxHP) and +0x98 (curHP) ########");
        scanStores("0x98");
        scanStores("0x94");
        scanReads("0x98");
        close();
    }
}
