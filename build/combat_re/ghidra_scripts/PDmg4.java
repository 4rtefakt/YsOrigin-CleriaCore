//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg4 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // GoXx ctor — does the bullet register a collision/hazard node?
        dumpFunc(0x00583a00L, "FUN_00583a00 (GoXx ctor)");
        // Node creator called inside the attack scan
        dumpFunc(0x00434970L, "FUN_00434970 (node creator in scan)");
        // Who calls the outgoing scan — only actors, or bullets too?
        xrefs(0x004fed50L, "callers of FUN_004fed50");
        // Registry buckets: base ptrs / add function
        xrefs(0x00767f0cL, "DAT_00767f0c (bucket free-list base)");
        xrefs(0x00767f34L, "DAT_00767f34 (bucket1 array base)");
        xrefs(0x00767f38L, "DAT_00767f38 (bucket1 count)");
        close();
    }
}
