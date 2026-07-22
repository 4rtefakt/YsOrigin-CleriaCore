//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class AchFamily extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        long[] targets = {
            0x00405660L, 0x004054f0L, 0x00405380L,
            0x00405210L, 0x004050a0L, 0x00404f30L
        };
        for (long va : targets) {
            dumpFunc(va, "TARGET FUN_" + Long.toHexString(va));
            out.println("\n---- CALLERS of FUN_" + Long.toHexString(va) + " ----");
            for (Address c : callersOf(va)) {
                out.println("    " + c + "   [" + fnName(c) + "]");
            }
        }

        // ---- full band scan: every function in 0x403000..0x407000 with its size
        out.println("\n\n############ BAND SCAN 0x403000..0x407000 ############");
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            long ep = f.getEntryPoint().getOffset();
            if (ep < 0x403000L || ep >= 0x407000L) continue;
            long sz = f.getBody().getNumAddresses();
            // count callers
            int nc = callersOf(ep).size();
            out.println(String.format("  FUN_%08x  size=%-5d  callers=%-2d", ep, sz, nc));
        }

        close();
    }
}
