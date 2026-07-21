//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class StubDis extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Address start = toAddr(0x00506ff6L);
        Address end = toAddr(0x00507070L);
        InstructionIterator it = currentProgram.getListing().getInstructions(start, true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (i.getAddress().compareTo(end) > 0) break;
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                if (tf != null) sb.append("   ; -> " + tf.getName());
            }
            out.println(sb);
        }
        close();
    }
}
