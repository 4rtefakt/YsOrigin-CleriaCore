//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import ghidra.program.model.mem.*;

public class MenuVerify extends Re2Lib {

    // dump `count` little-endian floats starting at va
    void dumpFloats(long va, int count, String tag) throws Exception {
        out.println("\n===== FLOATS @0x" + Long.toHexString(va) + " (" + tag + ") =====");
        Memory m = currentProgram.getMemory();
        for (int i = 0; i < count; i++) {
            Address a = toAddr(va + i * 4L);
            try {
                int bits = m.getInt(a);
                float f = Float.intBitsToFloat(bits);
                out.println("  +0x" + Integer.toHexString(i * 4) + " @" + a
                    + "  int=0x" + Integer.toHexString(bits) + "  f=" + f);
            } catch (Exception e) {
                out.println("  +0x" + Integer.toHexString(i * 4) + " @" + a + "  (unreadable)");
            }
        }
    }

    // only WRITE references to va
    void writersOf(long va, String tag) {
        out.println("\n===== WRITE-XREFS TO " + tag + " @0x" + Long.toHexString(va) + " =====");
        int n = 0;
        for (Reference r : getReferencesTo(toAddr(va))) {
            RefType t = r.getReferenceType();
            boolean w = t.isWrite();
            Instruction i = currentProgram.getListing().getInstructionAt(r.getFromAddress());
            out.println("  " + r.getFromAddress() + "  " + t + (w ? "  <WRITE>" : "") + "  "
                + (i != null ? i.toString() : "(data)") + "    [" + fnName(r.getFromAddress()) + "]");
            n++;
        }
        if (n == 0) out.println("  (no refs at all)");
    }

    // list any references landing anywhere in [va, va+len)
    void rangeXrefs(long va, int len, String tag) {
        out.println("\n===== ANY XREFS IN RANGE [0x" + Long.toHexString(va) + " , 0x"
            + Long.toHexString(va + len) + ") (" + tag + ") =====");
        Memory m = currentProgram.getMemory();
        for (long a = va; a < va + len; a += 4) {
            Address ad = toAddr(a);
            Reference[] rs = getReferencesTo(ad);
            if (rs.length == 0) continue;
            for (Reference r : rs) {
                Instruction i = currentProgram.getListing().getInstructionAt(r.getFromAddress());
                out.println("  @0x" + Long.toHexString(a) + " <- " + r.getFromAddress() + "  "
                    + r.getReferenceType() + "  " + (i != null ? i.toString() : "(data)")
                    + "    [" + fnName(r.getFromAddress()) + "]");
            }
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ---------- ITEM 1: menu chrome texture handles ----------
        out.println("################## ITEM 1: chrome texture handles ##################");
        xrefs(0x00749d60L, "frame 9-slice handle DAT_00749d60");
        xrefs(0x00749d68L, "selection-bar handle DAT_00749d68");
        // scan the surrounding handle block for the writer / struct base
        rangeXrefs(0x00749d00L, 0x100, "chrome-handle block 0x749d00..0x749e00");

        // ---------- ITEM 2: char-select key-art / portrait ----------
        out.println("\n\n################## ITEM 2: char key-art swap + draw ##################");
        dumpFunc(0x005c1e70L, "char key-art swap FUN_005c1e70");
        // FUN_005c2110 is the char-select builder that lays out the portrait sprite
        dumpFunc(0x005c2110L, "char-select build FUN_005c2110 (portrait rect + plate)");

        // ---------- ITEM 3: status-panel plate slide position ----------
        out.println("\n\n################## ITEM 3: status plate slide DAT_0073b978/97c ##################");
        writersOf(0x0073b978L, "status plate X DAT_0073b978");
        writersOf(0x0073b97cL, "status plate Y DAT_0073b97c");
        rangeXrefs(0x0073b960L, 0x40, "status-plate slide block 0x73b960..0x73b9a0");

        // ---------- ITEM 4: Option page-title plaque ----------
        out.println("\n\n################## ITEM 4: Option plaque (frame compositor) ##################");
        // plaque table &DAT_00732e64 stride 0xAC, 9 entries; page 3 = Option => 0x732e64 + 3*0xAC
        long optPlaque = 0x00732e64L + 3 * 0xACL;
        out.println("Option plaque csprite base = 0x" + Long.toHexString(optPlaque));
        dumpFloats(0x00732e64L, 8, "plaque[0] Equip csprite head");
        dumpFloats(optPlaque, 12, "plaque[3] Option csprite head");
        dumpFunc(0x005a73c0L, "frame compositor FUN_005a73c0");

        // ---------- ITEM 5: New-Game popups shared origin ----------
        out.println("\n\n################## ITEM 5: popup origins ##################");
        dumpFunc(0x005c3790L, "char-select handler / Type-Select popup FUN_005c3790");

        close();
    }
}
