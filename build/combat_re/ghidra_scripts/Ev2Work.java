//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class Ev2Work extends Re2Lib {

    public void dumpRange(long va, int count, String tag) {
        Address a = toAddr(va);
        out.println("\n\n======== " + tag + " @ 0x" + Long.toHexString(va) + " ========");
        Instruction i = currentProgram.getListing().getInstructionAt(a);
        if (i == null) { out.println("  (no instruction at address; maybe data)"); return; }
        int n = 0;
        while (i != null && n < count) {
            StringBuilder sb = new StringBuilder("  " + i.getAddress() + "  " + i);
            for (Reference r : i.getReferencesFrom()) {
                Address to = r.getToAddress();
                Data d = currentProgram.getListing().getDataAt(to);
                if (d != null && d.getValue() != null) sb.append("   ; -> " + d.getValue());
                Function tf = currentProgram.getFunctionManager().getFunctionAt(to);
                if (tf != null) sb.append("   ; call " + tf.getName());
            }
            out.println(sb);
            String m = i.getMnemonicString();
            if (m.equals("RET") || m.equals("RETN")) { out.println("  -- ret --"); break; }
            i = i.getNext();
            n++;
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // Chr remainder worker functions (resolve param splits)
        dumpFunc(0x00570ad0L, "Chr_MoveTo/SetForce worker FUN_00570ad0");
        dumpFunc(0x00570b30L, "Chr_FlyTo worker FUN_00570b30");
        dumpFunc(0x00571b80L, "Chr_Damage worker FUN_00571b80");
        dumpFunc(0x005715d0L, "Chr_Alpha worker FUN_005715d0");
        dumpFunc(0x005712e0L, "Chr_Pattern worker FUN_005712e0");

        // Chr_SetStatus (0x160): dump stub to see the 10-float block layout, then worker
        dumpRange(0x0056cae2L, 110, "0x160 Chr_SetStatus stub");

        // Key workers to cite (item / save / audio / char-query)
        dumpFunc(0x00573210L, "Effect_GetItem worker FUN_00573210 (inventory add)");
        dumpFunc(0x00420210L, "char-owned query FUN_00420210 (0x9a op7/8)");
        dumpFunc(0x00443d70L, "SaveContData worker FUN_00443d70");
        dumpFunc(0x004fa0e0L, "HealHp worker FUN_004fa0e0");
        dumpFunc(0x00432ee0L, "Bgm_Play worker FUN_00432ee0");
        dumpFunc(0x0043a040L, "Se_Play worker FUN_0043a040");

        close();
    }
}
