//@category YsOrigin
import ghidra.program.model.address.*;
public class CharIdxDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The char-select commit that WRITES DAT_0076bb74 = literal 1/2/3/0 (ARENA.md's "undumped" 4-bar select)
        dumpFunc(0x005c3790L, "char-select commit FUN_005c3790 (writes bb74 = 1/2/3/0)");
        close();
    }
}
