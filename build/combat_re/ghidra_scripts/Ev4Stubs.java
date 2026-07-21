//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.*;

public class Ev4Stubs extends Re2Lib {

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

        // FireBullet (scripted projectile spawn) - 22 operands, big
        dumpRange(0x0056aaa5L, 220, "0x111 FireBullet(iiifffffffffffiiifffff)");

        // Glare / bloom post-fx
        dumpRange(0x0056a85aL, 24, "0x10a GlareLevel(i)");
        dumpRange(0x0056a877L, 24, "0x10b GlareFeedback(i)");
        dumpRange(0x0056a894L, 30, "0x10c GlareSubtract(i)");
        dumpRange(0x0056a8b4L, 80, "0x10d DebugArena(i)");

        // Effect tail (not yet decoded)
        dumpRange(0x0056ad63L, 90, "0x112 WaterWallHitchk(fffff)");
        dumpRange(0x0056ae28L, 40, "0x113 Deburis(fff)");
        dumpRange(0x0056ae5bL, 100, "0x114 Blood(fffiii)");
        dumpRange(0x0056b24eL, 60, "0x11c Eft_SetAlphaTarget(sfi)");
        dumpRange(0x0056b2b9L, 60, "0x11f Eft_SetAlphaCurrent(sf)");
        dumpRange(0x0056b2fbL, 100, "0x11a Eft_LocatePosAddStr(ifffffsf)");
        dumpRange(0x0056b3cfL, 50, "0x11b EftAll_AddCameraOffset(if)");
        dumpRange(0x0056b411L, 24, "0x11d Scene_SetWaterPlane(f)");
        dumpRange(0x005664ccL, 170, "0x11e SetPointLight(16 args)");

        // Weather / environment
        dumpRange(0x0056b430L, 70, "0x120 FogSmoke(i)");
        dumpRange(0x0056b4d5L, 110, "0x121 SplashRain(iii)");
        dumpRange(0x0056b59cL, 90, "0x122 FrameSmoke(i)");
        dumpRange(0x0056b668L, 50, "0x123 FireBlur(fff)");
        dumpRange(0x0056b6ccL, 90, "0x124 SnowDrop(i)");
        dumpRange(0x0056b771L, 90, "0x125 FireDrop(ii)");
        dumpRange(0x0056b812L, 24, "0x126 MirrorFloor(i)");
        dumpRange(0x0056b832L, 24, "0x127 SetDefaultCubeMapTexture(s)");
        dumpRange(0x0056b84fL, 24, "0x128 SetDefaultSphereMapTexture(s)");
        dumpRange(0x0056b86cL, 60, "0x129 ChangeFog(if)");
        dumpRange(0x0056b8b3L, 50, "0x12a Eft_Die(ii)");

        // Menu builder tail
        dumpRange(0x0056a202L, 24, "0xd9 Menu_Select(s)");
        dumpRange(0x0056a220L, 24, "0xda Menu_AssignCancel(s)");
        dumpRange(0x0056a23eL, 16, "0xdb Menu_MoveCursorLastScript()");
        dumpRange(0x0056a24aL, 30, "0xdc Menu_RenameLastScript(s)");
        dumpRange(0x0056a149L, 70, "0xdd Menu_AddSpShop(ssi)");
        dumpRange(0x0056a267L, 30, "0xde Shop_Enter(i)");
        dumpRange(0x0056a286L, 16, "0xdf SysMenu_Enter()");
        dumpRange(0x0056a292L, 30, "0xe0 SaveMenu_Enter(s)");

        // Portrait tail
        dumpRange(0x0056a3b7L, 70, "0xe6 Portrait_Color(siiii)");
        dumpRange(0x0056a42bL, 50, "0xe7 Portrait_Priority(si)");
        dumpRange(0x0056a460L, 60, "0xe8 Portrait_FadeSpeed(sf)");

        // Chr_Damage inner apply worker (known addr)
        dumpFunc(0x00571ae0L, "Chr_Damage inner FUN_00571ae0(actor,hp,mode)");

        close();
    }
}
