//@category YsOrigin
// Raw instruction listing over an address RANGE — for the event-VM opcode stubs,
// which are inline inside the giant dispatch fn and so have no containing
// Function for dumpFunc() to resolve.
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;

public class ApRawDisasm extends Re2Lib {

    void raw(long start, long end, String tag) {
        out.println();
        out.println("########## " + tag + "  @" + Long.toHexString(start)
                    + " .. " + Long.toHexString(end) + " ##########");
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                        .getAddress(start);
        Address stop = currentProgram.getAddressFactory().getDefaultAddressSpace()
                        .getAddress(end);
        Listing lst = currentProgram.getListing();
        Instruction ins = lst.getInstructionAt(a);
        if (ins == null) ins = lst.getInstructionAfter(a);
        while (ins != null && ins.getAddress().compareTo(stop) < 0) {
            StringBuilder sb = new StringBuilder();
            sb.append(ins.getAddress()).append("  ")
              .append(String.format("%-46s", ins.toString()));
            // annotate call/jump targets and memory operands
            for (int i = 0; i < ins.getNumOperands(); i++) {
                Object[] o = ins.getOpObjects(i);
                for (Object x : o) {
                    if (x instanceof Address) {
                        Address t = (Address) x;
                        sb.append(" ; -> ").append(t).append(" ")
                          .append(fnName(t));
                    } else if (x instanceof Scalar) {
                        long v = ((Scalar) x).getUnsignedValue();
                        if (v > 0x400000L && v < 0x800000L)
                            sb.append(" ; data 0x").append(Long.toHexString(v));
                    }
                }
            }
            out.println(sb.toString());
            ins = ins.getNext();
        }
    }

    void dumpBytes(long start, int n, String tag) throws Exception {
        out.println();
        out.println("########## " + tag + " ##########");
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                        .getAddress(start);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            int b = currentProgram.getMemory().getByte(a.add(i)) & 0xff;
            sb.append(String.format("%02x ", b));
            if (i % 16 == 15) { out.println("  +" + Integer.toHexString(i - 15) + ": " + sb); sb.setLength(0); }
        }
        if (sb.length() > 0) out.println("  " + sb);
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpBytes(0x0056e6f0L, 136, "0xaf jump table @0x56e6f0 (34 entries)");
        raw(0x00568e36L, 0x00568f60L, "0xaf cases idx 14..33");
        close();
    }
}
