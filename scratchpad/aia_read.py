import struct, sys

def load(path):
    d = open(path,'rb').read()
    assert d[:4]==b'AIA\0', path
    ver = struct.unpack_from('<H', d, 4)[0]
    nPal, nImg, nReg = struct.unpack_from('<III', d, 8)
    cw, ch = struct.unpack_from('<HH', d, 0x14)
    imgtab = 0x28
    regtab = imgtab + nImg*16
    images=[]
    for i in range(nImg):
        tag, first, ndir, nframe = struct.unpack_from('<IIII', d, imgtab+i*16)
        images.append((tag,first,ndir,nframe))
    def region(r):
        o = regtab + r*32
        imageOfs, = struct.unpack_from('<I', d, o)
        pal, = struct.unpack_from('<H', d, o+4)
        flags, = struct.unpack_from('<H', d, o+6)
        bill = struct.unpack_from('<4f', d, o+8)
        x0,y0,x1,y1 = struct.unpack_from('<4H', d, o+0x18)
        return dict(imageOfs=imageOfs,pal=pal,flags=flags,bill=bill,x0=x0,y0=y0,x1=x1,y1=y1)
    return dict(ver=ver,nPal=nPal,nImg=nImg,nReg=nReg,cw=cw,ch=ch,images=images,region=region)

def frame_region(a, image, d, frame):
    tag,first,ndir,nframe = a['images'][image]
    return first + (nframe*d + frame)*2

for path in sys.argv[1:]:
    a = load(path)
    print(f"\n=== {path}  canvas={a['cw']}x{a['ch']}  nImg={a['nImg']} nReg={a['nReg']} ===")
    # bill of region 0
    r0 = a['region'](0)
    print(f"  bill[region0] = {r0['bill']}   (ox={r0['bill'][1]}, oy(footline)={r0['bill'][2]})")
    print(f"  {'img':>3} {'tag':>5} {'first':>6} {'ndir':>4} {'nfr':>4}   dir0-frame0 bbox (x0,y0,x1,y1)  botY=oy-(y0+h)")
    oy = r0['bill'][2]
    for i,(tag,first,ndir,nframe) in enumerate(a['images']):
        try:
            reg = frame_region(a, i, 0, 0)
            R = a['region'](reg)
            h = R['y1']-R['y0']
            boty = oy - R['y1']   # gap between sprite bottom and foot line (canvas px)
            print(f"  {i:>3} {tag:>5} {first:>6} {ndir:>4} {nframe:>4}   ({R['x0']},{R['y0']},{R['x1']},{R['y1']})  h={h}  bottomGap={boty:.0f}px")
        except Exception as e:
            print(f"  {i:>3} {tag:>5} {first:>6} {ndir:>4} {nframe:>4}   ERR {e}")
