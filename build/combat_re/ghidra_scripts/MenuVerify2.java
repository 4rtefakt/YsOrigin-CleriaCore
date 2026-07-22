//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;

public class MenuVerify2 extends Re2Lib {

    void dumpFloats(long va, int count, String tag) throws Exception {
        out.println("\n===== FLOATS @0x" + Long.toHexString(va) + " (" + tag + ") =====");
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < count; i++) {
            Address a = toAddr(va + i * 4L);
            try {
                int bits = m.getInt(a);
                out.println("  +0x" + Integer.toHexString(i * 4) + " @" + a
                    + "  int=0x" + Integer.toHexString(bits) + "  f=" + Float.intBitsToFloat(bits));
            } catch (Exception e) {
                out.println("  +0x" + Integer.toHexString(i * 4) + " @" + a + "  (unreadable)");
            }
        }
    }

    void listCallers(long va, String tag) {
        out.println("\n===== CALLERS OF " + tag + " @0x" + Long.toHexString(va) + " =====");
        for (Reference r : getReferencesTo(toAddr(va))) {
            Instruction i = currentProgram.getListing().getInstructionAt(r.getFromAddress());
            out.println("  " + r.getFromAddress() + "  " + (i != null ? i.toString() : "(data)")
                + "    [" + fnName(r.getFromAddress()) + "]");
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ---------- ITEM 3: status plate slide-rest + size ----------
        out.println("################## ITEM 3: FUN_005b05c0 status-plate slide ##################");
        dumpFunc(0x005b05c0L, "status-plate slide setter FUN_005b05c0");

        // ---------- ITEM 1: chrome handle usage + go_wnd neighbors ----------
        out.println("\n\n################## ITEM 1: chrome handle usage ##################");
        dumpFunc(0x00578f80L, "go_wnddialog draw FUN_00578f80 (reads d60/d68)");
        dumpFunc(0x0057a410L, "go_wnd fn FUN_0057a410 (reads d5c)");
        dumpFunc(0x0057a6d0L, "go_wnd fn FUN_0057a6d0 (reads d5c)");
        // who references the chrome-init strings? scan a wider handle window for any DATA/base ref
        out.println("\n-- widen handle-block scan for a pointer/base writer --");
        for (long a = 0x00749d58L; a <= 0x00749d78L; a += 4) {
            Address ad = toAddr(a);
            out.println("  refs to 0x" + Long.toHexString(a) + ":");
            for (Reference r : getReferencesTo(ad)) {
                Instruction i = currentProgram.getListing().getInstructionAt(r.getFromAddress());
                out.println("      " + r.getFromAddress() + "  " + r.getReferenceType() + "  "
                    + (i != null ? i.toString() : "(data)") + "    [" + fnName(r.getFromAddress()) + "]");
            }
        }

        // ---------- ITEM 4: Option plaque active-position (navigator) ----------
        out.println("\n\n################## ITEM 4: navigator + tab table + plaque[7] ##################");
        dumpFloats(0x00703508L, 20, "tab table 0x703508 (5 entries stride 0x10)");
        dumpFloats(0x00703570L, 8, "direct-entry plaque tex/pos DAT_00703570..");
        dumpFloats(0x00733328L, 12, "plaque[7] Option-alt csprite head (0x732e64+7*0xAC)");
        dumpFunc(0x005a68f0L, "tab navigator FUN_005a68f0 (lays active plaque)");

        // ---------- ITEM 5: all popup builders (Difficulty / SP-Bonus origins) ----------
        out.println("\n\n################## ITEM 5: callers of popup helpers ##################");
        listCallers(0x005784b0L, "popup caption/origin setter FUN_005784b0");

        close();
    }
}
