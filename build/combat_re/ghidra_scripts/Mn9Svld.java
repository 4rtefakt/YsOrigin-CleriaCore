//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Mn9Svld extends Re2Lib {
    void fdump(long va) throws Exception {
        Address a = toAddr(va);
        try {
            int bits = currentProgram.getMemory().getInt(a);
            float f = Float.intBitsToFloat(bits);
            out.printf("  0x%08x  raw=0x%08x  float=%.5f  int=%d%n", va, bits, f, bits);
        } catch (Exception e) { out.printf("  0x%08x  <unreadable>%n", va); }
    }
    void sdump(long va, int n) throws Exception {
        Address a = toAddr(va);
        StringBuilder sb = new StringBuilder();
        StringBuilder wb = new StringBuilder();
        for (int i=0;i<n;i++){
            int b = currentProgram.getMemory().getByte(a.add(i)) & 0xff;
            sb.append(String.format("%02x ", b));
            wb.append(b>=32&&b<127?(char)b:'.');
        }
        out.printf("  0x%08x bytes: %s | %s%n", va, sb.toString(), wb.toString());
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("==== MN9 svld deltas (float rdata) ====");
        out.println("-- Y deltas (added to plate[1]) --");
        long[] yd = {0x006bd9a8L,0x006bd98cL,0x006bd8e8L,0x006bda24L,0x006bda08L,0x006bd864L,0x006bd96cL,0x006bda78L};
        for (long v: yd) fdump(v);
        out.println("-- X deltas (added to plate[0]) --");
        long[] xd2 = {0x006bd9c4L,0x006bdb58L,0x006bdb64L,0x006bdb68L,0x006bdc00L,0x006bdc28L,0x006bdc9cL};
        for (long v: xd2) fdump(v);
        out.println("-- color / misc --");
        long[] cd = {0x006becd0L,0x006bd034L,0x006bdc04L,0x006bdbd0L,0x006bdba0L,0x006bd984L,0x006bd8c4L,0x006bd720L,0x006bdc9cL};
        for (long v: cd) fdump(v);
        out.println("-- char-icon csprite fields (runtime, may be 0) --");
        long[] ci = {0x0073c84cL,0x0073c854L,0x0073c858L};
        for (long v: ci) fdump(v);
        out.println("-- shared field format string 0x6a38dc, ext bytes 0x6e6962 --");
        sdump(0x006a38dcL, 20);
        sdump(0x006e6962L, 8);
        out.println("-- level/loc source labels: level=0x75afcc loc=0x75b014 (rec-relative) --");

        out.println("\n==== who WRITES the slot-header record (find real scanner) ====");
        long[] recFields = {0x0075b058L /*occupied +0x70*/, 0x0075afe8L /*difficulty*/, 0x0075afccL /*level base*/, 0x0075b05cL /*name*/, 0x0075affcL /*playtime*/, 0x0075b014L /*loc*/};
        for (long v: recFields) xrefs(v, "slot-record field 0x"+Long.toHexString(v));
        close();
    }
}
