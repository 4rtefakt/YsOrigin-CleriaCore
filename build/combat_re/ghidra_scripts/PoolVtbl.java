//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
public class PoolVtbl extends Re2Lib {
    // read a C string at addr
    String cstr(long va) {
        try {
            StringBuilder sb = new StringBuilder();
            Address a = toAddr(va);
            for (int i=0;i<200;i++){ byte b=currentProgram.getMemory().getByte(a.add(i)); if(b==0)break; sb.append((char)(b&0xff)); }
            return sb.toString();
        } catch(Exception e){ return "?"; }
    }
    // given a vtable VA, print RTTI class name via [vtbl-4]->[+0xC]->[+8]+8 (MSVC RTTI)
    void rtti(long vtbl) {
        try {
            Memory m = currentProgram.getMemory();
            long col = m.getInt(toAddr(vtbl-4)) & 0xffffffffL;    // CompleteObjectLocator
            long td  = m.getInt(toAddr(col+0xC)) & 0xffffffffL;   // TypeDescriptor
            String name = cstr(td+8);                             // .?AVClassName@@
            out.println(String.format("   VTBL 0x%08x  RTTI col=0x%08x td=0x%08x name=%s", vtbl, col, td, name));
        } catch(Exception e){ out.println(String.format("   VTBL 0x%08x  (no RTTI: %s)", vtbl, e)); }
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // reference: actor vtable
        rtti(0x0069ba3cL);
        dumpFunc(0x00478510L, "pool2 spawner FUN_00478510");
        dumpFunc(0x00464960L, "pool4 obj FUN_00464960");
        dumpFunc(0x00433110L, "pool5 RENDER spawner FUN_00433110");
        dumpFunc(0x0056fd20L, "pool9 spawner FUN_0056fd20");
        dumpFunc(0x005c4cc0L, "pool11 WINDOW spawner FUN_005c4cc0");
        dumpFunc(0x00454f40L, "pool16 spawner FUN_00454f40");
        close();
    }
}
