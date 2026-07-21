//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Survey2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // representative big string-less functions in the renderer/model zone
        long[] fns = {
            0x005e2670L, 0x005e44c0L, 0x005e6960L, 0x005e8cb0L, 0x005eb5b0L,
            0x005f7e20L, 0x005f92b0L, 0x005fcc90L, 0x005fd750L, 0x005fe3c0L,
            0x00600f60L, 0x00601820L, 0x00602370L, 0x00602040L
        };
        for (long f : fns) dumpFunc(f, "big-unknown FUN_" + Long.toHexString(f));

        // Movie/DirectShow question: xref COM + DirectShow-ish symbols anywhere in the exe
        String[] syms = {"CoCreateInstance","CoInitialize","CoInitializeEx","CoUninitialize"};
        SymbolTable st = currentProgram.getSymbolTable();
        for (String s : syms) {
            out.println("\n##### symbol lookup: " + s);
            for (Symbol sym : st.getSymbols(s)) {
                out.println("  " + sym.getName() + " @ " + sym.getAddress() + " type=" + sym.getSymbolType());
                for (Reference r : getReferencesTo(sym.getAddress())) {
                    out.println("     xref " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                }
            }
        }
        close();
    }
}
