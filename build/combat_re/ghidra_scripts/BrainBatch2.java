//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.decompiler.*;

// Deep-dive batch 2: undocumented mid/upper-tower brains M460..M980 + idx93
public class BrainBatch2 extends Re2Lib {

    // {brain va, label}
    Object[][] B = {
        {0x004c7020L, "M460 FUN_004c7020"},
        {0x0048c300L, "M470 FUN_0048c300"},
        {0x00481940L, "M490 FUN_00481940"},
        {0x00485050L, "M500 FUN_00485050"},
        {0x00488a90L, "M510 FUN_00488a90"},
        {0x0048b460L, "M520 FUN_0048b460"},
        {0x004965d0L, "M530 FUN_004965d0"},
        {0x00498830L, "M540 FUN_00498830"},
        {0x0049c130L, "M550 FUN_0049c130"},
        {0x004a0260L, "M560 FUN_004a0260"},
        {0x004f3730L, "M570 FUN_004f3730"},
        {0x0049db20L, "M580 FUN_0049db20"},
        {0x004a7030L, "M590 FUN_004a7030"},
        {0x004a8ea0L, "M600 FUN_004a8ea0"},
        {0x004ac940L, "M610 FUN_004ac940 (3rd 477f00 caster)"},
        {0x004ad980L, "M620 FUN_004ad980"},
        {0x004b0d60L, "M630 FUN_004b0d60"},
        {0x004b74c0L, "M640 FUN_004b74c0"},
        {0x004b4890L, "M650 FUN_004b4890"},
        {0x004af610L, "M660 FUN_004af610"},
        {0x004f1050L, "M670 FUN_004f1050"},
        {0x004c4100L, "M680 FUN_004c4100"},
        {0x004c5d20L, "M690 FUN_004c5d20"},
        {0x004bbf90L, "M700 FUN_004bbf90"},
        {0x004c3300L, "M710 FUN_004c3300"},
        {0x004bd150L, "M720 FUN_004bd150"},
        {0x004c5140L, "M740 FUN_004c5140"},
        {0x004bae90L, "M750 FUN_004bae90"},
        {0x00499f90L, "M760 FUN_00499f90"},
        {0x0049b060L, "M770 FUN_0049b060"},
        {0x004aa220L, "M780 FUN_004aa220"},
        {0x004d7510L, "M810 FUN_004d7510 (+spawn)"},
        {0x004df8c0L, "M820 FUN_004df8c0"},
        {0x004e8050L, "M830 FUN_004e8050"},
        {0x004dce30L, "M840 FUN_004dce30"},
        {0x004cc3d0L, "M850 FUN_004cc3d0"},
        {0x004e3070L, "M860 FUN_004e3070"},
        {0x004f6520L, "M890 FUN_004f6520"},
        {0x004ee000L, "M930 FUN_004ee000"},
        {0x004eec20L, "M940 FUN_004eec20"},
        {0x004ef840L, "M950 FUN_004ef840"},
        {0x004f0470L, "M960 FUN_004f0470"},
        {0x004ca1a0L, "M970 FUN_004ca1a0"},
        {0x004d4370L, "M980 FUN_004d4370"},
        {0x004f7980L, "idx93 FUN_004f7980"},
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
