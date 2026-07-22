//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class Task418c extends Re2Lib {
    void sig(long va, String tag) {
        Function f = fnAt(toAddr(va));
        if (f == null) { out.println(tag + " NO FUNC"); return; }
        out.println("\n== " + tag + " " + f.getName() + " @" + f.getEntryPoint()
            + " params=" + f.getParameterCount() + " conv=" + f.getCallingConventionName());
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // callee identities (short)
        sig(0x005f5c80L, "callee playAnim?");
        sig(0x0051ea30L, "callee advance?");
        sig(0x0043a080L, "callee spawn?");
        sig(0x004448f0L, "callee mover.getASO?");
        sig(0x00444850L, "callee mover.reset?");
        sig(0x00514200L, "caller-of-51db70");
        // xrefs to the tableless handlers (dispatch table entries)
        xrefs(0x00525790L, "FUN_00525790");
        xrefs(0x00525150L, "FUN_00525150");
        xrefs(0x00598d50L, "FUN_00598d50");
        // globals
        xrefs(0x0076bb74L, "DAT_0076bb74 char-index?");
        xrefs(0x0074c0d0L, "DAT_0074c0d0");
        close();
    }
}
