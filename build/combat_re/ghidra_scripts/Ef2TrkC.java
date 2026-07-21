//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class Ef2TrkC extends Re2Lib {
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
        dumpFunc(0x00563f20L, "GoParticle real ctor FUN_00563f20");
        dumpFunc(0x0044b520L, "particle manager tick FUN_0044b520");
        vtbl(0x0069f098L, 8);
        close();
    }
}
