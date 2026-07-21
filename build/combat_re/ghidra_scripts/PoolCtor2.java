//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class PoolCtor2 extends Re2Lib {
    String cstr(long va){try{StringBuilder s=new StringBuilder();Address a=toAddr(va);for(int i=0;i<200;i++){byte b=currentProgram.getMemory().getByte(a.add(i));if(b==0)break;s.append((char)(b&0xff));}return s.toString();}catch(Exception e){return "?";}}
    void rtti(long vtbl){try{Memory m=currentProgram.getMemory();long col=m.getInt(toAddr(vtbl-4))&0xffffffffL;long td=m.getInt(toAddr(col+0xC))&0xffffffffL;out.println(String.format("   VTBL 0x%08x  name=%s",vtbl,cstr(td+8)));}catch(Exception e){out.println(String.format("   VTBL 0x%08x (no rtti)",vtbl));}}
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        rtti(0x006a2b90L); // GoXx (pool2/9)
        rtti(0x006a6cc8L); // pool11 window?
        rtti(0x006a6d44L); // pool11 window?
        dumpFunc(0x004fae00L, "pool7 spawner FUN_004fae00");
        dumpFunc(0x00434970L, "pool8 spawner FUN_00434970");
        dumpFunc(0x00575c10L, "pool12 spawner FUN_00575c10");
        dumpFunc(0x005765c0L, "pool11/15 spawner FUN_005765c0");
        close();
    }
}
