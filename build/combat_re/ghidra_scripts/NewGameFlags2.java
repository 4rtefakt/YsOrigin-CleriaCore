//@category YsOrigin
import ghidra.program.model.address.*;
public class NewGameFlags2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00442260L, "FUN_00442260 caller of init");
        dumpFunc(0x0042fb90L, "FUN_0042fb90 caller of init (new-game?)");
        // callers of the two callers, to reach char-select confirm
        out.println("\n----- CALLERS of FUN_00442260 -----");
        for (Address a : callersOf(0x00442260L)) out.println("  " + a + "   [" + fnName(a) + "]");
        out.println("\n----- CALLERS of FUN_0042fb90 -----");
        for (Address a : callersOf(0x0042fb90L)) out.println("  " + a + "   [" + fnName(a) + "]");
        out.println("\n----- CALLERS of FUN_0042f5e0 (load-read) -----");
        for (Address a : callersOf(0x0042f5e0L)) out.println("  " + a + "   [" + fnName(a) + "]");
        // Story-flag address writers (absolute). base 0x76b91c.
        xrefs(0x0076bcc8L, "flag235 (0x76bcc8) writers?");   // 235*4=0x3ac
        xrefs(0x0076bcbcL, "flag232 (0x76bcbc)");
        xrefs(0x0076bcc4L, "flag234 (0x76bcc4)");
        xrefs(0x0076bcccL, "flag236 (0x76bccc)");
        xrefs(0x0076bca4L, "flag226 (0x76bca4)");
        xrefs(0x0076bc9cL, "flag224 (0x76bc9c)");
        xrefs(0x0076bb78L, "flag151 (0x76bb78)");
        xrefs(0x0076bb7cL, "flag152 (0x76bb7c)");
        xrefs(0x0076bcacL, "flag228 (0x76bcac)");
        close();
    }
}
