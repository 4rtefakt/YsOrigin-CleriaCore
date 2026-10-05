// ExportAll: one-shot full export of yso_win.exe so RE claims can be verified by grep,
// without taking the Ghidra project's single-writer lock.
//   EXPORT_DIR/funcs/<entry>.c   decomp C + callers + callees of every non-thunk function
//   EXPORT_DIR/index.tsv         entry, name, size, #callers, #callees
//   EXPORT_DIR/globals.tsv       data address -> each referencing function + ref type (R/W)
//   EXPORT_DIR/strings.tsv       defined strings -> referencing functions
//@category YsOrigin
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class ExportAll extends Re2Lib {
    public void run() throws Exception {
        File dir = new File(System.getenv("EXPORT_DIR"));
        File fdir = new File(dir, "funcs");
        fdir.mkdirs();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing L = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();

        PrintWriter idx = new PrintWriter(new FileWriter(new File(dir, "index.tsv")));
        PrintWriter glob = new PrintWriter(new FileWriter(new File(dir, "globals.tsv")));
        idx.println("entry\tname\tbytes\tcallers\tcallees");
        glob.println("data\tfunc\tfrom\ttype");
        int n = 0;
        for (Function f : fm.getFunctions(true)) {
            if (f.isThunk() || f.isExternal()) continue;
            String ep = f.getEntryPoint().toString();
            Set<String> callers = new TreeSet<>();
            for (Reference r : getReferencesTo(f.getEntryPoint())) {
                Function c = fm.getFunctionContaining(r.getFromAddress());
                callers.add(c != null ? c.getEntryPoint().toString() : "?" + r.getFromAddress());
            }
            Set<String> callees = new TreeSet<>();
            for (Function c : f.getCalledFunctions(mon)) callees.add(c.getEntryPoint() + " " + c.getName());

            // globals touched by this function
            InstructionIterator it = L.getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                for (Reference r : ins.getReferencesFrom()) {
                    if (!r.isMemoryReference() || r.getReferenceType().isFlow()) continue;
                    Address to = r.getToAddress();
                    if (fm.getFunctionAt(to) != null) continue;
                    String t = r.getReferenceType().isWrite() ? "W" : r.getReferenceType().isRead() ? "R" : "A";
                    glob.println(to + "\t" + ep + "\t" + ins.getAddress() + "\t" + t);
                }
            }

            PrintWriter p = new PrintWriter(new FileWriter(new File(fdir, ep + ".c")));
            p.println("// " + f.getName() + " @ " + ep + "  bytes=" + f.getBody().getNumAddresses()
                + "  params=" + f.getParameterCount() + "  conv=" + f.getCallingConventionName());
            p.println("// callers: " + String.join(" ", callers));
            p.println("// callees:");
            for (String c : callees) p.println("//   " + c);
            DecompileResults r = decomp.decompileFunction(f, 120, mon);
            if (r != null && r.decompileCompleted()) p.println(r.getDecompiledFunction().getC());
            else p.println("// (decomp fail)");
            p.close();
            idx.println(ep + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t"
                + callers.size() + "\t" + callees.size());
            if (++n % 250 == 0) println("exported " + n);
        }
        idx.close();
        glob.close();

        PrintWriter str = new PrintWriter(new FileWriter(new File(dir, "strings.tsv")));
        str.println("addr\tfuncs\tvalue");
        DataIterator di = L.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            if (!d.hasStringValue()) continue;
            Set<String> fs = new TreeSet<>();
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function c = fm.getFunctionContaining(r.getFromAddress());
                fs.add(c != null ? c.getEntryPoint().toString() : "?" + r.getFromAddress());
            }
            String v = String.valueOf(d.getValue()).replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r");
            str.println(d.getAddress() + "\t" + String.join(",", fs) + "\t" + v);
        }
        str.close();
        decomp.dispose();
        println("ExportAll done: " + n + " functions");
    }
}
