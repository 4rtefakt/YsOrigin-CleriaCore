#!/usr/bin/env python3
"""
aia_decode.py - Decoder for Ys Origin AIA sprite files (gfx/ani_dat.cpp format).

Reverse-engineered from yso_win.exe:
  - Loader  FUN_00444a50  (validates version 0x150, lays out tables)
  - Decode  FUN_00444fe0 -> FUN_00445380 (16bpp/4444 path) / FUN_00445220 (32bpp path)

Container layout (rvAMin/version u16 @ +4 == 0x0150):
  +0x00  u32  magic "AIA\0"  (0x00414941)
  +0x04  u16  version (0x0150)   +0x06 u16 pad
  +0x08  u32  numPalettes
  +0x0C  u32  numImages
  +0x10  u32  numRegions
  +0x14  u32  canvas (u16 w, u16 h)   e.g. 0x00800100 -> w=256, h=128
  ... more header dwords ...
  +0x28        image table   : numImages  * 16 bytes
  +...         region table  : numRegions * 32 bytes
  +...         palette block : numPalettes * 256 * 4 bytes  (B8 G8 R8 x8 ; loader forces A=0xFF)
  +...         pixel data    : RLE-compressed frames

Image-table entry (16 bytes): u32 animTag, u32 firstRegion, u32 nDir, u32 nFrame
Region record (32 bytes):
  +0x00 u32 imageOfs   (byte offset into pixel data; frames come in pairs body/2nd-layer)
  +0x04 u16 palIndex   (which 256-color sub-palette)
  +0x06 u8  flags      (bit7: no second layer)   +0x07 u8
  +0x08 f32[4]         billboard placement (scale, ox, oy, oz)
  +0x18 u16 x0  +0x1A u16 y0  +0x1C u16 x1  +0x1E u16 y1   (tight atlas bbox)

RLE stream (per row, MSB-first control word W16 = (b0<<8)|b1):
  b0 & 0x80 : LITERAL  n=(W16&0xFFF) opaque pixels; n index bytes follow
  b0 & 0x40 : ALPHA    n=(W16&0xFFF) pixels; n (alpha,index) byte pairs follow
  else      : SKIP     n=W16 transparent pixels (no data bytes)
  Rows are filled left->right; a new row starts when x reaches the width.
"""
import struct, sys, os

def u16(d,o): return struct.unpack_from('<H',d,o)[0]
def u32(d,o): return struct.unpack_from('<I',d,o)[0]

class Aia:
    def __init__(self, path):
        self.d = open(path,'rb').read()
        d=self.d
        assert d[:4]==b'AIA\x00', "bad magic"
        self.version = u16(d,4)
        assert self.version==0x150, "unsupported version %x"%self.version
        self.nPal = u32(d,8); self.nImg=u32(d,12); self.nReg=u32(d,16)
        self.canvasW = u16(d,0x14); self.canvasH = u16(d,0x16)
        self.imgtab = 0x28
        self.regtab = self.imgtab + self.nImg*16
        self.palbase= self.regtab + self.nReg*32
        self.pxbase = self.palbase + self.nPal*256*4

    def image(self,i):
        o=self.imgtab+i*16
        return dict(tag=u32(self.d,o),first=u32(self.d,o+4),nDir=u32(self.d,o+8),nFrame=u32(self.d,o+12))

    def region(self,r):
        d=self.d; o=self.regtab+r*32
        return dict(imageOfs=u32(d,o),palIndex=u16(d,o+4),flags=d[o+6],
                    bbox=(u16(d,o+0x18),u16(d,o+0x1a),u16(d,o+0x1c),u16(d,o+0x1e)))

    def palette(self,idx):
        d=self.d; base=self.palbase+idx*1024
        # stored B,G,R,pad ; alpha forced to 255 by loader
        return [(d[base+i*4+2],d[base+i*4+1],d[base+i*4+0]) for i in range(256)]

    def frame_len(self,r):
        """compressed length of region r's blob (distance to next distinct imageOfs)."""
        offs=sorted(set(self.region(k)['imageOfs'] for k in range(self.nReg)))
        io=self.region(r)['imageOfs']
        i=offs.index(io)
        end = offs[i+1] if i+1<len(offs) else (len(self.d)-self.pxbase)
        return end-io

    def decode_region(self, r, W=None, H=None):
        """Decode region r to an RGBA bytearray of size W*H*4. Returns (W,H,rgba,consumed)."""
        d=self.d
        reg=self.region(r)
        pal=self.palette(reg['palIndex'])
        x0,y0,x1,y1=reg['bbox']
        if W is None: W=x1-x0   # texture is sized to the tight bbox
        if H is None: H=y1-y0
        src=self.pxbase+reg['imageOfs']
        blen=self.frame_len(r)
        end=src+blen
        out=bytearray(W*H*4)   # zero = transparent
        x=0; y=0
        pos=y*W+x
        while y<H and src<end-1:
            b0=d[src]; b1=d[src+1]; word=(b0<<8)|b1; src+=2
            if b0 & 0x80:              # LITERAL opaque
                n=word&0xfff
                for _ in range(n):
                    idx=d[src]; src+=1
                    r_,g_,b_=pal[idx]; p=(y*W+x)*4
                    out[p]=r_; out[p+1]=g_; out[p+2]=b_; out[p+3]=255
                    x+=1
                    if x>=W: x=0; y+=1
            elif b0 & 0x40:            # ALPHA blended
                n=word&0xfff
                for _ in range(n):
                    a=d[src]; idx=d[src+1]; src+=2
                    r_,g_,b_=pal[idx]; p=(y*W+x)*4
                    # 32-bit path FUN_00445220: dst = (pal[idx]&0xFFFFFF) | (alpha<<24)
                    out[p]=r_; out[p+1]=g_; out[p+2]=b_; out[p+3]=a
                    x+=1
                    if x>=W: x=0; y+=1
            else:                      # SKIP transparent
                n=word
                for _ in range(n):
                    x+=1
                    if x>=W: x=0; y+=1
        return W,H,out,src-(self.pxbase+reg['imageOfs'])

def save_png(W,H,rgba,path):
    from PIL import Image
    img=Image.frombytes('RGBA',(W,H),bytes(rgba))
    img.save(path)

if __name__=='__main__':
    path=sys.argv[1] if len(sys.argv)>1 else r'C:\Users\kores\repos\YsOrigin-CleriaCore\assets\ANI\C_101.AIA'
    region=int(sys.argv[2]) if len(sys.argv)>2 else 0
    out=sys.argv[3] if len(sys.argv)>3 else r'C:\Users\kores\repos\YsOrigin-CleriaCore\build\aia_test.png'
    a=Aia(path)
    print("file=%s ver=%x nPal=%d nImg=%d nReg=%d canvas=%dx%d"%(
        os.path.basename(path),a.version,a.nPal,a.nImg,a.nReg,a.canvasW,a.canvasH))
    print("imgtab=%#x regtab=%#x palbase=%#x pxbase=%#x"%(a.imgtab,a.regtab,a.palbase,a.pxbase))
    reg=a.region(region)
    print("region[%d]: %s framelen=%d"%(region,reg,a.frame_len(region)))
    W,H,rgba,consumed=a.decode_region(region)
    print("decoded %dx%d, consumed %d bytes"%(W,H,consumed))
    os.makedirs(os.path.dirname(out),exist_ok=True)
    save_png(W,H,rgba,out)
    # opaque-ish pixel count for sanity
    nz=sum(1 for i in range(W*H) if rgba[i*4+3]>0)
    print("non-transparent pixels: %d  -> %s"%(nz,out))
