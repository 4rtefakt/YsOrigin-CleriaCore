//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
public class Task235b extends Re2Lib {
    void str(long va){
        Address a=toAddr(va);
        Data d=currentProgram.getListing().getDataAt(a);
        out.println("  0x"+Long.toHexString(va)+" -> "+(d!=null?(""+d.getValue()):"(no data)"));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("== strings ==");
        str(0x0067c87cL);
        out.println("== small callees (identify) ==");
        dumpFunc(0x004217f0L, "FUN_004217f0 (called by spline 536580)");
        out.println("\n== xrefs to manager global DAT_00764b94 ==");
        xrefs(0x00764b94L, "manager DAT_00764b94");
        out.println("\n== xrefs to DAT_007ddd18 (spline seg count) ==");
        xrefs(0x007ddd18L, "spline seg count");
        out.println("\n== callers of vtable-only fns ==");
        for(Address a: callersOf(0x005d83d0L)) out.println("  5d83d0 <- "+a+" ["+fnName(a)+"]");
        out.println("\n== xref FUN_00450410 (child factory in 51f280) callers count ==");
        int n=0; for(Address a: callersOf(0x00450410L)){ n++; }
        out.println("  FUN_00450410 caller count="+n);
        close();
    }
}
