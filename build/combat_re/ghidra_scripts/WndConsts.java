//@category YsOrigin
public class WndConsts extends Re2Lib {
    void f(long a, String tag) throws Exception {
        int raw = currentProgram.getMemory().getInt(toAddr(a));
        out.printf("%s @0x%x = %f (0x%08x)%n", tag, a, Float.intBitsToFloat(raw), raw);
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        f(0x006bdac8L, "curl width (x caps)");
        f(0x006bdaa8L, "top cap height");
        f(0x006bd9f0L, "bottom cap height / cursor cap");
        f(0x006bd304L, "mystery scale");
        f(0x006bda64L, "icon/cell size");
        f(0x006bd438L, "anim time step");
        for (int i = 0; i < 4; i++) f(0x006bea50L + i*4, "V-cut " + i);
        f(0x006bd98cL, "icon x offset");
        out.println("cursor U cuts (from code): 0, 0.125, 0.875, 1.0");
        close();
    }
}
