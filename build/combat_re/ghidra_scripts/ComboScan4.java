//@category YsOrigin
// FUN_0057xxxx contains the only *computed* writes of actor+0x994 (the action / ASO
// section id).  Dump it and list its callers -- that is the engine's "play this
// action" entry point, and the player's combo chain must go through it.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class ComboScan4 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Address a = toAddr(0x0057104eL);
        Function f = fnAt(a);
        out.println("containing fn = " + (f == null ? "(none)" : f.getName() + " @ " + f.getEntryPoint()));
        if (f != null) {
            dumpFunc(f.getEntryPoint().getOffset(), "action setter (writes +0x994)");
            out.println("\n===== CALLERS =====");
            for (Address c : callersOf(f.getEntryPoint().getOffset()))
                out.println("  " + c + "   [" + fnName(c) + "]");
        }
        close();
    }
}
