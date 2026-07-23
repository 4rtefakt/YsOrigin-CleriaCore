//@category YsOrigin
public class WndMenu extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005744f0L, "Window_Menu worker FUN_005744f0 (alloc)");
        dumpFunc(0x00578440L, "menu add-first-item FUN_00578440");
        dumpFunc(0x00578710L, "menu finalize FUN_00578710");
        // The menu window class draw: find it via callers/xrefs of the finalize obj is
        // hard statically; dump the GoWnd draw candidates adjacent to GoWndText's 0x57a6d0.
        dumpFunc(0x00578e00L, "candidate menu draw A");
        dumpFunc(0x00579200L, "candidate menu draw B");
        close();
    }
}
