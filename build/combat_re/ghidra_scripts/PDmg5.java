//@category YsOrigin
import ghidra.program.model.address.*;
public class PDmg5 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00504b00L, "callers of FUN_00504b00 (instant resolver)");
        xrefs(0x00505640L, "callers of FUN_00505640 (persistent-box resolver)");
        xrefs(0x004fd350L, "callers of FUN_004fd350 (enemy->player applier)");
        xrefs(0x004fbb40L, "callers of FUN_004fbb40 (player->enemy applier)");
        xrefs(0x004fe9e0L, "callers of FUN_004fe9e0 (single-target vs player)");
        // FUN_00505810 creates a collision node (calls FUN_004f9d70) — a hazard/hitvol object?
        dumpFunc(0x00505810L, "FUN_00505810 (node/hazard creator near resolvers)");
        xrefs(0x00505810L, "callers of FUN_00505810");
        close();
    }
}
