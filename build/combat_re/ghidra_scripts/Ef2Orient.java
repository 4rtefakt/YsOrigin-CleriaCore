//@category YsOrigin
// Where does an EF2 emitter quad's ORIENTATION come from? EF2_RENDER.md §A says the
// effect's world matrix at instance+0x20 is built by FUN_00451a40(pos, rot, scale).
// Dump that builder and every caller, so the field feeding `rot` can be identified —
// that is what decides whether a quad faces the camera or lies in the world plane.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Ef2Orient extends Re2Lib {

    void callSites(long target, String tag, int back) throws Exception {
        out.println("\n===== CALL SITES OF " + tag + " @0x" + Long.toHexString(target) + " =====");
        Listing lst = currentProgram.getListing();
        List<Address> sites = new ArrayList<>();
        for (Reference r : getReferencesTo(toAddr(target)))
            if (r.getReferenceType().isCall()) sites.add(r.getFromAddress());
        Collections.sort(sites);
        for (Address s : sites) {
            out.println("\n  -- call at " + s + "   [" + fnName(s) + "]");
            List<String> ctx = new ArrayList<>();
            Instruction i = lst.getInstructionAt(s);
            for (int k = 0; k < back && i != null; k++) {
                i = i.getPrevious();
                if (i == null) break;
                ctx.add(0, "       " + i.getAddress() + "  " + i);
            }
            for (String c : ctx) out.println(c);
        }
        if (sites.isEmpty()) out.println("  (none)");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00451a40L, "effect world-matrix builder FUN_00451a40(pos,rot,scale)");
        callSites(0x00451a40L, "FUN_00451a40", 24);
        close();
    }
}
