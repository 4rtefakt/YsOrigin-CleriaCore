//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.data.*;

public class MenuVerify3 extends Re2Lib {

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // ---------- ITEM 5: Difficulty / SP-Bonus popup builders ----------
        out.println("################## ITEM 5: difficulty/SP-Bonus popup builders ##################");
        dumpFunc(0x005c4cc0L, "popup builder FUN_005c4cc0");
        dumpFunc(0x005c7780L, "popup builder FUN_005c7780");
        dumpFunc(0x005c9d20L, "popup builder FUN_005c9d20");

        // ---------- ITEM 1: chrome DDS strings + loader ----------
        out.println("\n\n################## ITEM 1: go_wnd chrome accessor + dds string scan ##################");
        dumpFunc(0x00587010L, "wnd-tex accessor FUN_00587010 (reads [idx*4+0x749d48])");

        // scan defined strings for data\menu\*.dds and print each + first code xref's function
        out.println("\n===== data\\menu\\*.dds strings and their referencing functions =====");
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        int shown = 0;
        while (di.hasNext() && shown < 400) {
            Data d = di.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            String sl = s.toLowerCase();
            if (!sl.contains(".dds")) continue;
            // focus on menu chrome-ish names; skip the huge Topic/bestiary sets
            if (!(sl.contains("menu\\") || sl.contains("dialog") || sl.contains("window")
                  || sl.contains("cursor") || sl.contains("waku") || sl.contains("wnd")
                  || sl.contains("frame") || sl.contains("msgbox") || sl.contains("msg")
                  || sl.contains("select") || sl.contains("cur0") || sl.contains("back0"))) continue;
            Reference[] rs = getReferencesTo(d.getAddress());
            StringBuilder sb = new StringBuilder();
            sb.append("  @").append(d.getAddress()).append("  \"").append(s).append("\"");
            for (Reference r : rs) {
                sb.append("  <- ").append(r.getFromAddress()).append(" [").append(fnName(r.getFromAddress())).append("]");
            }
            if (rs.length == 0) sb.append("  (no xref)");
            out.println(sb.toString());
            shown++;
        }
        out.println("(shown " + shown + ")");

        close();
    }
}
