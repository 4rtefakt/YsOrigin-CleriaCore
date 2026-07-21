//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class MatFlags3 extends Re2Lib {
    // find UV-scroll integrator: function that WRITES [reg+0x48]/[reg+0x4c] AND READS [reg+0x50]/[reg+0x54]
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Map<String,int[]> tally = new TreeMap<>(); // fn -> {writes48, reads50}
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String m = i.getMnemonicString();
            if (!(m.startsWith("MOV")||m.startsWith("ADD")||m.startsWith("MUL")||m.startsWith("SUB"))) continue;
            String s = i.toString();
            boolean w48 = false, r50 = false;
            int comma = s.indexOf(',');
            String dst = comma<0? s : s.substring(0,comma);
            if ((dst.contains("+ 0x48]")||dst.contains("+ 0x4c]")) && !dst.contains("ESP") && !dst.contains("EBP")) w48=true;
            if ((s.contains("+ 0x50]")||s.contains("+ 0x54]")) && !s.contains("ESP") && !s.contains("EBP")) r50=true;
            if (!w48 && !r50) continue;
            Function f = fnAt(i.getAddress());
            String k = f!=null? f.getName()+"@"+f.getEntryPoint() : "?";
            int[] v = tally.computeIfAbsent(k, x->new int[2]);
            if (w48) v[0]++;
            if (r50) v[1]++;
        }
        out.println("===== functions writing +0x48/+0x4c AND/OR reading +0x50/+0x54 =====");
        for (Map.Entry<String,int[]> e : tally.entrySet()) {
            int[] v = e.getValue();
            if (v[0]>0 && v[1]>0) out.println("  *** BOTH  w48="+v[0]+" r50="+v[1]+"   "+e.getKey());
        }
        out.println("----- (write-only, for reference) -----");
        for (Map.Entry<String,int[]> e : tally.entrySet()) {
            int[] v = e.getValue();
            if (v[0]>0 && v[1]==0) out.println("  w48="+v[0]+"   "+e.getKey());
        }
        // go_mesh per-frame update
        dumpFunc(0x00501a30L, "go_mesh.cpp FUN_00501a30");
        close();
    }
}
