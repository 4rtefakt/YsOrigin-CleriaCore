//@category YsOrigin
import ghidra.program.model.address.*;
public class NightRender6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long[] fns = {
            0x00565ab0L, 0x005e9870L, 0x00563d80L,
            0x005d6170L, 0x00564110L, 0x005d9730L
        };
        String[] tags = {
            "FUN_00565ab0 (~307B)", "FUN_005e9870 (~304B)", "FUN_00563d80 (~300B)",
            "FUN_005d6170 (~299B)", "FUN_00564110 (~298B)", "FUN_005d9730 (~297B)"
        };
        for (int i = 0; i < fns.length; i++) {
            dumpFunc(fns[i], tags[i]);
            out.println("\n---- CALLERS OF " + tags[i] + " ----");
            for (Address a : callersOf(fns[i])) out.println("  " + a + "  in " + fnName(a));
        }
        close();
    }
}
