//@category YsOrigin
import ghidra.program.model.address.*;
public class DlgPortrait extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The box DRAW (go_wndtext draw) + box OPEN — the face-inset composite must live here.
        dumpFunc(0x0057a6d0L, "go_wndtext DRAW FUN_0057a6d0 (frame strip + face + text)");
        dumpFunc(0x0057a410L, "go_wndtext OPEN FUN_0057a410");
        dumpFunc(0x0057a050L, "go_wndtext font-atlas build FUN_0057a050");
        // The 2D csprite billboard draw the portrait likely routes through.
        dumpFunc(0x00456da0L, "2D csprite draw FUN_00456da0");
        close();
    }
}
