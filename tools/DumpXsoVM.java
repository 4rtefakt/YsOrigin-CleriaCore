//Decompile the XSO event-script VM interpreter to recover exact opcode
//semantics (control flow, accumulator, register ops). Roots from the RE notes:
//FUN_005663E0 (VM loop top, PC at ctx+0x1e4) and FUN_004472e0 (cited as the VM).
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class DumpXsoVM extends GhidraScript {
    static final long[] ROOTS = { 0x005663E0L, 0x004472e0L, 0x00567D17L /*the grant store*/ };
    static final int DEPTH = 1;
    DecompInterface decomp; ConsoleTaskMonitor monitor; PrintWriter out;
    Set<String> done = new LinkedHashSet<String>();

    public void run() throws Exception {
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        monitor = new ConsoleTaskMonitor();
        out = new PrintWriter(new FileWriter("D:\\ghidra-work\\xso_vm_out.txt"));
        FunctionManager fm = currentProgram.getFunctionManager();
        for (long r : ROOTS) {
            Function f = fm.getFunctionContaining(toAddr(r));
            if (f == null) { out.println("!! none @ " + Long.toHexString(r)); continue; }
            walk(f, 0);
        }
        out.close();
        println("done -> xso_vm_out.txt (" + done.size() + " funcs)");
    }

    void walk(Function f, int depth) {
        String ep = f.getEntryPoint().toString();
        if (done.contains(ep) || depth > DEPTH) return;
        done.add(ep);
        DecompileResults res = decomp.decompileFunction(f, 240, monitor);
        out.println("\n================================================================");
        out.println("FUNC " + f.getName() + " @ " + ep + "  (depth " + depth + ")");
        out.println("================================================================");
        if (res != null && res.decompileCompleted()) {
            String c = res.getDecompiledFunction().getC();
            if (c.length() > 24000) c = c.substring(0, 24000) + "\n/* ...truncated... */";
            out.println(c);
        } else { out.println("  (decompile failed)"); return; }
        Set<Function> callees = f.getCalledFunctions(monitor);
        for (Function c : callees) {
            String n = c.getName();
            if (n.startsWith("_") || c.isThunk() || c.isExternal()) continue;
            walk(c, depth + 1);
        }
    }
}
