// Dmg9: (a) resolve the damage-formula constants as IEEE floats/doubles from raw bytes
//       (Ghidra's Data typing lied: it showed 0x48 / 0x29 as ints),
//       (b) find every accessor of actor+0x3bc (the death flag) - walk operands, not xrefs,
//       (c) dump the player-damage path FUN_004fa740 and the prob-round helper FUN_004217f0.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class Dmg9 extends Re2Lib {

    void f32(long va, String tag) {
        try {
            int raw = currentProgram.getMemory().getInt(toAddr(va));
            out.println(String.format("  0x%08x  raw=0x%08x  float=%s   %s", va, raw, Float.intBitsToFloat(raw), tag));
        } catch (Exception e) { out.println("  0x" + Long.toHexString(va) + " <unreadable> " + tag); }
    }
    void f64(long va, String tag) {
        try {
            long raw = currentProgram.getMemory().getLong(toAddr(va));
            out.println(String.format("  0x%08x  raw=0x%016x  double=%s   %s", va, raw, Double.longBitsToDouble(raw), tag));
        } catch (Exception e) { out.println("  0x" + Long.toHexString(va) + " <unreadable> " + tag); }
    }

    void scanOffset(String off, String tag) {
        out.println("\n===== ALL ACCESSES TO [reg + " + off + "]  (" + tag + ") =====");
        Pattern p = Pattern.compile("\\[(EAX|EBX|ECX|EDX|ESI|EDI)\\s*\\+\\s*" + off + "\\]");
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        int n = 0;
        while (it.hasNext()) {
            Instruction i = it.next();
            String s = i.toString();
            if (!p.matcher(s).find()) continue;
            int comma = s.indexOf(',');
            String dst = comma < 0 ? s : s.substring(0, comma);
            String kind = p.matcher(dst).find() ? "W" : "R";
            out.println("  [" + kind + "] " + i.getAddress() + "  " + s + "    [" + fnName(i.getAddress()) + "]");
            n++;
        }
        out.println("  total: " + n);
    }

    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg9_out.txt");
        out.println("######## DMG9: constants, death flag, player path ########");

        out.println("\n===== DAMAGE FORMULA CONSTANTS (raw bytes -> IEEE) =====");
        f64(0x006bd520L, "pow exponent  (FUN_004fd350 @4fd40e)");
        f32(0x006bd414L, "DEF multiplier k  (@4fd445 MULSS [EDI+0xa8],k)");
        f32(0x006bd928L, "additive term     (@4fd454)");
        f32(0x006bd9b0L, "divisor           (@4fd465)");
        f32(0x006bdafcL, "prob-round scale  (@4fd4b5)");
        f32(0x006bd438L, "the 1.0 alive threshold");
        f32(0x006bd6acL, "PI (knockback angle, @571b55)");
        f32(0x006bd19cL, "local_24 init (@4fd35e)");
        out.println("  -- for reference, the stat-curve constants from prior work --");
        f32(0x006bda90L, "level denominator (expect 59.0)");
        f32(0x006bd580L, "expect 2.0");
        f32(0x006bda60L, "expect 50.0");
        f32(0x006bd994L, "expect 20.0");

        scanOffset("0x3bc", "DEATH FLAG - bit 0x10 set by FUN_004fd350 @4fdc84");

        dumpFunc(0x004fa740L, "PLAYER-DAMAGE PATH FUN_004fa740");
        dumpFunc(0x004217f0L, "PROB-ROUND / RNG CHANCE FUN_004217f0");
        close();
    }
}
