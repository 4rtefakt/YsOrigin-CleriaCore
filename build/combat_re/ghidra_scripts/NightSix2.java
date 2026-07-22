//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSix2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0065eb51L, "CRT triage FUN_0065eb51");
        dumpFunc(0x00589130L, "effect FUN_00589130");
        dumpFunc(0x0052d480L, "boss52 FUN_0052d480");
        dumpFunc(0x00565cc0L, "window56 FUN_00565cc0");
        dumpFunc(0x005d5b20L, "render5d FUN_005d5b20");
        dumpFunc(0x0055c700L, "boss55 FUN_0055c700");

        xrefs(0x0065eb51L, "FUN_0065eb51");
        xrefs(0x00589130L, "FUN_00589130");
        xrefs(0x0052d480L, "FUN_0052d480");
        xrefs(0x00565cc0L, "FUN_00565cc0");
        xrefs(0x005d5b20L, "FUN_005d5b20");
        xrefs(0x0055c700L, "FUN_0055c700");
        close();
    }
}
