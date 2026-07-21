//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class MatFlags4 extends Re2Lib {
    void listOffs(long lo, long hi) {
        out.println("\n===== float insns touching disp 0x48/0x4c/0x50/0x54 in ["+Long.toHexString(lo)+".."+Long.toHexString(hi)+"] =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(toAddr(lo), true);
        while (it.hasNext()) {
            Instruction i = it.next();
            if (i.getAddress().getOffset() > hi) break;
            String s = i.toString();
            if (s.contains("+ 0x48]")||s.contains("+ 0x4c]")||s.contains("+ 0x50]")||s.contains("+ 0x54]")) {
                Function f = fnAt(i.getAddress());
                out.println("  "+i.getAddress()+"  "+s+"   ["+(f!=null?f.getName():"?")+"]");
            }
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        listOffs(0x005f4400L, 0x005f7060L);
        dumpFunc(0x005f6000L, "ANIM CLIP FUN_005f6000 (stride 0xa8)");
        dumpFunc(0x005f5c80L, "CLIP SELECT FUN_005f5c80");
        close();
    }
}
