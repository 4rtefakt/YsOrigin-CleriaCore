//@category YsOrigin
import ghidra.program.model.address.*;
public class SixFam extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // --- the 6 target functions ---
        dumpFunc(0x00457f90L, "FUN_00457f90 (~265B) object/gfx 0x45");
        dumpFunc(0x004f8830L, "FUN_004f8830 (~264B) player/combat 0x4f");
        dumpFunc(0x004f6020L, "FUN_004f6020 (~264B) player/combat 0x4f");
        dumpFunc(0x004f5af0L, "FUN_004f5af0 (~264B) player/combat 0x4f");
        dumpFunc(0x004f5600L, "FUN_004f5600 (~264B) player/combat 0x4f");
        dumpFunc(0x005e7a10L, "FUN_005e7a10 (~262B) render/scenegraph 0x5e");
        // --- known damage appliers (family reference) ---
        dumpFunc(0x004faf20L, "FUN_004faf20 known applier");
        dumpFunc(0x004fbb40L, "FUN_004fbb40 known applier");
        dumpFunc(0x004fd350L, "FUN_004fd350 known central applier");
        dumpFunc(0x004fe1a0L, "FUN_004fe1a0 known elemental applier");
        // --- callers of each target ---
        xrefs(0x00457f90L, "callers FUN_00457f90");
        xrefs(0x004f8830L, "callers FUN_004f8830");
        xrefs(0x004f6020L, "callers FUN_004f6020");
        xrefs(0x004f5af0L, "callers FUN_004f5af0");
        xrefs(0x004f5600L, "callers FUN_004f5600");
        xrefs(0x005e7a10L, "callers FUN_005e7a10");
        close();
    }
}
