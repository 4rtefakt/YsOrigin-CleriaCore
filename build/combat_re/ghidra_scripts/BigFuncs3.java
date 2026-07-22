//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class BigFuncs3 extends Re2Lib {
    void dumpPtrs(long from, long to) {
        out.println("\n---- data pointers " + Long.toHexString(from) + ".." + Long.toHexString(to) + " ----");
        for (long a = from; a < to; a += 4) {
            Address ad = toAddr(a);
            Data d = currentProgram.getListing().getDataAt(ad);
            long val = 0;
            try {
                byte[] b = new byte[4];
                currentProgram.getMemory().getBytes(ad, b);
                val = ((b[0]&0xffL)) | ((b[1]&0xffL)<<8) | ((b[2]&0xffL)<<16) | ((b[3]&0xffL)<<24);
            } catch (Exception e) {}
            Function tf = currentProgram.getFunctionManager().getFunctionAt(toAddr(val));
            out.println("  " + ad + " -> 0x" + Long.toHexString(val) + (tf!=null?("  ("+tf.getName()+")"):""));
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpPtrs(0x0069b9e0L, 0x0069ba60L);
        // xref candidate vtable bases to find the constructor
        for (long base : new long[]{0x0069b9f8L,0x0069b9fcL,0x0069ba00L,0x0069ba04L,0x0069ba08L,0x0069ba10L,0x0069ba18L,0x0069ba20L,0x0069ba24L,0x0069ba28L}) {
            out.println("\n== xref candidate base 0x"+Long.toHexString(base)+" ==");
            for (Reference r : getReferencesTo(toAddr(base))) {
                out.println("  " + r.getFromAddress() + "  " + r.getReferenceType() + "  ["+fnName(r.getFromAddress())+"]");
            }
        }
        close();
    }
}
