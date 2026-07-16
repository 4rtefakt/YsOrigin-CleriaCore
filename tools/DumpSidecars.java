//Decompile the map sidecar loaders. SOB (object placement) is the target;
//SCM/SEN come along free since they sit in the same module at 0x433xxx.
//Also sweeps that address range for neighbours -- the other sidecar loaders
//(SEF/SEV/SCT/SFO/SNF) don't push a magic constant, so the magic scan missed them.
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class DumpSidecars extends GhidraScript {
    static final long[] ROOTS = {
        0x00433110L,  // SOB  <- object placement, THE target
        0x00433a10L,  // SEN  <- scene exits / room graph
        0x004338c0L,  // SCM  <- cameras
    };

    public void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
        PrintWriter out = new PrintWriter(new FileWriter("D:\\ghidra-work\\sidecar_out.txt"));
        FunctionManager fm = currentProgram.getFunctionManager();

        // 1. neighbourhood map -- what else lives in this module?
        out.println("### functions in 0x432000..0x435000 ###");
        FunctionIterator it = fm.getFunctions(toAddr(0x00432000L), true);
        while (it.hasNext()) {
            Function f = it.next();
            long ep = f.getEntryPoint().getOffset();
            if (ep > 0x00435000L) break;
            out.println(String.format("  %s  %-28s  body=%d",
                f.getEntryPoint(), f.getName(), f.getBody().getNumAddresses()));
        }

        Set<String> done = new LinkedHashSet<String>();
        for (long r : ROOTS) {
            Function f = fm.getFunctionContaining(toAddr(r));
            if (f == null) { out.println("!! none @ " + Long.toHexString(r)); continue; }
            dump(out, decomp, monitor, f, 0);
            done.add(f.getEntryPoint().toString());
            // one level of callees -- the record parser is likely a callee
            for (Function c : f.getCalledFunctions(monitor)) {
                String n = c.getName();
                if (n.startsWith("_") || c.isThunk() || c.isExternal()) continue;
                if (!done.add(c.getEntryPoint().toString())) continue;
                dump(out, decomp, monitor, c, 1);
            }
        }
        out.close();
        println("done -> sidecar_out.txt (" + done.size() + " funcs)");
    }

    void dump(PrintWriter out, DecompInterface d, ConsoleTaskMonitor m,
              Function f, int depth) {
        DecompileResults res = d.decompileFunction(f, 180, m);
        out.println("\n================================================================");
        out.println("FUNC " + f.getName() + " @ " + f.getEntryPoint() + "  (depth " + depth + ")");
        out.println("================================================================");
        if (res != null && res.decompileCompleted()) {
            String c = res.getDecompiledFunction().getC();
            if (c.length() > 13000) c = c.substring(0, 13000) + "\n/* ...truncated... */";
            out.println(c);
        } else out.println("  (decompile failed)");
    }
}
