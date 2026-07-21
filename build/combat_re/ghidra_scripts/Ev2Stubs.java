//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class Ev2Stubs extends Re2Lib {

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

        // ---- Flag/Reg arithmetic & compare dispatcher (confirm 0x76b91c + FUN_00570240) ----
        dumpRange(0x00567cdcL, 20, "0x64 Flag_SetInt");
        dumpRange(0x00567d78L, 24, "0x67 Flag_AddInt");
        dumpRange(0x00567e0aL, 24, "0x69 Flag_SubInt");
        dumpRange(0x00567e9cL, 24, "0x6b Flag_MulInt");
        dumpRange(0x00567f34L, 30, "0x6d Flag_DivInt");
        dumpRange(0x00567fefL, 30, "0x6f Flag_ModInt");
        dumpRange(0x00568052L, 24, "0x70 Flag_AndInt?");
        dumpRange(0x00568260L, 30, "0x77 Flag_AndFlagShl");
        dumpRange(0x00567bf8L, 30, "0x61 Flag_CmpInt (dispatcher)");
        dumpRange(0x005679f6L, 40, "0x5c Reg_EqualIntAndEqualInt");

        // ---- Item/Party/Save setters ----
        dumpRange(0x00568582L, 60, "0x7f SetWeaponLevel");
        dumpRange(0x005684cfL, 40, "0x82 SetPlayerLevel");
        dumpRange(0x005686a8L, 60, "0x83 SetRavalLevel");
        dumpRange(0x0056876dL, 40, "0x84 SetGoldflagNowZenny");
        dumpRange(0x005687c1L, 40, "0x85 HealHp");
        dumpRange(0x0056880aL, 12, "0x86 GiveO2");
        dumpRange(0x00568819L, 30, "0x87 Set_ItemField");
        dumpRange(0x00568843L, 60, "0x88 Set_Accessory");
        dumpRange(0x005688f1L, 40, "0x89 EquipAccessory");
        dumpRange(0x00568932L, 24, "0x8a EquipTool");
        dumpRange(0x0056894fL, 60, "0x8b EventXX");
        dumpRange(0x005689d7L, 24, "0x8c Flag_IsEquip");
        dumpRange(0x005689fdL, 40, "0x8d Flag_IsInvOwnMax");
        dumpRange(0x00568a35L, 30, "0x8f Set_Skill");
        dumpRange(0x005687a4L, 24, "0xb2 AddPlayerSP");
        dumpRange(0x00568c8bL, 12, "0x96 SaveContData");
        dumpRange(0x00568bcbL, 40, "0x94 SetHealSpot");
        dumpRange(0x00568ad0L, 24, "0x92 SetManualEnableIndex");
        dumpRange(0x00568af4L, 40, "0x93 SetTopicKnownIndex");

        // ---- Effect_GetItem + item drops ----
        dumpRange(0x0056b006L, 50, "0x116 Effect_GetItem");
        dumpRange(0x0056af0dL, 60, "0x115 Set_DropItem");

        // ---- Bgm / Se ----
        dumpRange(0x005698eaL, 20, "0xb9 Bgm_Play");
        dumpRange(0x0056990cL, 20, "0xba Bgm_Stop");
        dumpRange(0x00569929L, 30, "0xbb Bgm_Volume");
        dumpRange(0x00569968L, 60, "0xbc Se_Play");
        dumpRange(0x00569a27L, 60, "0xbd Se_Play3DPos");
        dumpRange(0x00569a97L, 40, "0xbe Se_Play3DChr");
        dumpRange(0x00569ae7L, 30, "0xbf Se_Stop");
        dumpRange(0x00569b26L, 40, "0xc0 Se_Volume");
        dumpRange(0x0056966aL, 16, "0xac Se_Load");
        dumpRange(0x005698eaL, 4, "pad");

        // ---- Effects: enclose / kill / break / spawn ----
        dumpRange(0x0056a977L, 40, "0x10e EnclosePlayer");
        dumpRange(0x0056a9c2L, 12, "0x10f KillAllZako");
        dumpRange(0x0056a9ccL, 80, "0x110 BreakObj");
        dumpRange(0x0056b067L, 80, "0x117 Eft_LocatePos");
        dumpRange(0x0056b112L, 80, "0x118 Eft_LocateChr");
        dumpRange(0x0056b230L, 40, "0x119 Eft_Kill");
        dumpRange(0x00569f43L, 30, "0xce ZoomBlur");
        dumpRange(0x00569f86L, 40, "0xcf Tilt");
        dumpRange(0x00569fccL, 20, "0xd0 PadTilt");

        // ---- 0x9a IsXXX story bitset ----
        dumpRange(0x00568fb0L, 120, "0x9a IsXXX (bitset 0x76bc80)");

        close();
    }
}
