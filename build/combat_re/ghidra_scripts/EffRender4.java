//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
public class EffRender4 extends Re2Lib {
    void dumpMaybe(long va, String tag) throws Exception {
        Address a = toAddr(va);
        Function f = fnAt(a);
        if (f == null) f = createFunction(a, null);
        if (f == null) { out.println("\n### no func @0x" + Long.toHexString(va) + " (" + tag + ")"); return; }
        dumpFunc(va, tag);
    }
    public void run() throws Exception {
        open(System.getenv("EFF_OUT"));
        dumpMaybe(0x0041c530L, "trig FUN_0041c530");
        dumpMaybe(0x0041c510L, "trig FUN_0041c510");
        dumpMaybe(0x00565320L, "GoParticle draw callback FUN_00565320");
        dumpMaybe(0x00564c10L, "GoParticle vtbl5 FUN_00564c10");
        close();
    }
}
