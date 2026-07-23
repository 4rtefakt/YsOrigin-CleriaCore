//@category YsOrigin
public class BoxLoader extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00442260L, "named-box table loader FUN_00442260 (writes DAT_0076a2b4)");
        close();
    }
}
