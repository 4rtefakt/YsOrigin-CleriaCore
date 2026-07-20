//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;
public class Proj3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("===== FUNCTIONS 0x00592000 .. 0x00599000 =====");
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(toAddr(0x00592000L), true);
        while (it.hasNext()) {
            Function f = it.next();
            long ep = f.getEntryPoint().getOffset();
            if (ep > 0x00599000L) break;
            out.println(String.format("  %s  size=%d  refs=%d", f.getEntryPoint(),
                f.getBody().getNumAddresses(), getReferencesTo(f.getEntryPoint()).length));
        }
        // vtable / class construct
        dumpFunc(0x00594190L, "FUN_00594190 (bullet destroy?)");
        dumpFunc(0x00596c70L, "FUN_00596c70");
        close();
    }
}
