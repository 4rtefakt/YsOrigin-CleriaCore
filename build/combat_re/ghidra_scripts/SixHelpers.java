//@category YsOrigin
import ghidra.program.model.address.*;
public class SixHelpers extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0041f570L, "hlp FUN_0041f570 (called by 5ddf40)");
        dumpFunc(0x0066f80aL, "hlp FUN_0066f80a (called by 5d2aa0 - atan2?)");
        dumpFunc(0x004217f0L, "hlp FUN_004217f0 (round helper)");
        dumpFunc(0x00402950L, "hlp FUN_00402950 (GOG obj)");
        dumpFunc(0x00566190L, "hlp FUN_00566190 (event script name lookup)");
        xrefs(0x00689ad8L, "689ad8 - vtable slot holding FUN_00406440");
        xrefs(0x006955bcL, "6955bc - vtable holding FUN_004178b0 dtor");
        close();
    }
}
