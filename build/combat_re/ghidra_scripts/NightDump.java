//@category YsOrigin
// Dump the [UNKNOWN]/gap functions flagged in PAUSEBOOK.md / BONUSSHOP.md / ARENA.md so the
// consuming agents can close them. Output path via NIGHT_OUT env.
import ghidra.program.model.address.*;
public class NightDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // ---- pause book shell / navigator / frame ----
        dumpFunc(0x005a68f0L, "pausebook navigator FUN_005a68f0");
        dumpFunc(0x005b0130L, "pausebook frame FUN_005b0130");
        dumpFunc(0x005a5d50L, "pausebook page draw switch FUN_005a5d50");
        dumpFunc(0x005a51a0L, "pausebook update dispatch FUN_005a51a0");
        // ---- Map (drawn in Item case tail) ----
        dumpFunc(0x005abd10L, "map draw FUN_005abd10");
        // ---- Save / Load slots ----
        dumpFunc(0x005b3e10L, "svld slot list draw FUN_005b3e10");
        dumpFunc(0x005b7cc0L, "save update FUN_005b7cc0");
        dumpFunc(0x005b6eb0L, "load update FUN_005b6eb0");
        dumpFunc(0x005b5100L, "svld shared FUN_005b5100");
        dumpFunc(0x005b6220L, "svld slot build FUN_005b6220");
        dumpFunc(0x005b6b60L, "svld slot draw FUN_005b6b60");
        // ---- Bonus Shop ----
        dumpFunc(0x005c9d20L, "bonus shop menu FUN_005c9d20");
        xrefs(0x005c9d20L, "bonus shop menu (find host scene caller)");
        dumpFunc(0x005c7b50L, "bonus shop buy handler @005c7b50");
        // ---- Arena / Time Attack ----
        dumpFunc(0x0057bb20L, "arena char-select body FUN_0057bb20");
        dumpFunc(0x004281c0L, "arena run controller FUN_004281c0");
        dumpFunc(0x00575dd0L, "arena title item FUN_00575dd0");
        dumpFunc(0x00575f50L, "options/arena entry FUN_00575f50");
        close();
    }
}
