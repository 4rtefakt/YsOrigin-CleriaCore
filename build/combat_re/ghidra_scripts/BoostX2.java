//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class BoostX2 extends Re2Lib {
    void rdFloats(long va, int n, String tag) throws Exception {
        out.println("\n---- FLOATS @0x"+Long.toHexString(va)+" ("+tag+") ----");
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        for (int i=0;i<n;i++){ int bits=m.getInt(toAddr(va+i*4)); sb.append(i+":"+Float.intBitsToFloat(bits)+"  "); }
        out.println(sb.toString());
    }
    void rdInts(long va, int n, String tag) throws Exception {
        out.println("\n---- INTS @0x"+Long.toHexString(va)+" ("+tag+") ----");
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        for (int i=0;i<n;i++){ int v=m.getInt(toAddr(va+i*4)); sb.append(i+":0x"+Integer.toHexString(v)+"  "); }
        out.println(sb.toString());
    }
    void rdShorts(long va, int n, String tag) throws Exception {
        out.println("\n---- SHORTS @0x"+Long.toHexString(va)+" ("+tag+") ----");
        Memory m = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        for (int i=0;i<n;i++){ short v=m.getShort(toAddr(va+i*2)); sb.append(i+":0x"+Integer.toHexString(v&0xffff)+"("+(v&0xffff)+")  "); }
        out.println(sb.toString());
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00462c40L, "FUN_00462c40 boost mode tick (drain=duration)");
        dumpFunc(0x00463f40L, "FUN_00463f40 boost ENTER");
        dumpFunc(0x0046cc10L, "FUN_0046cc10 gauge build hook (Yunica/Hugo)");
        dumpFunc(0x004fa550L, "FUN_004fa550 base damage compute");
        dumpFunc(0x004fd350L, "FUN_004fd350 hurt-gain SP");
        dumpFunc(0x004fe1a0L, "FUN_004fe1a0 hurt-gain elemental");
        dumpFunc(0x00443ce0L, "FUN_00443ce0 skill-equip array reader");
        dumpFunc(0x0046cb00L, "FUN_0046cb00 equipped-skill reader");
        dumpFunc(0x0046af60L, "FUN_0046af60 equipped-skill reader B");
        dumpFunc(0x0043fe00L, "FUN_0043fe00 boost HUD/gauge read");
        dumpFunc(0x00464960L, "FUN_00464960 reads boost flag");
        dumpFunc(0x00464fc0L, "FUN_00464fc0 reads boost flag");
        dumpFunc(0x005a8030L, "FUN_005a8030 equip write 0x76a630");
        // xrefs for the damage-multiplier index & Yunica boost flag
        xrefs(0x0076bb5cL, "DAT_0076bb5c dmg-mult index");
        xrefs(0x0076bb70L, "DAT_0076bb70 Yunica boost flag?");
        xrefs(0x0076bc3cL, "DAT_0076bc3c hit counter?");
        xrefs(0x0076bb90L, "DAT_0076bb90");
        xrefs(0x0076bc64L, "DAT_0076bc64");
        xrefs(0x0076a62cL, "0x76a62c equip array base");
        xrefs(0x0076a634L, "0x76a634 equip array [2]");
        // tables
        rdFloats(0x0068b218L, 8, "damage-mult table DAT_0068b218[idx]");
        rdInts(0x0068afd0L, 8, "PTR_DAT_0068afd0 section-table ptrs (skill A)");
        rdInts(0x0068afe0L, 8, "PTR_DAT_0068afe0 section-table ptrs (skill B)");
        rdInts(0x00732664L, 8, "PTR_DAT_00732664 combo-table ptrs");
        rdInts(0x0068a4fcL, 8, "PTR_DAT_0068a4fc alt-table ptrs");
        close();
    }
}
