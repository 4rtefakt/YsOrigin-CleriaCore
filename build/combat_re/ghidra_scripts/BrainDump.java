//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;

public class BrainDump extends Re2Lib {

    long[] B = {
        // early-tower brains M110..M250
        0x00489970L,0x00483200L,0x0047bae0L,0x0049f370L,0x004b9120L,0x0048a6c0L,
        0x0047cfd0L,0x0048f6d0L,0x0047db90L,0x004865b0L,0x004ab840L,0x00490530L,0x0048e190L,
        // known-good re-confirm brains
        0x0047adf0L,0x004c1260L,0x0047c5d0L,0x00480f50L,
        // shared helper family (classify)
        0x0047aa80L,0x0047a7c0L,0x0047a830L,0x0047a880L,0x0047abf0L
    };

    void dumpC(long va) {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) { try { f = createFunction(a, null); } catch (Exception e) {} if (f==null) f=fnAt(a); }
        if (f == null) { out.println("\n### NO FUNC @0x" + Long.toHexString(va)); return; }
        out.println("\n\n======== brain " + f.getName() + " @ " + f.getEntryPoint() + " ========");
        DecompileResults r = decomp.decompileFunction(f, 200, mon);
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        else out.println("(decomp fail)");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        for (long v : B) dumpC(v);
        close();
    }
}
