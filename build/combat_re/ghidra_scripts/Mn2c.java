//@category YsOrigin
import ghidra.program.model.address.*;

public class Mn2c extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // find the difficulty popup builder (references the "Very Easy" English string)
        xrefs(0x006a1748L, "\"Very Easy\" string");
        xrefs(0x006a175cL, "next diff string (~Easy)");
        // SP bonus popup: find via the [Enable]/[Disable] literals -- search brackets string
        // difficulty run write DAT_0074ac3c (set by the difficulty popup confirm)
        xrefs(0x0074ac3cL, "difficulty select DAT_0074ac3c");
        // SP-bonus config bytes
        xrefs(0x0074e25fL, "SP/run cfg DAT_0074e25f");
        xrefs(0x0074e684L, "run cfg DAT_0074e684");
        xrefs(0x0074e685L, "run cfg DAT_0074e685");
        close();
    }
}
