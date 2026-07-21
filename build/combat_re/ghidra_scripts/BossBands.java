//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class BossBands extends Re2Lib {

    // list every function whose entry is in [lo,hi), with size, sorted by address
    void band(String name, long lo, long hi) {
        out.println("\n########## BAND " + name + "  [0x" + Long.toHexString(lo) + ", 0x" + Long.toHexString(hi) + ") ##########");
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(toAddr(lo), true);
        List<Function> fs = new ArrayList<>();
        while (it.hasNext()) {
            Function f = it.next();
            long ep = f.getEntryPoint().getOffset();
            if (ep >= hi) break;
            if (ep < lo) continue;
            fs.add(f);
        }
        // print with size
        for (Function f : fs) {
            long sz = f.getBody().getNumAddresses();
            out.println(String.format("  %-14s size=%-6d", f.getName(), sz));
        }
        // top-6 by size
        fs.sort((a,b) -> Long.compare(b.getBody().getNumAddresses(), a.getBody().getNumAddresses()));
        out.println("  --- TOP 8 by size ---");
        for (int i = 0; i < Math.min(8, fs.size()); i++) {
            Function f = fs.get(i);
            out.println(String.format("    %-14s %d", f.getName(), f.getBody().getNumAddresses()));
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        band("boss02", 0x514200L, 0x51ffe0L);
        band("boss03", 0x51ffe0L, 0x527cc0L);
        band("boss04", 0x527cc0L, 0x52d9e0L);
        band("boss05", 0x52d9e0L, 0x53ab20L);
        band("boss06", 0x53ab20L, 0x545940L);
        band("boss07", 0x545940L, 0x550aa0L);
        band("boss08", 0x550aa0L, 0x556000L);
        close();
    }
}
