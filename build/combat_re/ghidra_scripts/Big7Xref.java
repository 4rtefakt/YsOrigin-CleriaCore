//@category YsOrigin
import ghidra.program.model.address.*;
public class Big7Xref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00599770L, "FUN_00599770");
        xrefs(0x005cac90L, "FUN_005cac90");
        xrefs(0x0052d6d0L, "FUN_0052d6d0");
        xrefs(0x00589f60L, "FUN_00589f60");
        xrefs(0x0052b6e0L, "FUN_0052b6e0");
        xrefs(0x0058fa00L, "FUN_0058fa00");
        xrefs(0x005416a0L, "FUN_005416a0");
        // key callees for identity confirmation
        xrefs(0x00593550L, "FUN_00593550 (GoXx spawn?)");
        xrefs(0x005a39a0L, "FUN_005a39a0 (window ctor?)");
        close();
    }
}
