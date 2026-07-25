//@category YsOrigin
// The Equip/Item grid cells blit SQUARE.dds (menu tex 10), a 512x256 atlas holding four
// tiles. CleriaCore draws the WHOLE sheet into one cell (issue #19: stray outlined
// squares). Dump the drawers to recover the per-tile UV rects.
//   FUN_005a7580 = equip-page grid (cells + cursor)
//   FUN_005a9140 = equip-page draw
//   FUN_005aad90 = item-page grid (cell tile in every cell + cursor + count badge)
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class SquareUv extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005a7580L, "equip grid FUN_005a7580");
        dumpFunc(0x005a9140L, "equip page draw FUN_005a9140");
        close();
    }
}
