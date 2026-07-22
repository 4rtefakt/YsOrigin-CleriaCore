//@category YsOrigin
import ghidra.program.model.address.*;
public class H6Portrait extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0043c230L, "builder A FUN_0043c230 — portrait select + slot frames");
        xrefs(0x0074a958L, "icon-handle table base — who WRITES it (the loader)");
        xrefs(0x0076a630L, "skill/magic equipped global — all consumers");
        xrefs(0x0076a62cL, "tool equipped global — all consumers");
        xrefs(0x0076bb74L, "char index global — all consumers (portrait select)");
        close();
    }
}
