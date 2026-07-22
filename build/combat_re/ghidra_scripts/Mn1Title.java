//@category YsOrigin
import ghidra.program.model.address.*;
public class Mn1Title extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Title window: constructor, sprite setup, per-frame tick + menu build
        dumpFunc(0x005755c0L, "GoWndDemo ctor FUN_005755c0");
        dumpFunc(0x00576020L, "title sprite setup FUN_00576020 (pillars/splash rects+UV)");
        dumpFunc(0x005765c0L, "title tick + menu build FUN_005765c0 (iVar6/originY)");
        dumpFunc(0x005758f0L, "title input/anim helper FUN_005758f0");
        dumpFunc(0x00577ce0L, "attract GoWndZzz spawn FUN_00577ce0");
        dumpFunc(0x00577ee0L, "title helper FUN_00577ee0");
        // go_wnddialog framework: origin/header/cursor, finalizer, draw fn (colors!)
        dumpFunc(0x00578440L, "menu origin/header/cursor FUN_00578440");
        dumpFunc(0x00578680L, "menu add-item FUN_00578680");
        dumpFunc(0x00578710L, "menu finalizer/layout FUN_00578710");
        dumpFunc(0x00578f80L, "go_wnddialog DRAW FUN_00578f80 (text colors, shadow, cursor bar)");
        dumpFunc(0x005781f0L, "alt finalizer (event-VM caption) FUN_005781f0");
        // cursor/frame chrome texture identity (MENU_LAYOUT open item)
        xrefs(0x00749d60L, "popup FRAME tex handle DAT_00749d60");
        xrefs(0x00749d68L, "popup CURSOR tex handle DAT_00749d68");
        close();
    }
}
