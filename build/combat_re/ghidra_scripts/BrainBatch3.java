//@category YsOrigin
import ghidra.program.model.address.*;
public class BrainBatch3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x004d7510L, "M810 brain FUN_004d7510");
        dumpFunc(0x004df8c0L, "M820 brain FUN_004df8c0");
        dumpFunc(0x004e8050L, "M830 brain FUN_004e8050");
        dumpFunc(0x004dce30L, "M840 brain FUN_004dce30");
        dumpFunc(0x004cc3d0L, "M850 brain FUN_004cc3d0");
        dumpFunc(0x004e3070L, "M860 brain FUN_004e3070");
        dumpFunc(0x004f6520L, "M890 brain FUN_004f6520");
        dumpFunc(0x004ee000L, "M930 brain FUN_004ee000");
        dumpFunc(0x004eec20L, "M940 brain FUN_004eec20");
        dumpFunc(0x004ef840L, "M950 brain FUN_004ef840");
        dumpFunc(0x004f0470L, "M960 brain FUN_004f0470");
        dumpFunc(0x004ca1a0L, "M970 brain FUN_004ca1a0");
        dumpFunc(0x004d4370L, "M980 brain FUN_004d4370");
        dumpFunc(0x004f7980L, "idx93 brain FUN_004f7980");
        close();
    }
}
