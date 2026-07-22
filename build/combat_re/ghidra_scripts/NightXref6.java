//@category YsOrigin
import ghidra.program.model.address.*;
public class NightXref6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0053f6e0L, "boss FUN_0053f6e0");
        xrefs(0x00596b30L, "effect FUN_00596b30");
        xrefs(0x00579f10L, "window FUN_00579f10");
        xrefs(0x006984acL, "camera vtable-ish 0x6984ac (what fn is this in / near)");
        xrefs(0x006a0a14L, "GoWndText::vftable 0x6a0a14");
        dumpFunc(0x00671000L, "round helper FUN_00671000");
        dumpFunc(0x005357c0L, "boss pos-source FUN_005357c0");
        xrefs(0x0074c09cL, "DAT_0074c09c (boss child ptr)");
        xrefs(0x00765552L, "DAT_00765552 (render mode flag)");
        close();
    }
}
