//@category YsOrigin
import ghidra.program.model.address.*;
public class Ef2TrkB extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0069f088L, "GoParticle::vftable (find ctor/update-slot writers)");
        xrefs(0x0073012cL, "particle live count DAT_0073012c (find update loop)");
        close();
    }
}
