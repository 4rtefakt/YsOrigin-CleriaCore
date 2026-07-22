//@category YsOrigin
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class BigFuncs4 extends Re2Lib {
    long rd(long a){ try{ byte[] b=new byte[4]; currentProgram.getMemory().getBytes(toAddr(a),b);
        return ((b[0]&0xffL))|((b[1]&0xffL)<<8)|((b[2]&0xffL)<<16)|((b[3]&0xffL)<<24);}catch(Exception e){return 0;} }
    String str(long a){ try{ StringBuilder s=new StringBuilder(); for(int i=0;i<48;i++){ byte c=currentProgram.getMemory().getByte(toAddr(a+i)); if(c==0)break; s.append((char)c);} return s.toString(); }catch(Exception e){return "?";} }
    void row(long a){
        long v=rd(a);
        Function tf=currentProgram.getFunctionManager().getFunctionAt(toAddr(v));
        String extra="";
        if(v>0x400000 && v<0x700000 && tf==null){ String s=str(v); if(s.length()>0 && s.chars().allMatch(c->c>=32&&c<127)) extra="  STR=\""+s+"\""; }
        out.println("  "+toAddr(a)+" -> 0x"+Long.toHexString(v)+(tf!=null?("  ("+tf.getName()+")"):"")+extra);
    }
    public void run() throws Exception {
        open(System.getenv("NIGHT_OUT"));
        out.println("string @0x6c68fc = \""+str(0x6c68fcL)+"\"");
        out.println("\n---- wide table scan 69b980..69bb00 ----");
        for(long a=0x0069b980L;a<0x0069bb00L;a+=4) row(a);
        // find who references the string 0x6c68fc and nearby record start pointers
        out.println("\n== xrefs to name string 0x6c68fc ==");
        for(Reference r: getReferencesTo(toAddr(0x6c68fcL))) out.println("  "+r.getFromAddress()+"  ["+fnName(r.getFromAddress())+"]");
        out.println("\n== xrefs to 0x69ba38 (name slot) ==");
        for(Reference r: getReferencesTo(toAddr(0x69ba38L))) out.println("  "+r.getFromAddress()+"  ["+fnName(r.getFromAddress())+"]");
        close();
    }
}
