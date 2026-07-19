//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
public class NightDump6 extends Re2Lib {
    void findStr(String needle, String tag) {
        out.println("\n===== STRINGS containing '" + needle + "' (" + tag + ") =====");
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        int n = 0;
        while (it.hasNext() && n < 25) {
            Data d = it.next(); Object v = d.getValue();
            if (v instanceof String && ((String) v).toLowerCase().contains(needle.toLowerCase())) {
                out.println("  @" + d.getAddress() + "  \"" + v + "\"");
                for (Reference r : getReferencesTo(d.getAddress()))
                    out.println("      xref " + r.getFromAddress() + "  [" + fnName(r.getFromAddress()) + "]");
                n++;
            }
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        findStr("enough", "not-enough-SP");
        findStr("SP is", "not-enough-SP2");
        findStr("Sealed", "topic locked");
        findStr("? ? ?", "topic unknown");
        xrefs(0x00701730L, "map region marker table DAT_00701730");
        close();
    }
}
