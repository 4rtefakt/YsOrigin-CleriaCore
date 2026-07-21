// FuncDump: emit EVERY function in yso_win.exe as `addr,size,name,thunk,external`
// to D:\ghidra-work\all_functions.txt. Feeds tools/re_coverage.py (the RE-coverage
// metric): address + byte-size lets coverage be weighted by code volume, and the
// name/thunk/external columns let library/CRT boilerplate be split out from game code.
//@category YsOrigin
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class FuncDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(new FileWriter("D:\\ghidra-work\\all_functions.txt"));
        out.println("# addr,size_bytes,name,is_thunk,is_external");
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        long bytes = 0;
        for (Function f : fm.getFunctions(true)) {
            long sz = f.getBody().getNumAddresses();   // byte count across the function body
            out.printf("%s,%d,%s,%b,%b%n",
                       f.getEntryPoint().toString(), sz, f.getName(),
                       f.isThunk(), f.isExternal());
            ++n;
            bytes += sz;
        }
        out.close();
        println("FuncDump: " + n + " functions, " + bytes + " code bytes -> all_functions.txt");
    }
}
