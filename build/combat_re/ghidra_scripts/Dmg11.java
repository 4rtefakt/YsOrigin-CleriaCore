// Dmg11: identify the type of the ECX/param_1 object handed to FUN_004fd350.
// Dump the hit-resolution callers, plus every field offset each one touches on that object.
//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class Dmg11 extends Re2Lib {
    public void run() throws Exception {
        open("D:\\ghidra-work\\dmg11_out.txt");
        out.println("######## DMG11: what object is the attacker-side arg? ########");
        dumpFunc(0x00504b00L, "FUN_00504b00 (hit resolution A)");
        dumpFunc(0x00505640L, "FUN_00505640 (hit resolution B)");
        dumpFunc(0x004fe9e0L, "FUN_004fe9e0 (hit resolution C)");
        dumpFunc(0x004fba30L, "FUN_004fba30");
        close();
    }
}
