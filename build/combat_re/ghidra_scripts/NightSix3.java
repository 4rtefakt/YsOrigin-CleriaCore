//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSix3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005ee980L, "FUN_005ee980 render/D3D 0x5e ~351B");
        callersOf(0x005ee980L);
        dumpFunc(0x00527b50L, "FUN_00527b50 object/battle 0x52 ~347B");
        callersOf(0x00527b50L);
        dumpFunc(0x005ec580L, "FUN_005ec580 render/D3D 0x5e ~346B");
        callersOf(0x005ec580L);
        dumpFunc(0x00577fc0L, "FUN_00577fc0 window/go_wnd 0x57 ~345B");
        callersOf(0x00577fc0L);
        dumpFunc(0x005404a0L, "FUN_005404a0 effect/link 0x54 ~343B");
        callersOf(0x005404a0L);
        dumpFunc(0x005cf420L, "FUN_005cf420 window/audio 0x5c ~341B");
        callersOf(0x005cf420L);
        close();
    }
}
