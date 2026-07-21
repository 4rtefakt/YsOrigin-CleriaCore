//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class Ev3Stubs extends Re2Lib {

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

        // Chr combat/render still-remaining
        dumpRange(0x0056c116L, 50, "0x147 Chr_FreeMove(ifffff)");
        dumpRange(0x0056c2e2L, 30, "0x14c Chr_Knockback_Perfect(ii)");
        dumpRange(0x0056c322L, 30, "0x14d Chr_CollisionVanish(ii)");
        dumpRange(0x0056c3a2L, 30, "0x14f Chr_ResistAttrib(ii)");
        dumpRange(0x0056c57aL, 60, "0x155 Chr_ChangeDiffuse(iiifi)");
        dumpRange(0x0056c621L, 60, "0x156 Chr_SetAura(iiiii)");
        dumpRange(0x0056c6bcL, 40, "0x157 Chr_OffsetCameraZ(if)");
        dumpRange(0x0056c6feL, 40, "0x158 Chr_ZTest(ii)");
        dumpRange(0x0056c73eL, 40, "0x159 Chr_FootSound(ii)");
        dumpRange(0x0056c77eL, 60, "0x15a Chr_ChangeSpecular(iiifi)");
        dumpRange(0x0056c825L, 60, "0x15b Chr_Blood(iiii)");
        dumpRange(0x0056c949L, 40, "0x15d Chr_FlyOnOff(ii)");
        dumpRange(0x0056c991L, 70, "0x15e ChrAllInEventBox_SetForce(sfffii)");
        dumpRange(0x0056ca06L, 90, "0x15f ChrAll_SetPointForce(ffffiffi)");
        dumpRange(0x0056cae2L, 130, "0x160 Chr_SetStatus(iifffffffff)");
        dumpRange(0x0056cc8fL, 40, "0x161 Chr_SlantFire(if)");
        dumpRange(0x0056ccd1L, 90, "0x162 Chr_Tremble(iiiff)");
        dumpRange(0x0056cd86L, 40, "0x163 Chr_NoForce(ii)");
        dumpRange(0x0056cdc6L, 40, "0x164 Chr_AniDisable(ii)");
        dumpRange(0x0056ce46L, 40, "0x166 Chr_FlyShakeDisable(ii)");
        dumpRange(0x0056ce86L, 40, "0x167 Chr_AniChange(ii)");

        // Mesh animated-transform / material / work / spawn (0x16e-0x18a)
        dumpRange(0x0056cfb8L, 60, "0x16e Mesh_SetAlphaTarget(sfi)");
        dumpRange(0x0056d023L, 40, "0x16f Mesh_SetAlphaCurrent(sf)");
        dumpRange(0x0056d065L, 70, "0x170 Mesh_MoveTo(sfffff)");
        dumpRange(0x0056d0efL, 70, "0x171 Mesh_MoveOffset(sfffff)");
        dumpRange(0x0056d179L, 50, "0x172 Mesh_Rotate(sfffi)");
        dumpRange(0x0056d1c4L, 40, "0x173 Mesh_SetPosition(sfff)");
        dumpRange(0x0056d1f8L, 40, "0x174 Mesh_SetRotation(sfff)");
        dumpRange(0x0056d2d6L, 40, "0x177 Mesh_SetScript(ss)");
        dumpRange(0x0056d30aL, 24, "0x178 Mesh_ResetPosition(s)");
        dumpRange(0x0056d328L, 24, "0x179 Mesh_ResetRotation(s)");
        dumpRange(0x0056d346L, 50, "0x17a Mesh_IsMotionFinish(si)");
        dumpRange(0x0056d384L, 50, "0x17b Mesh_SetTimeScale(sf)");
        dumpRange(0x0056d3c6L, 60, "0x17c Mesh_AttachEffect(ssi)");
        dumpRange(0x0056d411L, 50, "0x17d Mesh_SetHp(sf)");
        dumpRange(0x0056d453L, 70, "0x17e Mesh_DropItem(sii)");
        dumpRange(0x0056d4b2L, 90, "0x17f Mesh_IsLengthCenter2PlayerSquare(sffffff)");
        dumpRange(0x0056d5d0L, 60, "0x180 Mesh_IsLengthCenter2Player(sf)");
        dumpRange(0x0056d622L, 40, "0x181 Mesh_AddPosition(sfff)");
        dumpRange(0x0056d656L, 90, "0x182 Mesh_SetMaterialColor(ssiii)");
        dumpRange(0x0056d6edL, 70, "0x183 Mesh_SetMaterialAlpha(ssf)");
        dumpRange(0x0056d74eL, 70, "0x184 Mesh_SetWork(sii)");
        dumpRange(0x0056d7c8L, 90, "0x185 Mesh_IsWorkEqualInt(sii)");
        dumpRange(0x0056d85eL, 90, "0x186 Mesh_SetSphereCollision(sffff)");
        dumpRange(0x0056d91dL, 90, "0x187 Mesh_Locate(ssffffffi)");
        dumpRange(0x0056d99aL, 90, "0x188 Mesh_DistanceToAlpha(ssff)");
        dumpRange(0x0056da24L, 90, "0x189 Mesh_SyncronizePos(ssiii)");
        dumpRange(0x0056dabbL, 70, "0x18a Mesh_Fall(sff)");

        // Arena
        dumpRange(0x0056a915L, 60, "0x18e Arena(iii)");
        dumpRange(0x0056a8d1L, 60, "0x18f ArenaSave()");

        // Camera_Change worker (left [C-DIS worker addr TBD] in batch1)
        dumpRange(0x00569ca5L, 60, "0xec Camera_Change(ffffi)");

        close();
    }
}
