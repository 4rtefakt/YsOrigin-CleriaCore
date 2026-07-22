//@category YsOrigin
import ghidra.program.model.address.*;
public class TopicDump extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // The unlock/name lookup — THE KEY unknown
        dumpFunc(0x005be580L, "Topic unlock/name lookup FUN_005be580");
        dumpFunc(0x005be540L, "Topic entry-count FUN_005be540");
        // Top-level Topic dispatchers (self-gate on subId +0x130)
        dumpFunc(0x005bbae0L, "Topic category dispatcher FUN_005bbae0");
        dumpFunc(0x005bc6a0L, "Topic sub-list dispatcher FUN_005bc6a0");
        dumpFunc(0x005bf890L, "Topic Monster dispatcher FUN_005bf890");
        dumpFunc(0x005c17a0L, "Topic Manual dispatcher FUN_005c17a0");
        dumpFunc(0x005bd480L, "Topic mid update fn FUN_005bd480");
        // Builders/helpers
        dumpFunc(0x005bb810L, "Topic category builder FUN_005bb810");
        dumpFunc(0x005bcdf0L, "Topic small helper FUN_005bcdf0");
        dumpFunc(0x005bc260L, "Topic sub-list builder FUN_005bc260");
        dumpFunc(0x005c0f50L, "Topic Manual builder FUN_005c0f50");
        dumpFunc(0x005c1600L, "Topic Manual helper FUN_005c1600");
        close();
    }
}
