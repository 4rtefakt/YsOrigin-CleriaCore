//@category YsOrigin
public class IsThisChr extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x005706c0L, "op 0x13e Chr_IsThisChr worker");
        close();
    }
}
