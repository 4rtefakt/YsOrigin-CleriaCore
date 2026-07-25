//@category YsOrigin
// FUN_00593550 packs FireBullet's arg[0] (kind) and arg[1] (130 for Hugo) into a struct
// and hands it to FUN_00593b90. That is where the visual effect id is actually resolved —
// the brief's chain says arg[1] goes through DAT_0074e674, i.e. 130 may be an index into a
// bullet-definition table rather than an EFTLIST id.
import ghidra.program.model.mem.*;

public class BulletId extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00593b90L, "bullet init FUN_00593b90 (resolves the visual)");
        xrefs(0x0074e674L, "DAT_0074e674 (candidate bullet-definition table)");
        // if it is a table of dwords, show the neighbourhood around index 130
        Memory m = currentProgram.getMemory();
        out.println("\n===== DAT_0074e674 as dwords, idx 0..8 and 126..134 =====");
        for (int i : new int[]{0,1,2,3,4,5,6,7,8,126,127,128,129,130,131,132,133,134}) {
            try {
                int v = m.getInt(toAddr(0x0074e674L + i * 4L));
                out.println(String.format("  [%3d] = 0x%08x  (%d)", i, v, v));
            } catch (Exception e) { out.println("  [" + i + "] unreadable"); }
        }
        close();
    }
}
