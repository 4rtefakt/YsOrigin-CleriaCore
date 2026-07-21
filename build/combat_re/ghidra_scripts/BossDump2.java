//@category YsOrigin
import ghidra.program.model.address.*;

public class BossDump2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // boss02
        dumpFunc(0x00518ae0L, "boss02 BIG FUN_00518ae0 (8837, b_01/b_02 SFX)");
        dumpFunc(0x00516740L, "boss02 FUN_00516740 (6104)");
        dumpFunc(0x00515250L, "boss02 FUN_00515250 (5352)");
        // boss03
        dumpFunc(0x005215e0L, "boss03 FUN_005215e0 (4600, b_03 SFX)");
        dumpFunc(0x00526510L, "boss03 FUN_00526510 (3204)");
        dumpFunc(0x005227e0L, "boss03 FUN_005227e0 (3124)");
        dumpFunc(0x00525940L, "boss03 FUN_00525940 (3015)");
        // boss04
        dumpFunc(0x005295a0L, "boss04 FUN_005295a0 (3313, b_04 SFX)");
        dumpFunc(0x0052c8f0L, "boss04 FUN_0052c8f0 (2247)");
        // boss05
        dumpFunc(0x0052e2c0L, "boss05 GIANT FUN_0052e2c0 (27071)");
        dumpFunc(0x00538820L, "boss05 FUN_00538820 (7472)");
        dumpFunc(0x00537160L, "boss05 FUN_00537160 (5766)");
        // boss06
        dumpFunc(0x0053d700L, "boss06 FUN_0053d700 (4836)");
        dumpFunc(0x005423b0L, "boss06 FUN_005423b0 (2233)");
        dumpFunc(0x00540d40L, "boss06 FUN_00540d40 (2217)");
        dumpFunc(0x0053af10L, "boss06 FUN_0053af10 (2156)");
        // boss07
        dumpFunc(0x0054a890L, "boss07 GIANT FUN_0054a890 (17157, SFX bank)");
        dumpFunc(0x005472e0L, "boss07 FUN_005472e0 (7446)");
        dumpFunc(0x00549020L, "boss07 FUN_00549020 (3719)");
        dumpFunc(0x0054ee30L, "boss07 FUN_0054ee30 (3306)");
        // boss08
        dumpFunc(0x00552ef0L, "boss08 GIANT FUN_00552ef0 (21393, SFX bank)");
        dumpFunc(0x00552730L, "boss08 FUN_00552730 (1273)");
        close();
    }
}
