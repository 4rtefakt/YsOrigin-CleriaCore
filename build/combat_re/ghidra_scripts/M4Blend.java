//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import ghidra.program.model.mem.*;
import java.util.*;

public class M4Blend extends Re2Lib {

    // list all defined strings whose text contains any needle (case-insensitive), with xrefs
    void strings(String[] needles) throws Exception {
        out.println("\n===== STRING SCAN " + Arrays.toString(needles) + " =====");
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            if (d == null) continue;
            DataType dt = d.getDataType();
            if (dt == null) continue;
            String tn = dt.getName().toLowerCase();
            if (!(tn.contains("unicode") || tn.contains("string") || tn.contains("char"))) continue;
            Object v = d.getValue();
            if (v == null) continue;
            String s = v.toString();
            String low = s.toLowerCase();
            boolean hit = false;
            for (String n : needles) if (low.contains(n.toLowerCase())) { hit = true; break; }
            if (!hit) continue;
            out.println("  @" + d.getAddress() + "  \"" + s.replace("\n","\\n") + "\"");
            for (Reference r : getReferencesTo(d.getAddress())) {
                out.println("      xref <- " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
            }
        }
    }

    void findSym(String name) throws Exception {
        out.println("\n===== SYMBOL SEARCH '" + name + "' =====");
        SymbolIterator it = currentProgram.getSymbolTable().getSymbolIterator();
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getName().toLowerCase().contains(name.toLowerCase())) {
                out.println("  " + s.getName() + " @ " + s.getAddress() + "  (" + s.getSymbolType() + ")");
                for (Reference r : getReferencesTo(s.getAddress())) {
                    out.println("      xref <- " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                }
            }
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // 1. The render-state setter and its state global
        dumpFunc(0x005eb5b0L, "renderState setter FUN_005eb5b0");
        xrefs(0x00767760L, "DAT_00767760 renderState-id global (readers = D3D applier)");

        // 2. Brightness / gamma path
        xrefs(0x00765164L, "DAT_00765164 brightness slider value");
        findSym("GammaRamp");
        findSym("SetGamma");

        // 3. Strings: gamma/brightness + not-enough-SP + skill fail messages
        strings(new String[]{"gamma", "bright", " SP", "not enough", "insufficient", "cannot", "can't", "MP"});

        close();
    }
}
