//@category YsOrigin
// Round 2: functions the arena/shop agent flagged as NEEDS GHIDRA DUMP.
import ghidra.program.model.address.*;
public class NightDump2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Bonus Shop host scene / Feena key-art + background
        dumpFunc(0x005c5b20L, "bonus shop host FUN_005c5b20 (Feena/bg)");
        dumpFunc(0x005c6e20L, "bonus shop host2 FUN_005c6e20");
        dumpFunc(0x005c5ab0L, "shop register/tick FUN_005c5ab0");
        // The 4-bar (Adol) character select writes DAT_0076bb74 = 0..3 -> find its writer
        xrefs(0x0076bb74L, "view character index DAT_0076bb74 (find 4-bar arena char-select writer)");
        // Buy handlers deduct Arena SP DAT_0074c128 -> find the writers (price-deduction + confirm)
        xrefs(0x0074c128L, "Arena SP DAT_0074c128 (find buy handlers/writers)");
        // Arena stage tables + helpers
        dumpFunc(0x00406210L, "arena helper FUN_00406210");
        xrefs(0x0068c460L, "arena stage-count table DAT_0068c460 (readers)");
        close();
    }
}
