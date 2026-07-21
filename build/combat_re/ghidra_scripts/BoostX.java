//@category YsOrigin
import ghidra.program.model.address.*;
public class BoostX extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Boost mode flag: readers give the damage multiplier + duration timer
        xrefs(0x0076bb50L, "DAT_0076bb50 boost-mode flag");
        // Boost gauge: writers give Yunica/Hugo gauge build
        xrefs(0x0076c1b4L, "DAT_0076c1b4 boost gauge");
        // equipped-skill id
        xrefs(0x0076a630L, "DAT_0076a630 equipped-skill id");
        // mode bitfield
        xrefs(0x0076c198L, "DAT_0076c198 mode bitfield");
        // boost 'ready' gate
        xrefs(0x0076bc08L, "DAT_0076bc08 boost-enabled gate");
        // secondary boost globals seen in mover
        xrefs(0x0076bcd8L, "DAT_0076bcd8");
        xrefs(0x0076bc10L, "DAT_0076bc10 (guess boost timer)");
        close();
    }
}
