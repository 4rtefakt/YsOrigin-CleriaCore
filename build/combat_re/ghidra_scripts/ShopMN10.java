//@category YsOrigin
// MN10 Bonus Shop deep-RE: resolve the Feena key-art source + backdrop csprite binding.
import ghidra.program.model.address.*;
public class ShopMN10 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // PRIMARY: the fn that populates the whole backdrop csprite region 0x744240..0x744760
        //          (Feena csprite's texture handle should be bound here).
        dumpFunc(0x005c62d0L, "shop backdrop/csprite SETUP FUN_005c62d0 (Feena bind?)");
        // The art drawer called immediately before the priced list in the host.
        dumpFunc(0x005c69b0L, "shop art drawer FUN_005c69b0");
        // Scene enter/exit candidates (n2 map: touch 73261c/732620/73b8bc/73b970).
        dumpFunc(0x005c5490L, "shop scene enter? FUN_005c5490");
        dumpFunc(0x005c5690L, "shop scene enter2? FUN_005c5690");
        // Who sets the shop update/draw gates -> that caller loads the shop assets.
        xrefs(0x0074c0f0L, "shop UPDATE gate DAT_0074c0f0 (writer = scene entry/asset load)");
        xrefs(0x0074c0f1L, "shop DRAW gate DAT_0074c0f1 (writer)");
        // Who populates the shop icon/texture table 0x745478 (row art source).
        xrefs(0x00745478L, "shop icon/tex table DAT_00745478 (populator)");
        // The 3 backdrop csprite bases (from FUN_005c6e20) - find who binds their textures.
        xrefs(0x00744248L, "backdrop plate csprite 0x744248 (binder)");
        xrefs(0x007445a4L, "backdrop 2-entry csprite table 0x7445a4 (binder / Feena)");
        xrefs(0x007444f8L, "optional backdrop plate 0x7444f8 (binder)");
        close();
    }
}
