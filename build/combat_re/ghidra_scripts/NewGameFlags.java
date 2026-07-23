//@category YsOrigin
import ghidra.program.model.address.*;
public class NewGameFlags extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x004472e0L, // 161 writes into flag array - primary init candidate
            0x00446750L, // 63 writes into flag array
            0x0041fd10L, // clear loop, DAT_0076b91c=1, WRITE refs to base
            0x00431840L, // WRITE at 431e1a
            0x0043fe00L, // READ at 43fe69
        };
        String[] tags = {
            "FUN_004472e0 (161 flag writes)",
            "FUN_00446750 (63 flag writes)",
            "FUN_0041fd10 (clear loop, base WRITE)",
            "FUN_00431840 (base WRITE 431e1a)",
            "FUN_0043fe00 (base READ 43fe69)",
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n----- CALLERS of " + tags[i] + " -----");
            for (Address a : callersOf(fns[i])) out.println("  " + a + "   [" + fnName(a) + "]");
        }
        // character flag 150 = base + 0x258 = 0x76bb74
        xrefs(0x0076bb74L, "flag150 CHARACTER (0x76bb74)");
        close();
    }
}
