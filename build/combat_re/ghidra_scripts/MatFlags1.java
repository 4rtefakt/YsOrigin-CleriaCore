//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.util.*;

public class MatFlags1 extends Re2Lib {
    // scan render band for AND/TEST with immediate 0x1000, and slot-2 / scroll neighbours
    void scanImm(long lo, long hi, long imm) {
        out.println("\n===== SCAN [" + Long.toHexString(lo) + ".." + Long.toHexString(hi)
            + "] for AND/TEST/OR imm 0x" + Long.toHexString(imm) + " =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(toAddr(lo), true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (i.getAddress().getOffset() > hi) break;
            String m = i.getMnemonicString();
            if (!(m.equals("AND")||m.equals("TEST")||m.equals("OR")||m.equals("XOR")||m.equals("CMP")||m.equals("MOV"))) continue;
            boolean hit = false;
            for (int op = 0; op < i.getNumOperands(); op++) {
                Object[] r = i.getOpObjects(op);
                for (Object o : r) if (o instanceof Scalar && ((Scalar)o).getUnsignedValue()==imm) hit=true;
            }
            if (hit) {
                Function f = fnAt(i.getAddress());
                out.println("  " + i.getAddress() + "  " + i + "   [" + (f!=null?f.getName()+"@"+f.getEntryPoint():"?") + "]");
            }
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        scanImm(0x005e0000L, 0x00602000L, 0x1000L);
        dumpFunc(0x005f92b0L, "PER-DRAW RENDER STATE FUN_005f92b0 (§6)");
        dumpFunc(0x005f7050L, "SLOT-2 EXTRA STAGE FUN_005f7050 (gap b)");
        dumpFunc(0x005f5260L, "PER-INSTANCE MATERIAL CLONE FUN_005f5260 (scroll integrator cand)");
        close();
    }
}
