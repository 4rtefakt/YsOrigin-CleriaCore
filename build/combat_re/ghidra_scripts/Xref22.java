//@category YsOrigin
import ghidra.program.model.address.*;
public class Xref22 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0066bdaeL, "libm log FUN_0066bdae");
        xrefs(0x00620130L, "png_do_shift FUN_00620130");
        xrefs(0x005a02e0L, "effect FUN_005a02e0");
        xrefs(0x005d7b00L, "dsound FUN_005d7b00");
        xrefs(0x005bb540L, "cursor FUN_005bb540");
        xrefs(0x00599510L, "effect FUN_00599510");
        xrefs(0x005420b0L, "boss FUN_005420b0");
        close();
    }
}
