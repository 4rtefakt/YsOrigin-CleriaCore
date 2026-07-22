//@category YsOrigin
import ghidra.program.model.address.*;
public class Task398b extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0053a760L, "FUN_0053a760 caller of arena-bounds");
        dumpFunc(0x00403870L, "FUN_00403870 Steam stat wrapper?");
        dumpFunc(0x0041f570L, "FUN_0041f570 sqrt helper?");
        // dispatch context: what subtype routes to 0x58aeb0 (site 0x58fd4d)
        out.println("\n==== dispatcher context around 0x58fd4d ====");
        for (long a = 0x58fd20L; a <= 0x58fd60L; a += 1) {} // noop
        xrefs(0x007493beL, "Steam-init flag be");
        xrefs(0x0072ffa0L, "launcher DAT_0072ffa0");
        close();
    }
}
