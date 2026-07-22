//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
public class TopicData extends Re2Lib {
    void dwords(long va, int n, String tag) throws Exception {
        out.println("==== DWORDS @0x"+Long.toHexString(va)+" ("+tag+") ====");
        Address a = toAddr(va);
        for (int i=0;i<n;i++){
            int v = currentProgram.getMemory().getInt(a.add(i*4L));
            out.println(String.format("[%d] +%02x = 0x%08x (%d)", i, i*4, v, v));
        }
        out.println();
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The collection-record array: who WRITES the seen flag / encounter count?
        xrefs(0x0074ac40L, "topic collection record base +0 (seen/name)");
        xrefs(0x0074ac44L, "topic collection record +4 (encounter/kill count) — find writer");
        // per-category entry-count tables (Character/Monster sets)
        dwords(0x0068d5c0L, 8, "per-category counts (eac0=0 base)");
        // entry->id map tables (stride 0x24)
        dwords(0x0068d5d0L, 24, "Character entry->id map DAT_0068d5d0 stride0x24 field0");
        dwords(0x0068d9c0L, 24, "Monster entry->id map DAT_0068d9c0 stride0x24 field0");
        // datacard string-ptr tables
        xrefs(0x0068d5b0L, "PTR DC_001 table");
        // list row layout: DAT_0068e6b0/e6b4 (x,y stride 8), DAT_0068e7b0 (category button Y)
        dwords(0x0068e6b0L, 16, "list row pos table DAT_0068e6b0 (y,x pairs?)");
        dwords(0x0068e7b0L, 6, "category button Y DAT_0068e7b0");
        close();
    }
}
