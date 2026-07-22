//@category YsOrigin
// MN10: name the DDS behind the shop backdrop/pillar texture slots bound in FUN_005c62d0.
import ghidra.program.model.address.*;
public class ShopTex extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0075afc4L, "backdrop PLATE tex slot DAT_0075afc4 (Feena illustration?) - find loader");
        xrefs(0x0075afacL, "pillar WS tex slot DAT_0075afac - find loader");
        xrefs(0x0075afa8L, "pillar 4:3 tex slot DAT_0075afa8 - find loader");
        xrefs(0x0075aee8L, "rune-strip tex slot DAT_0075aee8 - find loader");
        // Scene-mode setter that gates the shop (sole writer of both gates).
        dumpFunc(0x004106e0L, "scene-mode setter FUN_004106e0 (shop gate writer)");
        close();
    }
}
