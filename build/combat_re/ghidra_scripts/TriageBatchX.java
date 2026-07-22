//@category YsOrigin
import ghidra.program.model.address.*;
public class TriageBatchX extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00621a00L, "CRT triage FUN_00621a00");
        callersOf(0x00621a00L);
        dumpFunc(0x0051d700L, "GAME FUN_0051d700 mesh region");
        callersOf(0x0051d700L);
        dumpFunc(0x0053eab0L, "GAME FUN_0053eab0 boss region");
        callersOf(0x0053eab0L);
        dumpFunc(0x00552510L, "GAME FUN_00552510 boss/enemy region");
        callersOf(0x00552510L);
        dumpFunc(0x00502cb0L, "GAME FUN_00502cb0 object/effect region");
        callersOf(0x00502cb0L);
        dumpFunc(0x005cf090L, "GAME FUN_005cf090 window/menu region");
        callersOf(0x005cf090L);
        close();
    }
}
