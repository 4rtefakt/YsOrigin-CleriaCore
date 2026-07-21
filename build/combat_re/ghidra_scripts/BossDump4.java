//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class BossDump4 extends Re2Lib {
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
        for (Function f : fs)
            out.println(String.format("  %-14s size=%-6d", f.getName(), f.getBody().getNumAddresses()));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00521130L, "boss03 BRAIN FUN_00521130 (1124)");
        dumpFunc(0x0052d1c0L, "boss04 BRAIN FUN_0052d1c0 (448)");
        dumpFunc(0x00544980L, "boss06 BRAIN FUN_00544980 (1403)");
        dumpFunc(0x0055ddc0L, "boss08 BRAIN FUN_0055ddc0");
        band("boss08-tail", 0x556000L, 0x560000L);
        close();
    }
}
