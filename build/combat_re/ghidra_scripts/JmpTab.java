//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class JmpTab extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        Memory mem = currentProgram.getMemory();
        long base = 0x00508374L;
        out.println("== boss01 action jump table @0x508374 (11 states) ==");
        for (int i=0;i<11;i++){
            Address a = toAddr(base + i*4);
            int v = mem.getInt(a);
            long t = ((long)v) & 0xffffffffL;
            out.println("  state["+i+"] -> 0x"+Long.toHexString(t));
        }
        close();
    }
}
