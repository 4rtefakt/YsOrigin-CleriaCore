//@category YsOrigin
// FUN_00450410 is the effect-spawn entry a FireBullet's arg[1] resolves through
// (DAT_0074e674 -> FUN_00450410). It calls the world-matrix builder at 0x4504d6, so
// whatever it passes as `rot` is the orientation a bullet-attached effect gets.
// FUN_00450880 is the sibling spawn at 0x450947.
public class Ef2Orient2 extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        dumpFunc(0x00450410L, "effect spawn FUN_00450410 (bullet art enters here)");
        dumpFunc(0x00450880L, "effect spawn sibling FUN_00450880");
        close();
    }
}
