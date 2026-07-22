//@category YsOrigin
import ghidra.program.model.address.*;
public class SizeBand6B extends Re2Lib {
    void raw(long va){
        StringBuilder sb=new StringBuilder("BYTES @0x"+Long.toHexString(va)+" = \"");
        try{ for(int i=0;i<24;i++){ byte b=getByte(toAddr(va+i)); if(b==0) break; sb.append((char)(b&0xff)); } }catch(Exception e){}
        out.println(sb.append("\"").toString());
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        raw(0x6941c4L); raw(0x6941c8L); raw(0x6941d0L);
        close();
    }
}
