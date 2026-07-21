//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;

// Deep-dive batch 1: undocumented early/mid-tower brains M260..M450
public class BrainBatch1 extends Re2Lib {

    // {brain va, label}
    Object[][] B = {
        {0x004f29e0L, "M260 idx19 FUN_004f29e0"},
        {0x004a1940L, "M270 idx20 FUN_004a1940"},
        {0x004a34b0L, "M280 idx21 FUN_004a34b0"},
        {0x004a2120L, "M290 idx22 FUN_004a2120"},
        {0x00491060L, "M300 idx23 FUN_00491060"},
        {0x00491c00L, "M310 idx24 FUN_00491c00"},
        {0x004e9d00L, "M320 idx25 FUN_004e9d00"},
        {0x004ea740L, "M330 idx26 FUN_004ea740"},
        {0x00493240L, "M340 idx27 FUN_00493240"},
        {0x004a56c0L, "M350 idx28 FUN_004a56c0"},
        {0x004d2620L, "M360 idx29 FUN_004d2620"},
        {0x00494430L, "M370 idx30 FUN_00494430"},
        {0x004953e0L, "M380 idx31 FUN_004953e0"},
        {0x0047f6a0L, "M390 idx32 FUN_0047f6a0"},
        {0x004ae8c0L, "M400 idx33 FUN_004ae8c0"},
        {0x004f4bb0L, "M410 idx34 FUN_004f4bb0"},
        {0x004be3f0L, "M420 idx35 FUN_004be3f0"},
        {0x004b27e0L, "M430 idx36 FUN_004b27e0"},
        {0x004b3e70L, "M440 idx37 FUN_004b3e70"},
        {0x004c0140L, "M450 idx38 FUN_004c0140"},
    };

    void dumpC(long va, String label) {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) { try { f = createFunction(a, null); } catch (Exception e) {} if (f==null) f=fnAt(a); }
        if (f == null) { out.println("\n### NO FUNC @0x" + Long.toHexString(va) + " (" + label + ")"); return; }
        out.println("\n\n======== " + label + " : " + f.getName() + " @ " + f.getEntryPoint() + " ========");
        DecompileResults r = decomp.decompileFunction(f, 240, mon);
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        else out.println("(decomp fail)");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        for (Object[] b : B) dumpC((Long)b[0], (String)b[1]);
        close();
    }
}
