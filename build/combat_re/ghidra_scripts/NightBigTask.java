//@category YsOrigin
import ghidra.program.model.address.*;
public class NightBigTask extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0065e916L, "CRT triage FUN_0065e916");
        dumpFunc(0x0051aec0L, "mesh region FUN_0051aec0");
        dumpFunc(0x0044b7a0L, "aso/resource FUN_0044b7a0");
        dumpFunc(0x0058c650L, "effect FUN_0058c650");
        dumpFunc(0x005d9d40L, "render/scenegraph FUN_005d9d40");
        dumpFunc(0x004a2f90L, "enemy/boss FUN_004a2f90");
        callersOf(0x0065e916L);
        callersOf(0x0051aec0L);
        callersOf(0x0044b7a0L);
        callersOf(0x0058c650L);
        callersOf(0x005d9d40L);
        callersOf(0x004a2f90L);
        xrefs(0x0069b980L, "enemy handler table 0x69b980");
        close();
    }
}
