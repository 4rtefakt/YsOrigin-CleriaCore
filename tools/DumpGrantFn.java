//Decompile the event-VM function that contains the grant store 0x567D17 (and
//thus the flag-test sub-op 0x5F that sets the accumulator). Recovers the exact
//test semantics needed for branch-accurate XSO execution.
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;

public class DumpGrantFn extends GhidraScript {
    public void run() throws Exception {
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        ConsoleTaskMonitor m = new ConsoleTaskMonitor();
        PrintWriter out = new PrintWriter(new FileWriter("D:\\ghidra-work\\grant_fn_out.txt"));
        long[] addrs = { 0x00567D17L, 0x005663E0L };
        java.util.LinkedHashSet<String> seen = new java.util.LinkedHashSet<String>();
        for (long a : addrs) {
            Function f = getFunctionContaining(toAddr(a));
            if (f == null) { out.println("!! no function @ " + Long.toHexString(a)); continue; }
            if (!seen.add(f.getEntryPoint().toString())) continue;
            DecompileResults r = d.decompileFunction(f, 300, m);
            out.println("\n==== FUNC " + f.getName() + " @ " + f.getEntryPoint()
                        + " (contains " + Long.toHexString(a) + ") ====");
            if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
            else out.println("(decompile failed)");
        }
        out.close();
        println("done -> grant_fn_out.txt");
    }
}
