//@category YsOrigin
// Round 5: last menu gaps -- difficulty-word global/drawer, save slot-header disk-scanner.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class NightDump5 extends Re2Lib {
    void findStr(String needle, String tag) {
        out.println("\n===== STRINGS containing '" + needle + "' (" + tag + ") =====");
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        int n = 0;
        while (it.hasNext() && n < 30) {
            Data d = it.next();
            Object v = d.getValue();
            if (v instanceof String && ((String) v).contains(needle)) {
                out.println("  @" + d.getAddress() + "  \"" + v + "\"");
                for (Reference r : getReferencesTo(d.getAddress()))
                    out.println("      xref " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                n++;
            }
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // difficulty word: find the strings + who reads/draws them (the pause bottom-right word)
        findStr("Nightmare", "difficulty words");
        findStr("Very Easy", "difficulty words 2");
        // save slot-header disk-scanner: who CALLS the load-read FUN_0042f5e0 (scans all 8 slots)
        xrefs(0x0042f5e0L, "load-read FUN_0042f5e0 callers (slot scanner)");
        // and the light header-only reader that fills 0x0075afe8
        xrefs(0x005b6220L, "svld slot build FUN_005b6220 callers");
        close();
    }
}
