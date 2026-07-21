//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class Ef2Trk2 extends Re2Lib {
    void vtbl(long va, int n) throws Exception {
        out.println("\n===== VTABLE @0x" + Long.toHexString(va) + " =====");
        Memory m = currentProgram.getMemory();
        for (int i=0;i<n;i++){
            long p = m.getInt(toAddr(va + i*4)) & 0xffffffffL;
            out.println("  ["+i+"] +0x"+Integer.toHexString(i*4)+"  -> 0x"+Long.toHexString(p)+"  "+fnName(toAddr(p)));
        }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        vtbl(0x0069b1d4L, 32);
        dumpFunc(0x00450410L, "creator FUN_00450410");
        close();
    }
}
