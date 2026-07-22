//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.symbol.*;

public class MenuVerify4 extends Re2Lib {

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        out.println("################## ITEM 1: menu-tex loader + full instruction scan ##################");
        dumpFunc(0x00576020L, "menu-tex table loader FUN_00576020");
        dumpFunc(0x005e4380L, "go_wnd tex getter? FUN_005e4380");

        // Scan EVERY instruction for a scalar operand OR reference in [0x749d40, 0x749d78],
        // catching base+disp writes that xref analysis missed.
        long lo = 0x00749d40L, hi = 0x00749d78L;
        out.println("\n===== INSTRUCTION SCAN for scalar/ref in [0x" + Long.toHexString(lo)
            + ", 0x" + Long.toHexString(hi) + "] =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int hits = 0;
        while (it.hasNext() && hits < 4000) {
            Instruction ins = it.next();
            boolean hit = false;
            // check scalar operands
            int nops = ins.getNumOperands();
            for (int oi = 0; oi < nops && !hit; oi++) {
                for (Object o : ins.getOpObjects(oi)) {
                    if (o instanceof Scalar) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v >= lo && v <= hi) { hit = true; break; }
                    }
                    if (o instanceof Address) {
                        long v = ((Address) o).getOffset();
                        if (v >= lo && v <= hi) { hit = true; break; }
                    }
                }
            }
            // check references
            if (!hit) {
                for (Reference r : ins.getReferencesFrom()) {
                    long v = r.getToAddress().getOffset();
                    if (v >= lo && v <= hi) { hit = true; break; }
                }
            }
            if (hit) {
                out.println("  " + ins.getAddress() + "  " + ins + "    [" + fnName(ins.getAddress()) + "]");
                hits++;
            }
        }
        out.println("(hits " + hits + ")");

        close();
    }
}
