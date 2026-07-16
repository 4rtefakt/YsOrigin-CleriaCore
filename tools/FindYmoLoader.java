//Locate and decompile Ys Origin's YMO model loader by finding the "YMO\0" magic
//comparison, plus sibling Falcom-magic loaders (they likely share a dispatcher).
//Ground truth for the four header counts at +0x10/+0x14/+0x18/+0x1C.
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.util.*;

public class FindYmoLoader extends GhidraScript {

    static final Map<Long, String> MAGIC = new LinkedHashMap<Long, String>();
    static {
        MAGIC.put(0x004F4D59L, "YMO\\0");
        MAGIC.put(0x004F4D59L & 0xFFFFFFL, "YMO(3b)");
        MAGIC.put(0x00525358L, "XSR\\0");
        MAGIC.put(0x00424F53L, "SOB\\0");
        MAGIC.put(0x004E4553L, "SEN\\0");
        MAGIC.put(0x00414941L, "AIA\\0");
        MAGIC.put(0x004D4353L, "SCM\\0");
        MAGIC.put(0x00494E4EL, "NNI\\0");
    }

    public void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
        PrintWriter out = new PrintWriter(
            new FileWriter("D:\\ghidra-work\\ymo_loader_out.txt"));

        FunctionManager fm = currentProgram.getFunctionManager();
        // entrypoint -> list of "addr  MAGIC  instruction"
        LinkedHashMap<String, List<String>> hits =
            new LinkedHashMap<String, List<String>>();
        LinkedHashMap<String, Set<String>> tags =
            new LinkedHashMap<String, Set<String>>();

        long n = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            n++;
            for (int i = 0; i < ins.getNumOperands(); i++) {
                for (Object o : ins.getOpObjects(i)) {
                    if (!(o instanceof Scalar)) continue;
                    long v = ((Scalar) o).getUnsignedValue();
                    String tag = MAGIC.get(v);
                    if (tag == null) continue;
                    Function f = fm.getFunctionContaining(ins.getAddress());
                    String ep = (f == null) ? "ORPHAN" : f.getEntryPoint().toString();
                    if (!hits.containsKey(ep)) {
                        hits.put(ep, new ArrayList<String>());
                        tags.put(ep, new TreeSet<String>());
                    }
                    hits.get(ep).add(String.format("    %s  %-8s %s",
                        ins.getAddress(), tag, ins.toString()));
                    tags.get(ep).add(tag);
                }
            }
        }

        out.println("scanned " + n + " instructions");
        out.println("functions referencing a Falcom magic: " + hits.size());
        out.println();
        for (String ep : hits.keySet()) {
            Function f = (ep.equals("ORPHAN")) ? null
                       : fm.getFunctionContaining(toAddr(Long.parseLong(ep, 16)));
            out.println("=== " + (f == null ? "ORPHAN" : f.getName()) + " @ " + ep
                + "  " + tags.get(ep) + "  (" + hits.get(ep).size() + " hits)");
            List<String> rows = hits.get(ep);
            for (int i = 0; i < Math.min(rows.size(), 8); i++) out.println(rows.get(i));
        }

        // Decompile everything that touches a YMO magic.
        out.println("\n########## DECOMPILED YMO CANDIDATES ##########");
        int done = 0;
        for (String ep : hits.keySet()) {
            if (ep.equals("ORPHAN")) continue;
            boolean isYmo = false;
            for (String t : tags.get(ep)) if (t.startsWith("YMO")) isYmo = true;
            if (!isYmo) continue;
            Function f = fm.getFunctionContaining(toAddr(Long.parseLong(ep, 16)));
            if (f == null) continue;
            DecompileResults res = decomp.decompileFunction(f, 180, monitor);
            out.println("\n----- FUNC " + f.getName() + " @ " + ep + " -----");
            if (res != null && res.decompileCompleted()) {
                String c = res.getDecompiledFunction().getC();
                if (c.length() > 16000) c = c.substring(0, 16000) + "\n/* ...truncated... */";
                out.println(c);
                done++;
            } else {
                out.println("  (decompile failed)");
            }
        }
        out.close();
        println("done -> ymo_loader_out.txt (" + hits.size() + " funcs, "
                + done + " YMO decompiled)");
    }
}
