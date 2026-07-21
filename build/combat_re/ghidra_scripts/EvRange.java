//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class EvRange extends Re2Lib {

    // Raw-disassemble `count` instructions starting at va, annotating call targets,
    // data values, and referenced string/label addresses. Works even when va is not
    // inside a defined function (the event-VM handlers live in undefined holes).
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
            // stop at an unconditional return; keep going through jmps back to the loop
            if (m.equals("RET") || m.equals("RETN")) { out.println("  -- ret --"); break; }
            i = i.getNext();
            n++;
        }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // Chr_* actor control
        dumpRange(0x0056b8f3L, 60, "0x12c Chr_Locate");
        dumpRange(0x0056b99aL, 20, "0x12d Chr_Delete");
        dumpRange(0x0056b9b9L, 24, "0x12e Chr_NpcOrEnemy");
        dumpRange(0x0056b9f9L, 24, "0x12f Chr_LocateDummy");
        dumpRange(0x0056ba39L, 24, "0x130 Chr_AttachScript");
        dumpRange(0x0056ba79L, 24, "0x132 Chr_TurnSpeed");
        dumpRange(0x0056babbL, 24, "0x133 Chr_Dir");
        dumpRange(0x0056bafdL, 24, "0x134 Chr_DirChr");
        dumpRange(0x0056bb3dL, 24, "0x135 Chr_Dir2");
        dumpRange(0x0056bb7fL, 24, "0x136 Chr_LookAtChr");
        dumpRange(0x0056bbbfL, 24, "0x137 Chr_LookAtPos");
        dumpRange(0x0056bbffL, 24, "0x138 Chr_LookAtDir");
        dumpRange(0x0056be93L, 24, "0x140 Chr_Pos");
        dumpRange(0x0056bed3L, 24, "0x141 Chr_Jump");
        dumpRange(0x0056bf15L, 30, "0x142 Chr_SetForce");
        dumpRange(0x0056bf7eL, 40, "0x143 Chr_MoveTo");
        dumpRange(0x0056c00aL, 40, "0x144 Chr_FlyTo");
        dumpRange(0x0056c096L, 24, "0x145 Chr_RunOrWalk");
        dumpRange(0x0056c0d6L, 24, "0x146 Chr_RandomWalk");
        dumpRange(0x0056c116L, 40, "0x147 Chr_FreeMove");
        dumpRange(0x0056c19eL, 30, "0x148 Chr_Emotion");
        dumpRange(0x0056c201L, 30, "0x149 Chr_Popup");
        dumpRange(0x0056c262L, 24, "0x14a Chr_Attack");
        dumpRange(0x0056c2a2L, 30, "0x14b Chr_Knockback");
        dumpRange(0x0056c362L, 24, "0x14e Chr_Muteking");
        dumpRange(0x0056c3e2L, 16, "0x150 Chr_LieDown");
        dumpRange(0x0056c401L, 24, "0x151 Chr_Animate");
        dumpRange(0x0056c441L, 30, "0x152 Chr_Pattern");
        dumpRange(0x0056c4a2L, 24, "0x153 Chr_Shadow");
        dumpRange(0x0056c4e2L, 40, "0x154 Chr_Alpha");
        dumpRange(0x0056c8b3L, 40, "0x15c Chr_Damage");
        dumpRange(0x0056cae2L, 90, "0x160 Chr_SetStatus");
        dumpRange(0x0056ce06L, 24, "0x165 Chr_Stop");

        // Window / dialogue / portrait
        dumpRange(0x00569fe9L, 12, "0xd2 Window_Close");
        dumpRange(0x00569ff5L, 60, "0xd3 Window_Msg");
        dumpRange(0x0056a076L, 16, "0xd4 Window_Menu");
        dumpRange(0x0056a08dL, 40, "0xd5 Window_Treasure");
        dumpRange(0x0056a0ecL, 16, "0xd6 SetMsgSpd");
        dumpRange(0x0056a109L, 12, "0xd7 Menu_Init");
        dumpRange(0x0056a115L, 40, "0xd8 Menu_Add");
        dumpRange(0x0056a2b0L, 20, "0xe2 Caption");
        dumpRange(0x0056a2ceL, 40, "0xe3 Portrait_Open");
        dumpRange(0x0056a302L, 20, "0xe4 Portrait_Close");
        dumpRange(0x0056a320L, 60, "0xe5 Portrait_Move");

        // Scene / Camera / Fader
        dumpRange(0x00568c95L, 30, "0x97 ChangeScene");
        dumpRange(0x00568ccaL, 30, "0x98 Scene_Replace");
        dumpRange(0x00568fb0L, 60, "0x9a IsXXX");
        dumpRange(0x00569b88L, 60, "0xc3 Scroll_MoveTo");
        dumpRange(0x00569be7L, 30, "0xc4 Scroll_Return");
        dumpRange(0x00569c27L, 30, "0xc5 Camera_Set");
        dumpRange(0x00569c67L, 24, "0xc6 Camera_Return");
        dumpRange(0x00569c86L, 24, "0xeb CopyCamera_Set");
        dumpRange(0x00569ca5L, 60, "0xec Camera_Change");
        dumpRange(0x00569d71L, 24, "0xc8 Fader_Current");
        dumpRange(0x00569da9L, 24, "0xc9 Fader_Target");
        dumpRange(0x00569de1L, 30, "0xca Fader_Color");
        dumpRange(0x00569e24L, 24, "0xcb Fader_Priority");
        dumpRange(0x00569e4fL, 30, "0xcc Fader_Time");
        dumpRange(0x00569e95L, 60, "0xcd CrossFade");

        // Wait_*
        dumpRange(0x0056df91L, 30, "0xf0 Wait_Interval");
        dumpRange(0x0056a4a2L, 24, "0xf1 Wait_Fader");
        dumpRange(0x0056de74L, 20, "0xf2 Wait_Time");
        dumpRange(0x0056a4d0L, 12, "0xf3 Wait_Prompt");
        dumpRange(0x0056a4d9L, 40, "0xf4 Wait_Script");
        dumpRange(0x0056a543L, 12, "0xf5 Wait_WindowOpen");
        dumpRange(0x0056a54fL, 12, "0xf6 Wait_WindowClose");
        dumpRange(0x0056a55bL, 30, "0xf7 Wait_Sound");
        dumpRange(0x0056a59bL, 40, "0xf8 Wait_FlagEqualInt");
        dumpRange(0x0056a5e9L, 24, "0xf9 Wait_EndingTimer");
        dumpRange(0x0056a608L, 50, "0xfd Wait_ChrInSphere");
        dumpRange(0x0056a66bL, 50, "0xfe Wait_ChrInCylinder");
        dumpRange(0x0056a756L, 24, "0x100 Wait_ChrMoving");
        dumpRange(0x0056a7c5L, 24, "0x104 Wait_MeshMoving");
        dumpRange(0x0056a7e3L, 40, "0x105 Wait_MeshMotionFinish");

        // Mesh_* mislabels + neighbours
        dumpRange(0x0056cec6L, 24, "0x169 Mesh_Delete");
        dumpRange(0x0056cee4L, 30, "0x16a Mesh_Sleep");
        dumpRange(0x0056cf19L, 30, "0x16b Mesh_SetVisible");
        dumpRange(0x0056cf4eL, 30, "0x16c Mesh_EnableCollision");
        dumpRange(0x0056cf83L, 30, "0x16d Mesh_StopMotion");
        dumpRange(0x0056d22cL, 40, "0x175 Mesh_SetMotion");
        dumpRange(0x0056d2a1L, 30, "0x176 Mesh_EnableEvent");

        close();
    }
}
