//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
public class PDmg10 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== stores of an immediate to [reg + 0x12] (GoXx type-tag assignments) =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (!s.startsWith("MOV")) continue;
            if (!(s.contains("+ 0x12]") || s.contains("+0x12]"))) continue;
            // must store an immediate (has a scalar operand)
            boolean hasImm = false; long imm = -1;
            int n = i.getNumOperands();
            for (int op = 0; op < n; op++) {
                Object[] objs = i.getOpObjects(op);
                for (Object o : objs) if (o instanceof Scalar) { hasImm = true; imm = ((Scalar)o).getValue(); }
            }
            if (!hasImm) continue;
            // filter: tag values are small (0..0x40); skip huge immediates (offsets)
            if (imm < 0 || imm > 0x40) continue;
            out.println("  " + i.getAddress() + "  " + s + "   [imm=0x" + Long.toHexString(imm) + "]  [" + fnName(i.getAddress()) + "]");
        }
        close();
    }
}
