//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class H6Vals extends Re2Lib {
    void f(long va, String tag) {
        try {
            Address a = toAddr(va);
            MemoryBlock b = currentProgram.getMemory().getBlock(a);
            boolean init = b != null && b.isInitialized();
            int i = currentProgram.getMemory().getInt(a);
            out.println(String.format("  0x%08x  int=0x%08x  float=%.6f  block=%s init=%s   %s",
                va, i, Float.intBitsToFloat(i), b==null?"?":b.getName(), init, tag));
        } catch (Exception e) { out.println("  0x"+Long.toHexString(va)+"  ERR "+e); }
    }
    void vec4(long va, String tag) {
        out.println(" VEC4 "+tag+" @0x"+Long.toHexString(va)+":");
        for (int k=0;k<4;k++) f(va+4*k, "  ["+k+"]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("== portrait UV ==");
        f(0x006bd2f8L,"portrait base U (_DAT_006bd2f8)");
        f(0x006bd310L,"portrait base V (_DAT_006bd310)");
        f(0x006bd240L,"U stride 0.234375? (DAT_006bd240)");
        f(0x006bd218L,"V stride 0.203125? (DAT_006bd218)");
        f(0x006bd200L,"portrait V-height 0.1875? (DAT_006bd200)");
        f(0x006bd238L,"_DAT_006bd238 (portrait screen dY?)");
        out.println("== magic icon ==");
        f(0x006bd438L,"elem col wind 0x74 (1.0)");
        f(0x006bd580L,"elem col earth 0x76 (2.0)");
        f(0x006bd370L,"Hugo U (DAT_006bd370)");
        f(0x006bd248L,"magic base V (_DAT_006bd248)");
        f(0x006bd178L,"magic cell size 40/512 (_DAT_006bd178)");
        f(0x006bda34L,"40.0 (DAT_006bda34)");
        f(0x006bd008L,"1/512 (DAT_006bd008)");
        out.println("== screen-rect vectors ==");
        vec4(0x006bee90L,"portrait screen rect (_DAT_006bee90)");
        vec4(0x006bee80L,"portrait screen rect2 (_DAT_006bee80)");
        vec4(0x006bee70L,"magic-icon screen rect (_DAT_006bee70)");
        f(0x006bdb38L,"slot-frame x base (_DAT_006bdb38)");
        f(0x006bdb44L,"item-icon x base (_DAT_006bdb44)");
        out.println("== icon handle table population ==");
        xrefs(0x0074e270L,"icon texture handle array 0x74e270 (fontraster2 loads)");
        f(0x0074a958L,"0x74a958[0]");
        f(0x0074a968L,"0x74a968 (id0 alt icon DAT_0074a968)");
        close();
    }
}
