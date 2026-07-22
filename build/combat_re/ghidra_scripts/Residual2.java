//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class Residual2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ---- TARGET A: who spawns the tag-0x18 damaging projectile? ----
        dumpFunc(0x00591f60L, "FUN_00591f60 = tag-0x18 ctor (MOV EAX,0x18 -> [+0x12])");
        out.println("\n===== CALLERS of FUN_00591f60 (tag-0x18 ctor) =====");
        for (Address a : callersOf(0x00591f60L))
            out.println("  " + a + "   [" + fnName(a) + "]");

        // player-damage appliers: who invokes them (find an incoming bullet scan)
        out.println("\n===== CALLERS of FUN_004fd350 (enemy->player applier) =====");
        for (Address a : callersOf(0x004fd350L))
            out.println("  " + a + "   [" + fnName(a) + "]");
        out.println("\n===== CALLERS of FUN_004faf20 (applier +0xd0 vs +0x60) =====");
        for (Address a : callersOf(0x004faf20L))
            out.println("  " + a + "   [" + fnName(a) + "]");
        out.println("\n===== CALLERS of FUN_004fbb40 (player->enemy applier) =====");
        for (Address a : callersOf(0x004fbb40L))
            out.println("  " + a + "   [" + fnName(a) + "]");

        // the enemy outgoing attack-box scan (how a melee/box hits the player)
        dumpFunc(0x004fed50L, "FUN_004fed50 = outgoing attack-box scan");

        // ---- TARGET B: neighborhood 0x9c0..0x9d4 wide/block writer for +0x9cc ----
        out.println("\n\n===== SCAN: instructions referencing offsets 0x9c0..0x9d4 (find wide store covering +0x9cc) =====");
        String[] needles = {"0x9c0]","0x9c4]","0x9c8]","0x9cc]","0x9d0]","0x9d4]","0x9c0,","0x9c4,","0x9c8,"};
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            for (String n : needles) {
                if (s.contains(n)) { out.println("  " + i.getAddress() + "  " + s + "    [" + fnName(i.getAddress()) + "]"); break; }
            }
        }

        // the two other +0x9cc readers — what is +0x9cc semantically?
        dumpFunc(0x00465170L, "FUN_00465170 = reads +0x9cc (MOVSS)");
        dumpFunc(0x004a7030L, "FUN_004a7030 = reads +0x9cc (COMISS)");

        close();
    }
}
