//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The parry-branch applier looped over DAT_00767f14 — prime suspect for the shared hazard scan
        dumpFunc(0x004faf20L, "FUN_004faf20 (parry-branch applier / hazard scan?)");
        xrefs(0x00767f14L, "DAT_00767f14 (scan count global)");
        // Node registration into the collision system
        dumpFunc(0x004f9d70L, "FUN_004f9d70 (register collision node: class,uid)");
        xrefs(0x004f9d70L, "callers of FUN_004f9d70 (who registers nodes)");
        // The shared overlap test
        dumpFunc(0x004f9e90L, "FUN_004f9e90 (sphere/capsule overlap)");
        xrefs(0x004f9e90L, "callers of FUN_004f9e90");
        close();
    }
}
