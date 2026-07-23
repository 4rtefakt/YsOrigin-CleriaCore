//@category YsOrigin
import ghidra.program.model.address.*;
public class Op13dRegion extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0042dc20L, "region/box name lookup FUN_0042dc20 (param->box descriptor)");
        dumpFunc(0x00434d50L, "entity-by-id resolver FUN_00434d50");
        dumpFunc(0x0056bdaaL, "sub 0x13c handler");
        dumpFunc(0x0056be1bL, "sub 0x13e handler");
        // resolver of the string/int operand used by 0x13d
        dumpFunc(0x00566100L, "operand resolver FUN_00566100 (0x13d op2)");
        close();
    }
}
