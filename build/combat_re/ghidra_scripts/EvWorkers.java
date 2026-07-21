//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class EvWorkers extends Re2Lib {

    public void dumpRange(long va, int count, String tag) {
        Address a = toAddr(va);
        out.println("\n\n======== RANGE " + tag + " @ 0x" + Long.toHexString(va) + " ========");
        Instruction i = currentProgram.getListing().getInstructionAt(a);
        if (i == null) { out.println("  (no instruction)"); return; }
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

        // ---- cut-off handler tails: capture the worker CALL ----
        dumpRange(0x0056bff6L, 12, "0x143 Chr_MoveTo tail");
        dumpRange(0x0056c082L, 12, "0x144 Chr_FlyTo tail");
        dumpRange(0x0056c18cL, 14, "0x147 Chr_FreeMove tail");
        dumpRange(0x0056c1edL, 12, "0x148 Chr_Emotion tail");
        dumpRange(0x0056c24bL, 12, "0x149 Chr_Popup tail");
        dumpRange(0x0056c490L, 12, "0x152 Chr_Pattern tail");
        dumpRange(0x0056c557L, 20, "0x154 Chr_Alpha tail");
        dumpRange(0x0056c92aL, 24, "0x15c Chr_Damage tail");
        dumpRange(0x0056cc04L, 24, "0x160 Chr_SetStatus tail");
        dumpRange(0x0056d295L, 8,  "0x175 Mesh_SetMotion tail");

        // ---- entity resolver ----
        dumpFunc(0x00434d50L, "ENTITY RESOLVE FUN_00434d50 (id -> actor*)");

        // ---- Chr_* workers ----
        dumpFunc(0x00570700L, "Chr_Delete FUN_00570700");
        dumpFunc(0x00571470L, "Chr_NpcOrEnemy FUN_00571470");
        dumpFunc(0x00570350L, "Chr_LocateDummy FUN_00570350");
        dumpFunc(0x00570770L, "Chr_AttachScript FUN_00570770");
        dumpFunc(0x005714d0L, "Chr_TurnSpeed FUN_005714d0");
        dumpFunc(0x005708c0L, "Chr_Dir FUN_005708c0");
        dumpFunc(0x00570960L, "Chr_DirChr FUN_00570960");
        dumpFunc(0x00570820L, "Chr_LookAtPos FUN_00570820");
        dumpFunc(0x00570a40L, "Chr_Pos FUN_00570a40");
        dumpFunc(0x005709d0L, "Chr_Jump FUN_005709d0");
        dumpFunc(0x00570fb0L, "Chr_RunOrWalk FUN_00570fb0");
        dumpFunc(0x00570f60L, "Chr_RandomWalk FUN_00570f60");
        dumpFunc(0x00571350L, "Chr_Attack FUN_00571350");
        dumpFunc(0x00571200L, "Chr_Knockback FUN_00571200");
        dumpFunc(0x00571390L, "Chr_Muteking FUN_00571390");
        dumpFunc(0x00571810L, "Chr_LieDown FUN_00571810");
        dumpFunc(0x00571000L, "Chr_Animate FUN_00571000");
        dumpFunc(0x00570a80L, "Chr_Shadow FUN_00570a80");
        dumpFunc(0x00571e00L, "Chr_Stop FUN_00571e00");

        // ---- Window / portrait / menu workers ----
        dumpFunc(0x00573e10L, "Window_Close FUN_00573e10");
        dumpFunc(0x00573e60L, "Window_Msg FUN_00573e60");
        dumpFunc(0x00574410L, "Window_Treasure FUN_00574410");
        dumpFunc(0x005744f0L, "Window_Menu FUN_005744f0");
        dumpFunc(0x00574f50L, "Portrait_Open FUN_00574f50");
        dumpFunc(0x00575050L, "Portrait_Close FUN_00575050");

        // ---- Scene / Camera / Scroll workers ----
        dumpFunc(0x0056ec20L, "ChangeScene FUN_0056ec20");
        dumpFunc(0x00433050L, "Scene_Replace FUN_00433050");
        dumpFunc(0x0056ec40L, "Scroll_MoveTo FUN_0056ec40");
        dumpFunc(0x0056ed40L, "Camera_Set FUN_0056ed40");
        dumpFunc(0x0056ed70L, "Camera_Return FUN_0056ed70");

        // ---- Fader workers (on fader obj 0x769f18) ----
        dumpFunc(0x0042e970L, "Fader_Current FUN_0042e970");
        dumpFunc(0x00427490L, "Fader_Target FUN_00427490");

        // ---- Mesh workers (the mislabels) ----
        dumpFunc(0x00572a30L, "Mesh_Delete FUN_00572a30");
        dumpFunc(0x00572a60L, "Mesh_Sleep FUN_00572a60");
        dumpFunc(0x00572aa0L, "Mesh_SetVisible FUN_00572aa0");
        dumpFunc(0x00572ae0L, "Mesh_EnableCollision FUN_00572ae0");
        dumpFunc(0x00572b20L, "Mesh_StopMotion FUN_00572b20");

        close();
    }
}
