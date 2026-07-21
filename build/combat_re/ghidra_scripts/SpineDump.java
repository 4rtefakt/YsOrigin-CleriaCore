//@category YsOrigin
import ghidra.program.model.address.*;
public class SpineDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // --- app entry / boot ---
        dumpFunc(0x0040e150L, "app_entry FUN_0040e150 (WinMain?)");
        dumpFunc(0x0040e210L, "app_entry FUN_0040e210 (entry - builds pools/D3D/scene)");
        dumpFunc(0x0040d1d0L, "FUN_0040d1d0 CoInitialize + sets DAT_0074c120=1");
        dumpFunc(0x0040dcc0L, "FUN_0040dcc0 CoUninitialize / shutdown");
        dumpFunc(0x00415510L, "config_entry FUN_00415510");
        dumpFunc(0x0041fd10L, "g_flags new-game memset FUN_0041fd10");
        // --- main loop / step / gameover ---
        dumpFunc(0x004106e0L, "sim step FUN_004106e0");
        dumpFunc(0x00427820L, "gameover path FUN_00427820");
        // --- movie / FMV (DirectShow COM) ---
        dumpFunc(0x0044a4f0L, "movie player FUN_0044a4f0 (CoCreateInstance)");
        callersOf(0x0044a4f0L);
        dumpFunc(0x0044cda0L, "FUN_0044cda0 (movie neighbor / big unknown)");
        // --- biggest unknown functions ---
        dumpFunc(0x0043c230L, "big unknown FUN_0043c230 (9.6KB)");
        dumpFunc(0x0043fe00L, "big unknown FUN_0043fe00 (8.3KB)");
        dumpFunc(0x0043e800L, "big unknown FUN_0043e800");
        // --- scene subsystem init / pools ---
        dumpFunc(0x00442260L, "scene subsystem init FUN_00442260");
        dumpFunc(0x00421830L, "pool builder FUN_00421830");
        dumpFunc(0x00428ec0L, "destroy-ALL FUN_00428ec0");
        close();
    }
}
