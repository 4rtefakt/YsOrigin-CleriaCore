//@category YsOrigin
public class SixXref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x005edc80L, "FUN_005edc80");
        xrefs(0x0044a380L, "FUN_0044a380");
        xrefs(0x005da4c0L, "FUN_005da4c0");
        xrefs(0x005d9990L, "FUN_005d9990");
        xrefs(0x004fabf0L, "FUN_004fabf0");
        xrefs(0x004057d0L, "FUN_004057d0");
        xrefs(0x0041f570L, "FUN_0041f570 (shared sqrt helper)");
        close();
    }
}
