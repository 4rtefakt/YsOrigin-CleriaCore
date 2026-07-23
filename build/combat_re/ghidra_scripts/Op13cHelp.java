//@category YsOrigin
public class Op13cHelp extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00570bc0L, "0x13c helper FUN_00570bc0 (is-moving?)");
        dumpFunc(0x005706c0L, "0x13e helper FUN_005706c0 (is-this-chr?)");
        close();
    }
}
