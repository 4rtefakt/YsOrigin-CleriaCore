//@category YsOrigin
// Round 3: the concrete internals for future features (save I/O bytes, map blit, buy deduction, book frame).
import ghidra.program.model.address.*;
public class NightDump3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005a73c0L, "book frame FUN_005a73c0 (BACK00/tabs/wordmark/difficulty word)");
        dumpFunc(0x0042e9a0L, "save write FUN_0042e9a0");
        dumpFunc(0x0042f5e0L, "load read FUN_0042f5e0");
        dumpFunc(0x0042f510L, "save delete FUN_0042f510");
        dumpFunc(0x005acf90L, "map overlay draw FUN_005acf90 (MAP.DDS blit)");
        dumpFunc(0x005c6f40L, "bonus shop buy/price handler FUN_005c6f40");
        dumpFunc(0x0041fd10L, "player character set FUN_0041fd10 (writes DAT_0076bb74)");
        close();
    }
}
