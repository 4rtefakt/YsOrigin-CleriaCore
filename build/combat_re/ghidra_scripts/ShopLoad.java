//@category YsOrigin
// MN10: FUN_005c5800 loads the shop backdrop plate (DAT_0075afc4) -> find the DDS name(s).
import ghidra.program.model.address.*;
public class ShopLoad extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005c5800L, "shop LOADER FUN_005c5800 (backdrop/Feena DDS)");
        dumpFunc(0x005c5690L, "GoWndShop2 dtor2 FUN_005c5690");
        // The shop-owned texture table iterated in the dtor (stride 0x14 from 0x75aed8).
        xrefs(0x0075aed8L, "shop texture table base DAT_0075aed8 (loaders)");
        close();
    }
}
