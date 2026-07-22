//@category YsOrigin
import ghidra.program.model.address.*;
public class Task244 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0051f420L, "obj/mesh 0x51 FUN_0051f420");
        xrefs(0x0051f420L, "FUN_0051f420");
        dumpFunc(0x004fff30L, "player/effect 0x50 FUN_004fff30");
        xrefs(0x004fff30L, "FUN_004fff30");
        dumpFunc(0x005f4f10L, "render/YMO 0x5f FUN_005f4f10");
        xrefs(0x005f4f10L, "FUN_005f4f10");
        dumpFunc(0x0044e850L, "obj/ASO 0x44 FUN_0044e850");
        xrefs(0x0044e850L, "FUN_0044e850");
        dumpFunc(0x005a4760L, "window/pause 0x5a FUN_005a4760");
        xrefs(0x005a4760L, "FUN_005a4760");
        dumpFunc(0x005ebe90L, "render/D3D 0x5e FUN_005ebe90");
        xrefs(0x005ebe90L, "FUN_005ebe90");
        close();
    }
}
