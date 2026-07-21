//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class Ev3Work extends Re2Lib {

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

        // ---- Chr combat/render workers ----
        dumpFunc(0x005713e0L, "0x147 Chr_FreeMove worker");
        dumpFunc(0x00571250L, "0x14c Chr_Knockback_Perfect worker");
        dumpFunc(0x005712a0L, "0x14d Chr_CollisionVanish worker");
        dumpFunc(0x00571840L, "0x14f Chr_ResistAttrib worker");
        dumpFunc(0x00571660L, "0x155 Chr_ChangeDiffuse worker");
        dumpFunc(0x00571a90L, "0x156 Chr_SetAura worker");
        dumpFunc(0x005717e0L, "0x157 Chr_OffsetCameraZ worker");
        dumpFunc(0x00571790L, "0x158 Chr_ZTest worker");
        dumpFunc(0x00571320L, "0x159 Chr_FootSound worker");
        dumpFunc(0x005716c0L, "0x15a Chr_ChangeSpecular worker");
        dumpFunc(0x00571720L, "0x15b Chr_Blood worker");
        dumpFunc(0x00571bc0L, "0x15d Chr_FlyOnOff worker");
        dumpFunc(0x00571ef0L, "0x15e ChrAllInEventBox_SetForce worker");
        dumpFunc(0x005720b0L, "0x15f ChrAll_SetPointForce worker");
        dumpFunc(0x00571bf0L, "0x160 Chr_SetStatus worker (KEY)");
        dumpFunc(0x00571ca0L, "0x161 Chr_SlantFire worker");
        dumpFunc(0x00571ce0L, "0x162 Chr_Tremble worker");
        dumpFunc(0x00571d60L, "0x163 Chr_NoForce worker");
        dumpFunc(0x00571db0L, "0x164 Chr_AniDisable worker");
        dumpFunc(0x00571e70L, "0x166 Chr_FlyShakeDisable worker");
        dumpFunc(0x00571ec0L, "0x167 Chr_AniChange worker");

        // ---- Mesh transform / material / spawn workers ----
        dumpFunc(0x00572a20L, "Mesh name-resolver (shared, 0x184/0x185)");
        dumpFunc(0x00572b40L, "0x16e Mesh_SetAlphaTarget worker");
        dumpFunc(0x00572ba0L, "0x16f Mesh_SetAlphaCurrent worker");
        dumpFunc(0x00572d50L, "0x170 Mesh_MoveTo worker");
        dumpFunc(0x00572dc0L, "0x171 Mesh_MoveOffset worker");
        dumpFunc(0x00572e70L, "0x172 Mesh_Rotate worker");
        dumpFunc(0x00572ee0L, "0x173 Mesh_SetPosition worker");
        dumpFunc(0x00572f10L, "0x174 Mesh_SetRotation worker");
        dumpFunc(0x00573010L, "0x177 Mesh_SetScript worker");
        dumpFunc(0x00573050L, "0x178 Mesh_ResetPosition worker");
        dumpFunc(0x005730b0L, "0x179 Mesh_ResetRotation worker");
        dumpFunc(0x005730e0L, "0x17a Mesh_IsMotionFinish worker");
        dumpFunc(0x00573110L, "0x17b Mesh_SetTimeScale worker");
        dumpFunc(0x00572bd0L, "0x17c Mesh_AttachEffect worker");
        dumpFunc(0x00573330L, "0x17d Mesh_SetHp worker");
        dumpFunc(0x00573150L, "0x17e Mesh_DropItem worker");
        dumpFunc(0x005733f0L, "0x17f Mesh_IsLengthCenter2PlayerSquare worker");
        dumpFunc(0x00573370L, "0x180 Mesh_IsLengthCenter2Player worker");
        dumpFunc(0x00573570L, "0x181 Mesh_AddPosition worker");
        dumpFunc(0x005737e0L, "0x182 Mesh_SetMaterialColor worker");
        dumpFunc(0x005738b0L, "0x183 Mesh_SetMaterialAlpha worker");
        dumpFunc(0x005735b0L, "0x186 Mesh_SetSphereCollision worker");
        dumpFunc(0x00573930L, "0x187 Mesh_Locate worker");
        dumpFunc(0x00573aa0L, "0x188 Mesh_DistanceToAlpha worker");
        dumpFunc(0x00573b80L, "0x189 Mesh_SyncronizePos worker");
        dumpFunc(0x00573be0L, "0x18a Mesh_Fall worker");

        // ---- Arena ----
        dumpFunc(0x005722c0L, "0x18e Arena worker");

        // ---- Camera_Change tail to reveal worker addr ----
        dumpRange(0x00569d5eL, 6, "0xec Camera_Change call tail");

        close();
    }
}
