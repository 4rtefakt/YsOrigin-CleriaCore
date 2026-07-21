//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss06Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00544980L, "boss06 BRAIN FUN_00544980");
        dumpFunc(0x0053d700L, "boss06 MAIN DRIVER FUN_0053d700");
        dumpFunc(0x005423b0L, "boss06 attack FUN_005423b0");
        dumpFunc(0x00540d40L, "boss06 attack FUN_00540d40");
        dumpFunc(0x0053af10L, "boss06 attack FUN_0053af10");
        dumpFunc(0x005434f0L, "boss06 case1 FUN_005434f0");
        dumpFunc(0x005439d0L, "boss06 case2 FUN_005439d0");
        dumpFunc(0x00543e00L, "boss06 case3 FUN_00543e00");
        dumpFunc(0x005357c0L, "boss06 case5a FUN_005357c0");
        dumpFunc(0x00535dc0L, "boss06 case5b FUN_00535dc0");
        dumpFunc(0x00544030L, "boss06 case5c FUN_00544030");
        dumpFunc(0x00542cd0L, "boss06 shared FUN_00542cd0");
        dumpFunc(0x00544750L, "boss06 tail FUN_00544750");
        close();
    }
}
