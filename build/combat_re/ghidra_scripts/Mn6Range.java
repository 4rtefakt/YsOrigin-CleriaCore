//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
public class Mn6Range extends Re2Lib {
    void range(long start, long end, String tag) {
        out.println("\n===== RANGE " + tag + " 0x" + Long.toHexString(start) + "..0x" + Long.toHexString(end) + " =====");
        Address a = toAddr(start), e = toAddr(end);
        Listing L = currentProgram.getListing();
        Instruction i = L.getInstructionAt(a);
        if (i == null) i = L.getInstructionAfter(a);
        while (i != null && i.getAddress().compareTo(e) <= 0) {
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Data d = L.getDataAt(r.getToAddress());
                if (d != null && d.getValue() != null) sb.append("   ; -> " + d.getValue());
            }
            out.println(sb);
            i = i.getNext();
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // SetXXX (0xaf) inline setter + SetRavalCostToFlag (0xb0) inline cost mapper
        range(0x00568d40L, 0x005692b0L, "event-VM 0xaf SetXXX / 0xb0 SetRavalCostToFlag inline");
        close();
    }
}
