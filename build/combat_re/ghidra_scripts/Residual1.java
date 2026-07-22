//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class Residual1 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ---- TARGET A: projectile tag / spawn / update bodies ----
        dumpFunc(0x00593b90L, "FUN_00593b90 = bullet spawner (sets tag +0x12)");
        dumpFunc(0x00594230L, "FUN_00594230 = tag-0xb FireBullet update");
        dumpFunc(0x00592d00L, "FUN_00592d00 = tag-0x18 damaging projectile update");
        dumpFunc(0x00593b90L, "dup guard"); // no-op re-dump safe

        // ---- TARGET B: actor init that sets +0x30c (and maybe +0x9cc) ----
        dumpFunc(0x00459410L, "FUN_00459410 = enemy actor init (+0x30c writer)");

        // ---- SCAN 1: every word-store to [reg+0x12] (find the 0x18 tag writer) ----
        out.println("\n\n===== SCAN: stores to [reg + 0x12] (tag writers) =====");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (i.getMnemonicString().equals("MOV") && s.contains("0x12]") && s.matches(".*\\[[^\\]]*0x12\\],.*")) {
                out.println("  " + i.getAddress() + "  " + s + "    [" + fnName(i.getAddress()) + "]");
            }
        }

        // ---- SCAN 2: every instruction touching +0x9cc (find the writer) ----
        out.println("\n\n===== SCAN: all instructions referencing +0x9cc =====");
        it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (s.contains("0x9cc")) {
                out.println("  " + i.getAddress() + "  " + s + "    [" + fnName(i.getAddress()) + "]");
            }
        }

        // ---- SCAN 3: immediate 0x18 stored to a word field (secondary net for the tag) ----
        out.println("\n\n===== SCAN: instructions with both '0x12]' and ',0x18' or setting AX=0x18 near =====");
        it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if ((s.contains("0x18") && (s.contains("AX") || s.contains("word"))) && s.startsWith("MOV")) {
                // narrow to likely tag loads
                if (s.matches(".*,0x18$") || s.contains("[") ) {
                    // only print if in the projectile code region 0x58f000..0x59a000
                    long a = i.getAddress().getOffset();
                    if (a >= 0x58f000L && a <= 0x59b000L) {
                        out.println("  " + i.getAddress() + "  " + s + "    [" + fnName(i.getAddress()) + "]");
                    }
                }
            }
        }

        close();
    }
}
