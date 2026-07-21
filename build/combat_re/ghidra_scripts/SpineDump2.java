//@category YsOrigin
import ghidra.program.model.address.*;
public class SpineDump2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // --- D3D device + window + input + sound builder (WIRING [UNKNOWN]) ---
        dumpFunc(0x0040fa50L, "device/window builder FUN_0040fa50");
        // --- post-device game init (called by FUN_0040d1d0 after device) ---
        dumpFunc(0x00426c70L, "post-device init FUN_00426c70");
        dumpFunc(0x00439250L, "post-device init2 FUN_00439250");
        // --- master update / top-level state dispatch ---
        dumpFunc(0x004281c0L, "master update FUN_004281c0");
        // --- movie driver: Run + neighbors ---
        dumpFunc(0x0044aae0L, "movie Run FUN_0044aae0");
        dumpFunc(0x005773e0L, "unified media open FUN_005773e0");
        // --- map WinMain & the state graph via xrefs ---
        xrefs(0x0040e210L, "app entry FUN_0040e210 (find WinMain caller)");
        xrefs(0x004155e0L, "config/launcher FUN_004155e0 (find caller)");
        xrefs(0x00427820L, "gameover trigger sites");
        xrefs(0x0041fd10L, "new-game init call sites");
        xrefs(0x0044a4f0L, "movie graph-build call sites");
        // --- movie/ogg filename tables ---
        xrefs(0x006a0650L, "movie (.avi) name table readers");
        close();
    }
}
