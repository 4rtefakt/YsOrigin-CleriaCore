//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import java.util.*;

public class SurveyBand extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        long lo = 0x005e0000L, hi = 0x00620000L;
        AddressSet set = new AddressSet(toAddr(lo), toAddr(hi));
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(set, true);
        // collect
        List<Function> funcs = new ArrayList<>();
        while (it.hasNext()) {
            Function f = it.next();
            long ep = f.getEntryPoint().getOffset();
            if (ep < lo || ep >= hi) continue;
            funcs.add(f);
        }
        out.println("## Band 0x" + Long.toHexString(lo) + "-0x" + Long.toHexString(hi)
            + " : " + funcs.size() + " functions");
        // sort by size desc
        funcs.sort((a,b) -> Long.compare(bodyLen(b), bodyLen(a)));
        out.println("\n### By size (top 60)");
        for (int k = 0; k < funcs.size() && k < 60; k++) {
            Function f = funcs.get(k);
            out.println(String.format("%2d  %s  size=%5d", k, f.getEntryPoint(), bodyLen(f)));
        }

        out.println("\n### Per-function: called externals + string refs (ALL, in addr order)");
        funcs.sort((a,b) -> a.getEntryPoint().compareTo(b.getEntryPoint()));
        for (Function f : funcs) {
            Set<String> ext = new TreeSet<>();
            Set<String> strs = new LinkedHashSet<>();
            Set<String> callsFun = new TreeSet<>();
            InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (ii.hasNext()) {
                Instruction i = ii.next();
                for (Reference r : i.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    Function tf = currentProgram.getFunctionManager().getFunctionAt(to);
                    if (tf != null) {
                        String nm = tf.getName();
                        if (tf.isExternal() || tf.isThunk() || !nm.startsWith("FUN_")) {
                            // resolve thunk target name
                            Function th = tf.isThunk() ? tf.getThunkedFunction(true) : tf;
                            String rn = th != null ? th.getName() : nm;
                            if (rn.startsWith("FUN_")) rn = nm;
                            ext.add(rn);
                        } else {
                            callsFun.add(nm.replace("FUN_",""));
                        }
                    }
                    Data d = currentProgram.getListing().getDataAt(to);
                    if (d != null && d.getValue() != null) {
                        Object v = d.getValue();
                        if (v instanceof String) {
                            String s = ((String)v).trim();
                            if (s.length() >= 3) strs.add(s.replace("\n","\\n"));
                        }
                    }
                }
            }
            StringBuilder sb = new StringBuilder();
            sb.append(f.getEntryPoint()).append(" sz=").append(bodyLen(f));
            if (!ext.isEmpty()) sb.append("  EXT{").append(String.join(",", ext)).append("}");
            if (!strs.isEmpty()) {
                sb.append("  STR{");
                int c=0; for (String s: strs){ if(c++>0) sb.append(" | "); sb.append('"').append(s).append('"'); if(c>=8){sb.append(" ...");break;} }
                sb.append("}");
            }
            out.println(sb.toString());
        }
        close();
    }

    long bodyLen(Function f) {
        return f.getBody().getNumAddresses();
    }
}
