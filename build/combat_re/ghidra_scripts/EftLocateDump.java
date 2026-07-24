//@category YsOrigin
import ghidra.program.model.address.*;
public class EftLocateDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0056ef90L, "0x117 Eft_LocatePos worker FUN_0056ef90 (args: name,id,x,y,z,scale,DURATION?)");
        dumpFunc(0x0056b067L, "0x117 dispatch site 0x56b067 (operand fetch order)");
        dumpFunc(0x0056f880L, "Eft_Kill worker FUN_0056f880 (how a named effect is found/killed)");
        close();
    }
}
