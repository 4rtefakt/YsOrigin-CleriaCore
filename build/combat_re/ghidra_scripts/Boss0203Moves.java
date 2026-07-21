//@category YsOrigin
import ghidra.program.model.address.*;
public class Boss0203Moves extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        xrefs(0x00509260L, "who commits boss01 states (FUN_00509260 callers)");
        // boss02 Nygtilger
        dumpFunc(0x00518ae0L, "boss02 BRAIN FUN_00518ae0");
        dumpFunc(0x00516740L, "boss02 attack drv FUN_00516740");
        dumpFunc(0x00515250L, "boss02 attack drv FUN_00515250");
        dumpFunc(0x0051b850L, "boss02 fn FUN_0051b850");
        dumpFunc(0x00518220L, "boss02 fn FUN_00518220");
        // boss03 Gelaldy
        dumpFunc(0x00521130L, "boss03 DISPATCH FUN_00521130");
        dumpFunc(0x005227e0L, "boss03 perframe FUN_005227e0");
        dumpFunc(0x00523860L, "boss03 atk0 FUN_00523860");
        dumpFunc(0x0051ea30L, "boss03 atk1 FUN_0051ea30");
        dumpFunc(0x00523880L, "boss03 atk2 FUN_00523880");
        dumpFunc(0x00523b30L, "boss03 atk3 FUN_00523b30");
        dumpFunc(0x00523fd0L, "boss03 atk4 FUN_00523fd0");
        dumpFunc(0x00524240L, "boss03 atk5 FUN_00524240");
        dumpFunc(0x00524930L, "boss03 atk6 FUN_00524930");
        dumpFunc(0x00524f10L, "boss03 atk7 FUN_00524f10");
        dumpFunc(0x00525940L, "boss03 finisher FUN_00525940");
        dumpFunc(0x00526510L, "boss03 atkC FUN_00526510");
        dumpFunc(0x00525300L, "boss03 grab FUN_00525300");
        close();
    }
}
