//@category YsOrigin
import ghidra.program.model.address.*;
public class NightSix2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00444fe0L, "FUN_00444fe0 ASO-decoder caller");
        dumpFunc(0x00444850L, "FUN_00444850 anim-list accessor");
        dumpFunc(0x004039f0L, "FUN_004039f0 config-fam read");
        dumpFunc(0x00403560L, "FUN_00403560 config-fam write");
        dumpFunc(0x00403870L, "FUN_00403870 config-fam");
        xrefs(0x005e1220L, "FUN_005e1220 render loader (find vtable/callers)");
        xrefs(0x0059ffe0L, "FUN_0059ffe0 effect tick (find vtable)");
        xrefs(0x005cd860L, "FUN_005cd860 audio shutdown (find table)");
        close();
    }
}
