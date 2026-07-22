//@category YsOrigin
import ghidra.program.model.address.*;
public class TriageXref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00628ff4L, "FUN_00628ff4");
        xrefs(0x00652b3eL, "FUN_00652b3e");
        xrefs(0x005886b0L, "FUN_005886b0");
        xrefs(0x0053ff70L, "FUN_0053ff70");
        xrefs(0x005bb1b0L, "FUN_005bb1b0");
        xrefs(0x0059b430L, "FUN_0059b430");
        xrefs(0x005f8a50L, "FUN_005f8a50");
        // helpers referenced, to pin subsystem
        xrefs(0x005e9380L, "FUN_005e9380 (helper of 5886b0)");
        xrefs(0x005d33f0L, "FUN_005d33f0 (helper of 5bb1b0)");
        close();
    }
}
