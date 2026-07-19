//@category YsOrigin
// Round 4: the remaining menu-side [UNKNOWN]/nofunc gaps.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class NightDump4 extends Re2Lib {
    // disassemble N instructions from a raw address (no defined function needed -- for nofunc LAB_ handlers)
    void disasmFrom(long va, int n, String tag) {
        out.println("\n\n===== RANGE DISASM " + tag + " @0x" + Long.toHexString(va) + " (" + n + " instrs) =====");
        Listing L = currentProgram.getListing();
        Instruction i = L.getInstructionAt(toAddr(va));
        int c = 0;
        while (i != null && c < n) {
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Data d = L.getDataAt(r.getToAddress());
                if (d != null && d.getValue() != null) sb.append("   ; -> " + d.getValue());
                Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                if (tf != null) sb.append("   ; call " + tf.getName());
            }
            out.println(sb);
            i = i.getNext(); c++;
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005c5b20L, "shop host FUN_005c5b20 (Feena/bg full)");
        dumpFunc(0x005c6e20L, "shop backdrop FUN_005c6e20 (full)");
        dumpFunc(0x005c3790L, "page-8 4-char writer FUN_005c3790");
        dumpFunc(0x005c2110L, "page-8 builder FUN_005c2110");
        xrefs(0x0075afe8L, "slot-header buffer 0x0075afe8 (find disk-scan loader)");
        disasmFrom(0x005c7b50L, 240, "buy handler LAB_005c7b50 (confirm/deduct/not-enough-SP)");
        close();
    }
}
