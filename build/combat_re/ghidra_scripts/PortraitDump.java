//@category YsOrigin
import ghidra.program.model.address.*;
public class PortraitDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00574f50L, "0xe3 Portrait_Open worker FUN_00574f50 (position/anchor/scale)");
        dumpFunc(0x0058b090L, "portrait init FUN_0058b090");
        close();
    }
}
