// Re2Lib: shared helper base class for the re2 investigation scripts.
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class Re2Lib extends GhidraScript {
    public PrintWriter out;
    public DecompInterface decomp;
    public ConsoleTaskMonitor mon = new ConsoleTaskMonitor();

    public void open(String path) throws Exception {
        out = new PrintWriter(new FileWriter(path));
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
    }
    public void close() { out.close(); decomp.dispose(); }

    public Function fnAt(Address a) {
        return currentProgram.getFunctionManager().getFunctionContaining(a);
    }
    public String fnName(Address a) {
        Function f = fnAt(a);
        return f != null ? f.getName() + "@" + f.getEntryPoint() : "(nofunc)@" + a;
    }

    // ---- disassembly of one function, with any referenced data value appended
    public void disasm(Function f) {
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction i = it.next();
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Data d = currentProgram.getListing().getDataAt(r.getToAddress());
                if (d != null && d.getValue() != null) sb.append("   ; -> " + d.getValue());
                Function tf = currentProgram.getFunctionManager().getFunctionAt(r.getToAddress());
                if (tf != null) sb.append("   ; call " + tf.getName());
            }
            out.println(sb);
        }
    }

    public void dumpFunc(long va, String tag) {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) { out.println("\n### NO FUNC @0x" + Long.toHexString(va) + " (" + tag + ")"); return; }
        out.println("\n\n================ " + tag + " : " + f.getName() + " @ " + f.getEntryPoint()
            + "  params=" + f.getParameterCount() + " conv=" + f.getCallingConventionName() + " ================");
        DecompileResults r = decomp.decompileFunction(f, 240, mon);
        out.println("---- DECOMP ----");
        if (r != null && r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        else out.println("(decomp fail)");
        out.println("---- RAW DISASM ----");
        disasm(f);
    }

    public List<Address> callersOf(long va) {
        List<Address> res = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (Reference r : getReferencesTo(toAddr(va))) {
            Function cf = fnAt(r.getFromAddress());
            String k = cf != null ? cf.getEntryPoint().toString() : r.getFromAddress().toString();
            if (seen.contains(k)) continue;
            seen.add(k);
            res.add(r.getFromAddress());
        }
        return res;
    }

    public void xrefs(long va, String tag) {
        out.println("\n===== XREFS TO " + tag + " @0x" + Long.toHexString(va) + " =====");
        int n = 0;
        for (Reference r : getReferencesTo(toAddr(va))) {
            Instruction i = currentProgram.getListing().getInstructionAt(r.getFromAddress());
            out.println("  " + r.getFromAddress() + "  " + r.getReferenceType() + "  "
                + (i != null ? i.toString() : "(data)") + "    [" + fnName(r.getFromAddress()) + "]");
            n++;
        }
        if (n == 0) out.println("  (none)");
    }

    public void run() throws Exception {}
}
