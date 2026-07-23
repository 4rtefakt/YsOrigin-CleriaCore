//@category YsOrigin
public class DlgPortrait3 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00444850L, "archive ctx getter FUN_00444850");
        dumpFunc(0x00444f70L, "resource find/load FUN_00444f70 (type 0x1a/0x15)");
        dumpFunc(0x00444dc0L, "face index -> record FUN_00444dc0");
        dumpFunc(0x00444fe0L, "face bitmap upload FUN_00444fe0");
        close();
    }
}
