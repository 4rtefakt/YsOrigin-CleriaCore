//@category YsOrigin
import ghidra.program.model.address.*;
public class SixDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005edc80L, "FUN_005edc80 render/D3D 0x5e");
        callersOf(0x005edc80L);
        dumpFunc(0x0044a380L, "FUN_0044a380 object/ASO 0x44");
        callersOf(0x0044a380L);
        dumpFunc(0x005da4c0L, "FUN_005da4c0 render/scenegraph 0x5d");
        callersOf(0x005da4c0L);
        dumpFunc(0x005d9990L, "FUN_005d9990 render/scenegraph 0x5d");
        callersOf(0x005d9990L);
        dumpFunc(0x004fabf0L, "FUN_004fabf0 player/combat 0x4f");
        callersOf(0x004fabf0L);
        dumpFunc(0x004057d0L, "FUN_004057d0 core/Steam 0x40");
        callersOf(0x004057d0L);
        close();
    }
}
