//@category YsOrigin
// The player's attack/action selector.  FUN_0046ea00 writes actor+0x994 = 0x111 / 0x10e
// and calls SetSection with a COMPUTED id (PUSH EAX at 0x46efaf / 0x46f294) -- that is
// the combo chain.  Dump it plus the small helpers it leans on.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ComboScan6 extends Re2Lib {

    void dumpWords(long base, int n, String tag) throws Exception {
        out.println("\n===== U16 TABLE " + tag + " @0x" + Long.toHexString(base) + " =====");
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder("  ");
        for (int i = 0; i < n; i++) {
            int v = m.getShort(toAddr(base + i * 2)) & 0xffff;
            sb.append(String.format("[%d]=0x%x ", i, v));
        }
        out.println(sb);
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpWords(0x0069bd0cL, 8, "locomotion state -> section");
        dumpFunc(0x0046ea00L, "player attack/action selector FUN_0046ea00");
        dumpFunc(0x0040b230L, "get current section id FUN_0040b230");
        dumpFunc(0x00446550L, "ASO SetSection FUN_00446550");
        out.println("\n===== CALLERS OF FUN_0046ea00 =====");
        for (Address c : callersOf(0x0046ea00L)) out.println("  " + c + "   [" + fnName(c) + "]");
        close();
    }
}
