// Dmg6: (a) dump EVERY ascii string in the go_script*.cpp pool 0x69f000..0x6a0900 (the full
//       script-binding name list), (b) find their users by walking INSTRUCTION OPERANDS for the
//       string VA as a scalar immediate (immediates have no xrefs - trap #2).
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.*;
import java.io.*;
import java.util.*;

public class Dmg6 extends Re2Lib {

    String readCStr(long va, int max) {
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < max; i++) {
                byte b = m.getByte(toAddr(va + i));
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) (b & 0xff));
            }
        } catch (Exception e) { return null; }
        return sb.length() == 0 ? null : sb.toString();
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg6_out.txt");
        Memory m = currentProgram.getMemory();

        // ---- (a) walk the pool, emitting every NUL-terminated ascii run
        out.println("######## STRING POOL 0x69f000..0x6a0900 ########");
        Set<Long> poolStrs = new LinkedHashSet<>();
        long a = 0x69f000L;
        while (a < 0x6a0900L) {
            String s = readCStr(a, 128);
            if (s != null && s.length() >= 3) {
                out.println(String.format("  0x%08x  \"%s\"", a, s));
                poolStrs.add(a);
                a += s.length() + 1;
                while ((a & 3) != 0) a++;   // 4-byte align
            } else a++;
        }

        // ---- (b) who uses them? scan every instruction's scalar operands
        out.println("\n\n######## USERS (instruction operand == string VA) ########");
        Map<Long,List<String>> users = new LinkedHashMap<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction i = it.next();
            for (int op = 0; op < i.getNumOperands(); op++) {
                Object[] objs = i.getOpObjects(op);
                for (Object o : objs) {
                    if (!(o instanceof Scalar)) continue;
                    long v = ((Scalar) o).getUnsignedValue();
                    if (!poolStrs.contains(v)) continue;
                    users.computeIfAbsent(v, k -> new ArrayList<>())
                         .add(i.getAddress() + "  " + i + "   [" + fnName(i.getAddress()) + "]");
                }
            }
        }
        for (Long k : poolStrs) {
            List<String> u = users.get(k);
            out.println(String.format("\n  0x%08x \"%s\"  users=%d", k, readCStr(k, 128), u == null ? 0 : u.size()));
            if (u != null) for (String s : u) out.println("        " + s);
        }
        close();
    }
}
