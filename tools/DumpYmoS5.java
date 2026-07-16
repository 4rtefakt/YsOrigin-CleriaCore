//Close the last YMO gap: section [5]'s sub-record parser FUN_005f4110,
//and FUN_005f4e10 (the name->texture-handle resolver used by both meshes
//and nodes).
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;

public class DumpYmoS5 extends GhidraScript {
    static final long[] ROOTS = {
        0x005f4110L,  // section[5] sub-record parser  <- last unknown
        0x005f4e10L,  // name -> texture handle resolver
        0x005f4390L,  // submesh sort comparator (draw order)
    };

    public void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
        PrintWriter out = new PrintWriter(new FileWriter("D:\\ghidra-work\\ymo_s5_out.txt"));
        FunctionManager fm = currentProgram.getFunctionManager();

        for (long r : ROOTS) {
            Function f = fm.getFunctionContaining(toAddr(r));
            if (f == null) { out.println("!! no function at " + Long.toHexString(r)); continue; }
            DecompileResults res = decomp.decompileFunction(f, 180, monitor);
            out.println("\n================================================================");
            out.println("FUNC " + f.getName() + " @ " + f.getEntryPoint());
            out.println("================================================================");
            if (res != null && res.decompileCompleted())
                out.println(res.getDecompiledFunction().getC());
            else
                out.println("  (decompile failed)");
        }
        out.close();
        println("done -> ymo_s5_out.txt");
    }
}
