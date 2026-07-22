//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn8Map extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005ab0d0L, "Map-overlay BUILDER FUN_005ab0d0 (MN5 flag)");
        dumpFunc(0x005acf90L, "MAP.dds BLIT FUN_005acf90");
        dumpFunc(0x005abd10L, "item-action + map-teleport STATE MACHINE FUN_005abd10");
        // region marker table + related globals
        xrefs(0x00701730L, "region marker table DAT_00701730 (stride?)");
        xrefs(0x00701744L, "DAT_00701744 (link matrix?)");
        xrefs(0x00701750L, "DAT_00701750 (link matrix?)");
        xrefs(0x0076c11cL, "warp-enable flag array (&DAT_0076c11c)[region]");
        xrefs(0x0076bb40L, "warp target DAT_0076bb40");
        xrefs(0x0073a0dcL, "region cursor DAT_0073a0dc");
        xrefs(0x00739160L, "warp-requested DAT_00739160");
        xrefs(0x00739164L, "map pan/scroll DAT_00739164");
        xrefs(0x00738e60L, "MAP.dds handle DAT_00738e60");
        xrefs(0x007390b0L, "you-are-here/selected marker sprite _DAT_007390b0");
        xrefs(0x00738e00L, "region marker sprite base DAT_00738e00");
        xrefs(0x00739168L, "region markers DAT_00739168");
        xrefs(0x00738b40L, "map/item state DAT_00738b40");
        close();
    }
}
