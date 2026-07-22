//@category YsOrigin
import ghidra.program.model.address.*;
public class SizeBand6 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005de050L, "FUN_005de050 render/scenegraph 0x5d");
        callersOf(0x005de050L);
        dumpFunc(0x00411f50L, "FUN_00411f50 core/boot 0x41");
        callersOf(0x00411f50L);
        dumpFunc(0x00404c60L, "FUN_00404c60 core/Steam 0x40");
        callersOf(0x00404c60L);
        dumpFunc(0x00475e60L, "FUN_00475e60 object/enemy 0x47");
        callersOf(0x00475e60L);
        dumpFunc(0x0040af50L, "FUN_0040af50 core/boot 0x40");
        callersOf(0x0040af50L);
        dumpFunc(0x004f8d30L, "FUN_004f8d30 player/combat 0x4f");
        callersOf(0x004f8d30L);
        close();
    }
}
