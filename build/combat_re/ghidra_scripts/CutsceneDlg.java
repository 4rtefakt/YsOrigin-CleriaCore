//@category YsOrigin
import ghidra.program.model.address.*;
public class CutsceneDlg extends Re2Lib {
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        // Box texture handle + box vertex-geometry block: find writers
        xrefs(0x00749d5cL, "go_wndtext BOX TEXTURE handle DAT_00749d5c (find loader/writer)");
        xrefs(0x00732af0L, "go_wndtext BOX VERTEX block DAT_00732af0 (find setup/writer)");
        // Window_Msg allocator + face/voice; text submit
        dumpFunc(0x00573e60L, "Window_Msg 0xd3 FUN_00573e60 (alloc GoWndText, speaker, face)");
        dumpFunc(0x0057b030L, "text submit FUN_0057b030 (str1,str2 -> box)");
        dumpFunc(0x00574f40L, "Portrait_Load 0xae FUN_00574f40");
        dumpFunc(0x0056fd20L, "Caption 0xe2 FUN_0056fd20 (map_name image)");
        dumpFunc(0x005741b0L, "Wait_Prompt predicate FUN_005741b0");
        dumpFunc(0x00574f50L, "Portrait_Open 0xe3 FUN_00574f50");
        // string xrefs to confirm which DDS the box uses
        xrefs(0x006d456cL, "\"data\\image\\dialog0.dds\" string (find loader)");
        close();
    }
}
