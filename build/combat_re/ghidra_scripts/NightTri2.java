//@category YsOrigin
import ghidra.program.model.address.*;
public class NightTri2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // A) CRT triage
        dumpFunc(0x00623b00L, "A) CRT triage FUN_00623b00");
        xrefs(0x00623b00L, "FUN_00623b00 callers");
        // B) game functions
        dumpFunc(0x00474870L, "B1 FUN_00474870 object/enemy");
        xrefs(0x00474870L, "FUN_00474870 callers");
        dumpFunc(0x005453d0L, "B2 FUN_005453d0 boss region");
        xrefs(0x005453d0L, "FUN_005453d0 callers");
        dumpFunc(0x005d43d0L, "B3 FUN_005d43d0 render layer");
        xrefs(0x005d43d0L, "FUN_005d43d0 callers");
        dumpFunc(0x00574970L, "B4 FUN_00574970 window/text");
        xrefs(0x00574970L, "FUN_00574970 callers");
        dumpFunc(0x005f76d0L, "B5 FUN_005f76d0 render/D3D");
        xrefs(0x005f76d0L, "FUN_005f76d0 callers");
        dumpFunc(0x00473c60L, "B6 FUN_00473c60 object/enemy");
        xrefs(0x00473c60L, "FUN_00473c60 callers");
        dumpFunc(0x00552120L, "B7 FUN_00552120");
        xrefs(0x00552120L, "FUN_00552120 callers");
        close();
    }
}
