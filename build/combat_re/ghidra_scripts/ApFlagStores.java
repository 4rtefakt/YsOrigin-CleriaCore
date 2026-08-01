//@category YsOrigin
// Dumps the stubs the Ys Origin AP mod needs to hook:
//   * 0x67 Flag_AddInt / 0x69 Flag_SubInt  -> the `ADD/SUB [ESI],ECX` stores that
//     bypass DecideStore (spliced at 0x567D17, the 0x64 store only)
//   * 0xdd Menu_AddSpShop / 0xb0 SetRavalCostToFlag -> the vanilla statue shop
import ghidra.program.model.address.*;

public class ApFlagStores extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00567d78L, "0x67 Flag_AddInt handler (ADD [ESI],ECX store)");
        dumpFunc(0x00567e0aL, "0x69 Flag_SubInt handler (SUB [ESI],ECX store)");
        dumpFunc(0x005659e0L, "FUN_005659e0 shared flag-slot accessor");
        dumpFunc(0x0056a149L, "0xdd Menu_AddSpShop(label,item,price)");
        dumpFunc(0x005691fdL, "0xb0 SetRavalCostToFlag (armor/boots cost)");
        dumpFunc(0x005747a0L, "FUN_005747a0 shared Menu_Add worker");
        close();
    }
}
