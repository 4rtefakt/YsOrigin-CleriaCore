//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class Ef2Trk7 extends Re2Lib {
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
        dumpFunc(0x0044c250L, "particle init from emitter FUN_0044c250 (17-track sampler)");
        vtbl(0x0069f088L, 12);
        close();
    }
}
