//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
public class SizeBand6H extends Re2Lib {
    void str(long va){
        Address a = toAddr(va);
        Data d = getDataAt(a);
        out.println("STR @0x"+Long.toHexString(va)+" = "+(d==null?"(nodata)":d.getDefaultValueRepresentation()));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0066fd70L, "helper FUN_0066fd70 (trig?)");
        dumpFunc(0x00670d30L, "helper FUN_00670d30 (trig?)");
        dumpFunc(0x0041f570L, "helper FUN_0041f570");
        str(0x6941c4L); str(0x6941c8L); str(0x6941d0L); str(0x6941b8L);
        str(0x689590L); str(0x6898bcL); str(0x6898b4L);
        close();
    }
}
