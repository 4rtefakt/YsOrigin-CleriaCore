//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class CoreDump6 extends Re2Lib {
    void callees(long va) {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) { out.println("  (nofunc)"); return; }
        out.println("---- CALLEES of " + f.getName() + " ----");
        Set<String> seen = new HashSet<>();
        for (Function c : f.getCalledFunctions(mon)) {
            String k = c.getName();
            if (seen.add(k)) out.println("    -> " + c.getName() + " @ " + c.getEntryPoint());
        }
    }
    void callers(long va) {
        out.println("---- CALLERS ----");
        for (Address c : callersOf(va)) out.println("    <- " + fnName(c));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {0x005d4f00L,0x005eee60L,0x0051e880L,0x005fb640L,0x0051b3f0L,0x0040e790L};
        for (long va : fns) {
            dumpFunc(va, "TARGET FUN_" + Long.toHexString(va));
            callers(va);
            callees(va);
        }
        close();
    }
}
