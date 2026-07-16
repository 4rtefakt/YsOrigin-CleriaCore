//Decompile the YMO mesh sub-parser FUN_005f4420 and everything it calls
//(depth 2), plus the bone sub-parser FUN_005f3c70. This is the vertex/index
//format -- the last real unknown in YMO.
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class DumpYmoMesh extends GhidraScript {

    static final long[] ROOTS = {
        0x005f4420L,  // mesh sub-parser   <- THE TARGET
        0x005f3c70L,  // bone sub-parser
    };
    static final int DEPTH = 2;

    DecompInterface decomp;
    ConsoleTaskMonitor monitor;
    PrintWriter out;
    Set<String> done = new LinkedHashSet<String>();

    public void run() throws Exception {
        decomp = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        decomp.setOptions(opts);
        decomp.openProgram(currentProgram);
        monitor = new ConsoleTaskMonitor();
        out = new PrintWriter(new FileWriter("D:\\ghidra-work\\ymo_mesh_out.txt"));

        FunctionManager fm = currentProgram.getFunctionManager();
        for (long r : ROOTS) {
            Function f = fm.getFunctionContaining(toAddr(r));
            if (f == null) { out.println("!! no function at " + Long.toHexString(r)); continue; }
            walk(f, 0);
        }
        out.close();
        println("done -> ymo_mesh_out.txt (" + done.size() + " funcs)");
    }

    void walk(Function f, int depth) {
        String ep = f.getEntryPoint().toString();
        if (done.contains(ep) || depth > DEPTH) return;
        done.add(ep);

        DecompileResults res = decomp.decompileFunction(f, 180, monitor);
        out.println("\n================================================================");
        out.println("FUNC " + f.getName() + " @ " + ep + "   (depth " + depth + ")");
        out.println("  sig: " + f.getSignature().getPrototypeString());
        out.println("================================================================");
        if (res != null && res.decompileCompleted()) {
            out.println(res.getDecompiledFunction().getC());
        } else {
            out.println("  (decompile failed)");
            return;
        }

        // recurse into callees, skipping obvious CRT/alloc noise
        Set<Function> callees = f.getCalledFunctions(monitor);
        List<Function> next = new ArrayList<Function>();
        for (Function c : callees) {
            String n = c.getName();
            if (n.startsWith("_") || n.startsWith("__") || n.contains("alloc")
                || n.contains("memset") || n.contains("memcpy")
                || c.isThunk() || c.isExternal()) continue;
            next.add(c);
        }
        out.println("\n  /* callees of " + f.getName() + ": " + next.size() + " */");
        for (Function c : next) walk(c, depth + 1);
    }
}
