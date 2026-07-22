//@category YsOrigin
public class SqrtChk extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x0041f570L, "FUN_0041f570 shared sqrt");
        dumpFunc(0x004faf20L, "FUN_004faf20 caller of 4fabf0");
        dumpFunc(0x0044a4f0L, "FUN_0044a4f0 caller of 44a380 (movie)");
        close();
    }
}
