//@category YsOrigin
import ghidra.program.model.address.*;
public class RewardsDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004fbb40L, "EXP AWARD SITE FUN_004fbb40 (chain ctr @0x4fc86e, award @0x4fcc0b)");
        dumpFunc(0x004fa2f0L, "level-scale helper FUN_004fa2f0");
        dumpFunc(0x004202e0L, "enemy stat/EXP setup FUN_004202e0 (EXP level<31 pow branch, actor+0xc4)");
        dumpFunc(0x004fa390L, "kill FX/drop #1 FUN_004fa390");
        dumpFunc(0x004fa290L, "kill FX/drop #2 FUN_004fa290");
        dumpFunc(0x004fa4a0L, "pickup/give-EXP FUN_004fa4a0 (uses fa2f0 return)");
        xrefs(0x0076a5fcL, "EXP-rate DAT_0076a5fc (find setters)");
        xrefs(0x0076c1a8L, "chain-bonus counter DAT_0076c1a8 (find writers)");
        xrefs(0x006bd084L, "chain-bonus const DAT_006bd084 (0.01?)");
        xrefs(0x0074622cL, "xorshift RNG state 0x74622c (find users)");
        close();
    }
}
