//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;
public class Ef2Scan68 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== scan for imm 0x68 (104, track stride) and 0x1a0 disp =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        LinkedHashSet<String> hit68 = new LinkedHashSet<>();
        LinkedHashSet<String> hit1a0 = new LinkedHashSet<>();
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            String mn = i.getMnemonicString();
            // IMUL / ADD with 0x68 immediate
            if ((mn.startsWith("IMUL")||mn.equals("ADD")||mn.equals("LEA")) && (s.contains("0x68")||s.contains(",104"))) {
                hit68.add(fnName(i.getAddress()) + "  @" + i.getAddress() + "  " + s);
            }
            if (s.contains("0x1a0")) {
                hit1a0.add(fnName(i.getAddress()) + "  @" + i.getAddress() + "  " + s);
            }
        }
        out.println("\n---- 0x68 stride sites ("+hit68.size()+") ----");
        for (String s : hit68) out.println("  "+s);
        out.println("\n---- 0x1a0 disp sites ("+hit1a0.size()+") ----");
        for (String s : hit1a0) out.println("  "+s);
        close();
    }
}
