//@category YsOrigin
import ghidra.program.model.address.*;
public class DlgPortrait4 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The c_NNN name-pointer table PTR_s_c_000_0068a800: dump 48 pointers + strings.
        out.println("===== ani slot name table @0x68a800 =====");
        for (int i = 0; i < 80; i++) {
            long p = currentProgram.getMemory().getInt(toAddr(0x0068a800L + i*4L)) & 0xffffffffL;
            StringBuilder sb = new StringBuilder();
            if (p > 0x400000 && p < 0x700000) {
                for (int k = 0; k < 32; k++) {
                    int c = currentProgram.getMemory().getByte(toAddr(p + k)) & 0xff;
                    if (c == 0) break;
                    sb.append((char) c);
                }
            }
            out.printf("  slot 0x%02x (%2d) -> %s%n", i, i, sb.toString());
        }
        // Portrait quad verts (4 x 7 floats) + frame strip for coords.
        out.println("\n===== PORTRAIT quad DAT_00732a80 (4 verts x [x y z rhw u v col?]) =====");
        for (int v = 0; v < 4; v++) {
            StringBuilder sb = new StringBuilder("  v" + v + ":");
            for (int f = 0; f < 7; f++) {
                int raw = currentProgram.getMemory().getInt(toAddr(0x00732a80L + (v*7+f)*4L));
                sb.append(String.format(" %10.3f", Float.intBitsToFloat(raw)));
            }
            out.println(sb);
        }
        close();
    }
}
