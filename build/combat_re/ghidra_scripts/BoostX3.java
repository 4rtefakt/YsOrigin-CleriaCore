//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
public class BoostX3 extends Re2Lib {
    void rdFloats(long va, int n, String tag) throws Exception {
        out.println("\n---- FLOATS @0x"+Long.toHexString(va)+" ("+tag+") ----");
        Memory m = currentProgram.getMemory(); StringBuilder sb=new StringBuilder();
        for (int i=0;i<n;i++){ int b=m.getInt(toAddr(va+i*4)); sb.append(i+":"+Float.intBitsToFloat(b)+"  "); }
        out.println(sb.toString());
    }
    void rdShorts(long va, int n, String tag) throws Exception {
        out.println("\n---- SHORTS @0x"+Long.toHexString(va)+" ("+tag+") ----");
        Memory m = currentProgram.getMemory(); StringBuilder sb=new StringBuilder();
        for (int i=0;i<n;i++){ short v=m.getShort(toAddr(va+i*2)); sb.append(i+":"+(v&0xffff)+"("+String.format("0x%x",v&0xffff)+")  "); }
        out.println(sb.toString());
    }
    void callers(long va, String tag) {
        out.println("\n===== CALLERS OF "+tag+" @0x"+Long.toHexString(va)+" =====");
        for (Address a : callersOf(va)) out.println("  "+a+"   ["+fnName(a)+"]");
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // the damage DOUBLER gate -- find its writer
        xrefs(0x0076bc64L, "DAT_0076bc64 damage-doubler gate");
        xrefs(0x0076bc68L, "DAT_0076bc68 sibling");
        xrefs(0x0076c15cL, "DAT_0076c15c 2nd mode bitfield");
        xrefs(0x0076bc80L, "DAT_0076bc80 buff-config flags");
        // gauge-build hook + boost enter callers
        callers(0x0046cc10L, "FUN_0046cc10 gauge build hook");
        callers(0x00463f40L, "FUN_00463f40 buff enter");
        // equip write path
        dumpFunc(0x005a8030L, "FUN_005a8030 equip-select write");
        dumpFunc(0x0046d590L, "FUN_0046d590 skill->regen-rate index");
        // constants
        rdFloats(0x006bd438L, 1, "boost drain base (fVar10 normal)");
        rdFloats(0x006bd334L, 1, "SMode/alt-drain 0.6");
        rdFloats(0x006bd464L, 1, "param4 dmg mult (Yunica) 1.1");
        rdFloats(0x006bdbc0L, 1, "SP superarmor cost char0");
        rdFloats(0x006bdcf4L, 1, "SP superarmor cost Yunica");
        rdFloats(0x006bd868L, 1, "5.0 base");
        rdFloats(0x006bdf68L, 1, "boost full 30000");
        // per-character boost drain amounts (shorts)
        rdShorts(0x0068b060L, 4, "boost drain amt per char DAT_0068b060");
        rdShorts(0x0068b058L, 4, "boost drain amt (diff2) DAT_0068b058");
        rdShorts(0x0068b17cL, 1, "DAT_0068b17c drain special");
        // skill section sub-tables (shorts) per character - skillA ptrs were 0x68b008/000/ff8/ff0
        rdShorts(0x0068b008L, 4, "skillA char0 sections");
        rdShorts(0x0068b000L, 4, "skillA char1 sections");
        rdShorts(0x0068aff8L, 4, "skillA char2 sections");
        rdShorts(0x0068aff0L, 4, "skillA char3 sections");
        // combo tables per char: 0x68afb8/afa0/acdc/a7f4
        rdShorts(0x0068afb8L, 8, "combo char0");
        rdShorts(0x0068afa0L, 8, "combo char1");
        rdShorts(0x0068acdcL, 8, "combo char2");
        rdShorts(0x0068a7f4L, 8, "combo char3");
        close();
    }
}
