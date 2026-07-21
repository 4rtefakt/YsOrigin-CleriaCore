//@category YsOrigin
import ghidra.program.model.address.*;
public class PoolCallers extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00434f30L, "pool1 ACTOR pop FUN_00434f30");
        xrefs(0x004523e0L, "pool2 pop FUN_004523e0");
        xrefs(0x0045e1b0L, "pool3 pop FUN_0045e1b0");
        xrefs(0x004fff30L, "pool4 pop FUN_004fff30");
        xrefs(0x00435080L, "pool5 RENDER pop FUN_00435080");
        xrefs(0x004525a0L, "pool6 pop FUN_004525a0");
        xrefs(0x00421640L, "pool7 pop FUN_00421640");
        xrefs(0x0042de00L, "pool8 pop FUN_0042de00");
        xrefs(0x0040f700L, "pool9 pop FUN_0040f700");
        xrefs(0x00429ad0L, "pool11 pop FUN_00429ad0");
        xrefs(0x004444f0L, "pool12 pop FUN_004444f0");
        xrefs(0x004443a0L, "pool13 pop FUN_004443a0");
        xrefs(0x005752c0L, "pool14 pop FUN_005752c0");
        xrefs(0x00577ce0L, "pool15 pop FUN_00577ce0");
        xrefs(0x00455710L, "pool16 pop? FUN_00455710");
        close();
    }
}
