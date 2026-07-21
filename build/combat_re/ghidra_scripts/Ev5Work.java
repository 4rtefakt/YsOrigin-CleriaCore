//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

public class Ev5Work extends Re2Lib {

    void cstr(long va, String tag) {
        try {
            Address a = toAddr(va);
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 64; i++) {
                byte b = currentProgram.getMemory().getByte(a.add(i));
                if (b == 0) break;
                sb.append((char)(b & 0xff));
            }
            out.println("STR @0x" + Long.toHexString(va) + " (" + tag + ") = \"" + sb + "\"");
        } catch (Exception e) { out.println("STR @0x" + Long.toHexString(va) + " read fail"); }
    }

    void ptrTable(long base, int n, String tag) {
        out.println("\nPTR TABLE @0x" + Long.toHexString(base) + " (" + tag + "):");
        try {
            for (int i = 0; i < n; i++) {
                Address slot = toAddr(base + i*4L);
                long p = currentProgram.getMemory().getInt(slot) & 0xffffffffL;
                StringBuilder sb = new StringBuilder();
                if (p != 0) {
                    Address a = toAddr(p);
                    for (int j = 0; j < 48; j++) {
                        byte b = currentProgram.getMemory().getByte(a.add(j));
                        if (b == 0) break;
                        sb.append((char)(b & 0xff));
                    }
                }
                out.println("  [" + i + "] 0x" + Long.toHexString(p) + " = \"" + sb + "\"");
            }
        } catch (Exception e) { out.println("  table read fail"); }
    }

    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));

        // --- script-control workers ---
        dumpFunc(0x0056fe10L, "CallC native dispatcher FUN_0056fe10");
        dumpFunc(0x00434970L, "StartScript GoScript spawner FUN_00434970");
        dumpFunc(0x00570330L, "StartScript arg1 resolve FUN_00570330");
        dumpFunc(0x0056ec20L, "GoWarpPoint warp-start FUN_0056ec20");
        dumpFunc(0x0056eb10L, "IsOtherScriptExist FUN_0056eb10");
        dumpFunc(0x0056e8d0L, "Fix FUN_0056e8d0");
        dumpFunc(0x0056eab0L, "Unfix/end cleanup FUN_0056eab0");
        dumpFunc(0x0040b2c0L, "IsFix query FUN_0040b2c0");
        dumpFunc(0x004358b0L, "IsEqualPreviousScene name-resolve FUN_004358b0");

        // --- light workers ---
        dumpFunc(0x00443ea0L, "Light_Directional FUN_00443ea0");
        dumpFunc(0x00443f50L, "Light_Ambient FUN_00443f50");

        // --- warp / IsEnableWarp constants ---
        cstr(0x00694c4cL, "IsEnableWarp compare-string");
        cstr(0x0069f0a4L, "warp table[0] name");
        cstr(0x0069f630L, "StartScript literal");
        cstr(0x00698c2cL, "StartScript2 path fmt");
        ptrTable(0x0068c190L, 16, "warp destination script names");

        close();
    }
}
