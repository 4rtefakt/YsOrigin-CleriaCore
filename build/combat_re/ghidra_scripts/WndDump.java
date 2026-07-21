//@category YsOrigin
import ghidra.program.model.address.*;
public class WndDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ---- Group 1: csprite primitive & submission ----
        dumpFunc(0x005a43a0L, "CSPRITE DRAW PRIMITIVE FUN_005a43a0 (DrawPrimitiveUP)");
        dumpFunc(0x00576020L, "csprite.cpp anchor / title sprite setup FUN_00576020");
        dumpFunc(0x005a3850L, "csprite.cpp anchor FUN_005a3850");
        dumpFunc(0x005a39a0L, "menu-tex lazy loader FUN_005a39a0");

        // ---- Group 2: book runtime core ----
        dumpFunc(0x005a5130L, "book tick FUN_005a5130");
        dumpFunc(0x005a51a0L, "book update dispatch FUN_005a51a0");
        dumpFunc(0x005a5d50L, "book draw switch FUN_005a5d50");
        dumpFunc(0x005a73c0L, "frame/tab-bar draw FUN_005a73c0");
        dumpFunc(0x005b0130L, "caption/info bar FUN_005b0130");

        // ---- Group 3: go_wndmain2.cpp asserted fns (Topic pages / book internals) ----
        dumpFunc(0x005bce30L, "go_wndmain2 FUN_005bce30");
        dumpFunc(0x005bdca0L, "go_wndmain2 FUN_005bdca0");
        dumpFunc(0x005bdfd0L, "go_wndmain2 FUN_005bdfd0");
        dumpFunc(0x005bff40L, "go_wndmain2 FUN_005bff40");
        dumpFunc(0x005c1d20L, "go_wndmain2 FUN_005c1d20");
        dumpFunc(0x005c2110L, "go_wndmain2 (page8 builder) FUN_005c2110");

        // ---- Group 4: go_wnddialog / go_wndtext ----
        dumpFunc(0x005781f0L, "go_wnddialog FUN_005781f0");
        dumpFunc(0x00578710L, "go_wnddialog finalize/layout FUN_00578710");
        dumpFunc(0x00578680L, "go_wnddialog add-item FUN_00578680");
        dumpFunc(0x00578440L, "go_wnddialog cursor/header FUN_00578440");
        dumpFunc(0x0057a050L, "go_wndtext FUN_0057a050");
        dumpFunc(0x0057a6d0L, "go_wndtext FUN_0057a6d0");

        // ---- Group 5: shop ----
        dumpFunc(0x005c5800L, "go_wndshop2 asserted FUN_005c5800");
        dumpFunc(0x005c5ab0L, "shop register FUN_005c5ab0");
        dumpFunc(0x005c5fc0L, "shop draw FUN_005c5fc0");
        dumpFunc(0x005c62d0L, "shop draw FUN_005c62d0");

        // ---- Group 6: GoWnd family vtable shape ----
        dumpFunc(0x005755c0L, "GoWndDemo ctor FUN_005755c0");

        // ---- render-callback registrar (referenced by book tick) ----
        dumpFunc(0x005df8f0L, "render-callback register FUN_005df8f0");

        // ---- xrefs to key globals ----
        xrefs(0x007651c0L, "D3D device global DAT_007651c0");
        xrefs(0x00732c5cL, "book page id DAT_00732c5c");
        xrefs(0x007945c0L, "csprite text font DAT_007945c0");

        close();
    }
}
