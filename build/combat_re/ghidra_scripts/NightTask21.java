//@category YsOrigin
import ghidra.program.model.address.*;
public class NightTask21 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // A) CRT triage candidate (zlib band)
        dumpFunc(0x006232d0L, "CRT-TRIAGE FUN_006232d0 (zlib band)");
        xrefs(0x006232d0L, "FUN_006232d0 callers");
        // B) game functions
        dumpFunc(0x00475fd0L, "FUN_00475fd0 (object/enemy 0x47)");
        xrefs(0x00475fd0L, "FUN_00475fd0 callers");
        dumpFunc(0x004112c0L, "FUN_004112c0 (boot/D3D 0x41)");
        xrefs(0x004112c0L, "FUN_004112c0 callers");
        dumpFunc(0x005d1320L, "FUN_005d1320 (render/scenegraph 0x5d)");
        xrefs(0x005d1320L, "FUN_005d1320 callers");
        dumpFunc(0x0051b620L, "FUN_0051b620 (object/mesh 0x51)");
        xrefs(0x0051b620L, "FUN_0051b620 callers");
        dumpFunc(0x00541de0L, "FUN_00541de0 (boss 0x54)");
        xrefs(0x00541de0L, "FUN_00541de0 callers");
        close();
    }
}
