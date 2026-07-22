//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.util.*;
public class SixFuncs extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x0043a340L, 0x005cbc40L, 0x00583530L,
            0x0046cb00L, 0x00564c10L, 0x0045d3f0L
        };
        String[] tags = {
            "FUN_0043a340 audio/SE 0x43",
            "FUN_005cbc40 window/audio 0x5c",
            "FUN_00583530 effect/particle-pool 0x58",
            "FUN_0046cb00 player/ability 0x46",
            "FUN_00564c10 render/object-draw 0x56",
            "FUN_0045d3f0 object/mover 0x45"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n----- CALLERS of " + tags[i] + " -----");
            for (Address a : callersOf(fns[i]))
                out.println("  " + a + "   [" + fnName(a) + "]");
        }
        close();
    }
}
