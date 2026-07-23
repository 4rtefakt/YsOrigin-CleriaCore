//@category YsOrigin
public class BoxTableXref extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x0076a2b4L, "named-box table base DAT_0076a2b4 (find writer/loader)");
        xrefs(0x0076a2b8L, "named-box table count DAT_0076a2b8");
        close();
    }
}
