//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class Ev5Stubs extends Re2Lib {

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

        // Misc queries / setters
        dumpRange(0x00568a66L, 22, "0x90 Flag_IsAndInt");
        dumpRange(0x00568abaL, 12, "0x91 IsTimeAttackBossRush");
        dumpRange(0x00568c69L, 16, "0x95 SetTimeScale");
        dumpRange(0x00568cf9L, 60, "0x99 IsEqualPreviousScene");

        // Scene static loaders + setup
        dumpRange(0x0056934aL, 14, "0x9b Scene_LoadStaticObjects");
        dumpRange(0x00569367L, 14, "0x9c Scene_LoadStaticEffects");
        dumpRange(0x00569384L, 14, "0x9d Scene_LoadStaticFog");
        dumpRange(0x005693a1L, 14, "0x9e Scene_LoadStaticCamera");
        dumpRange(0x005693beL, 20, "0x9f Scene_LoadStaticEntries");
        dumpRange(0x005693f7L, 14, "0xa0 Scene_LoadStaticCast");
        dumpRange(0x00569414L, 14, "0xa1 Scene_LoadStaticScnInf");
        dumpRange(0x00569431L, 24, "0xa2 Scene_SetClipPlain");
        dumpRange(0x00569473L, 24, "0xa3 Scene_SetGlobalEffect");
        dumpRange(0x005694b3L, 16, "0xa4 Scene_SetDefaultBgm");

        // Warp
        dumpRange(0x005694d0L, 40, "0xa5 IsEnableWarp");
        dumpRange(0x0056952cL, 16, "0xa6 GoWarpPoint");
        dumpRange(0x00569546L, 20, "0xa7 SetWarpEnableIndex");

        // Scene setters + lights
        dumpRange(0x00569565L, 18, "0xa8 Scene_DamageRatio");
        dumpRange(0x00569587L, 30, "0xa9 Scene_MapPoint");
        dumpRange(0x005695c9L, 40, "0xaa Light_Directional");
        dumpRange(0x00569608L, 50, "0xab Light_Ambient");

        // Preload + generic setters
        dumpRange(0x0056966aL, 16, "0xac Se_Load");
        dumpRange(0x00569687L, 16, "0xad Cast_Load");
        dumpRange(0x005696a4L, 20, "0xae Portrait_Load");
        dumpRange(0x00568d4aL, 60, "0xaf SetXXX");
        dumpRange(0x005691fdL, 60, "0xb0 SetRavalCostToFlag");
        dumpRange(0x005687a4L, 24, "0xb2 AddPlayerSP");

        // Script control / native call
        dumpRange(0x005692afL, 60, "0xb1 CallC");
        dumpRange(0x005696c2L, 60, "0xb3 IsOtherScriptExist");
        dumpRange(0x00569716L, 130, "0xb4 StartScript");
        dumpRange(0x0056989cL, 20, "0xb5 AllowMultiInstance");
        dumpRange(0x005698bfL, 20, "0xb6 Fix");
        dumpRange(0x005698deL, 16, "0xb7 Unfix");
        dumpRange(0x00569847L, 40, "0xb8 StartScript2");
        dumpRange(0x00568f62L, 24, "0xc1 IsFix");
        dumpRange(0x00568f88L, 30, "0xc2 IsUnderWaterWorld");

        close();
    }
}
