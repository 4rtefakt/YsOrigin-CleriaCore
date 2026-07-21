//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;

public class HandlerDump extends Re2Lib {

    // AI index -> handler address (from ai_dispatch_table.txt)
    long[] H = {
        0x0046fba0L,0x0045f250L,0x0045e870L,0x0047b7f0L,0x0048a370L,0x00484d60L,0x0047c2e0L,0x0049fef0L,
        0x0047cce0L,0x004baba0L,0x0048b170L,0x0047d8a0L,0x00490240L,0x0047f3b0L,0x004887a0L,0x004ac470L,
        0x004c2fb0L,0x00490d70L,0x0048f3e0L,0x004f3440L,0x004a1e30L,0x004a5300L,0x004a31c0L,0x00491910L,
        0x00492f50L,0x004ea450L,0x004ebf70L,0x00494140L,0x004a6d40L,0x004d3f30L,0x004950f0L,0x004962e0L,
        0x00480c60L,0x004af320L,0x004f5310L,0x004bfdd0L,0x004b3970L,0x004b45a0L,0x004c0ee0L,0x004c9880L,
        0x0048de00L,0x00481650L,0x00482f10L,0x004862c0L,0x00489680L,0x0048c010L,0x004978a0L,0x00499c90L,
        0x0049d830L,0x004a12a0L,0x004f4690L,0x0049f080L,0x004a87f0L,0x004a9ec0L,0x004ad690L,0x004ae5a0L,
        0x004b24f0L,0x004b8e30L,0x004b5360L,0x004afb80L,0x004f24c0L,0x004c4df0L,0x004c6cd0L,0x004bce40L,
        0x004c3d50L,0x004be0a0L,0x004ec3b0L,0x004c59d0L,0x004bbc30L,0x0049ad70L,0x0049be40L,0x004ab280L,
        0x004f5710L,0x004f5c00L,0x004da070L,0x004e2bd0L,0x004e9870L,0x004df410L,0x004d1c90L,0x004e7af0L,
        0x0053af10L,0x004f6130L,0x004f7690L,0x004731d0L,0x004ed0d0L,0x004edd10L,0x004ee930L,0x004ef550L,
        0x004f0180L,0x004f0d60L,0x004cc070L,0x004d6ea0L,0x004f8940L,0x004f8540L
    };

    void dumpC(long va, String tag) {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) {
            try { f = createFunction(a, null); } catch (Exception e) {}
            if (f == null) f = fnAt(a);
        }
        if (f == null) { out.println("\n### NO FUNC @0x" + Long.toHexString(va) + " (" + tag + ")"); return; }
        out.println("\n\n======== " + tag + " : " + f.getName() + " @ " + f.getEntryPoint() + " ========");
        DecompileResults r = decomp.decompileFunction(f, 200, mon);
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        else out.println("(decomp fail)");
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        for (int i = 0; i < H.length; i++) dumpC(H[i], "AIidx " + i + " handler");
        close();
    }
}
