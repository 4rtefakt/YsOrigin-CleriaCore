//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
public class SevFmt4 extends Re2Lib {
    void str(long va) throws Exception {
        Address a = toAddr(va);
        StringBuilder sb = new StringBuilder();
        Memory m = currentProgram.getMemory();
        for (int i=0;i<48;i++){ int b = m.getByte(a.add(i)) & 0xff; if(b==0){break;} sb.append((char)b);}
        out.println(String.format("  0x%08x = \"%s\"", va, sb.toString()));
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("=== path/extension strings ===");
        str(0x00698bbcL); // format "data\map\..."
        str(0x00698bf0L); // SEV parser extension
        str(0x00698be8L); // sibling parser extension
        dumpFunc(0x00432cb0L, "generic magic+recordsize file loader FUN_00432cb0 (SEV header validate)");
        close();
    }
}
