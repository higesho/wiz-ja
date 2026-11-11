import sys,struct
from PIL import Image
G=r'L:/SteamLibrary/steamapps/common/Wizardry7/wizardry7gold/'
def load(name):
    d=open(G+name,'rb').read(); h=d[:16]
    w,hgt,typ,bpr,bpr2,cnt,flags=h[0],h[1],h[2],h[3],h[4],h[5],h[6]
    gs=struct.unpack('<h',h[10:12])[0]; gs2=struct.unpack('<h',h[14:16])[0]
    widths=[d[16+i*2+1] for i in range(128)]; codes=[d[16+i*2] for i in range(128)]
    return dict(d=d,w=w,h=hgt,typ=typ,bpr=bpr,bpr2=bpr2,cnt=cnt,flags=flags,gs=gs,gs2=gs2,widths=widths,codes=codes)
def render(name,scale=3):
    f=load(name); d=f['d']; base=0x110
    cols=16; rows=(f['cnt']+15)//16
    img=Image.new('RGB',(cols*(f['w']+2)*scale,rows*(f['h']+2)*2*scale),(40,40,80))
    for c in range(f['cnt']):
        for plane in (0,1):
            if plane==1 and not f['flags']&4: continue
            off=base+c*f['gs'] if plane==0 else base+f['cnt']*f['gs']+c*f['gs2']
            bpr=f['bpr'] if plane==0 else f['bpr2']
            for y in range(f['h']):
                for x in range(f['w']):
                    if f['typ']==0:
                        b=d[off+y*bpr+(x>>3)]; on=(b>>(7-(x&7)))&1; col=(255,255,255) if on else None
                    else:
                        v=d[off+y*f['w']+x]; col=None if v==0xff else (v,v,v)
                    if col:
                        X=(c%16)*(f['w']+2)+x; Y=(c//16)*(f['h']+2)*2+plane*(f['h']+1)+y
                        for i in range(scale):
                            for j in range(scale): img.putpixel((X*scale+i,Y*scale+j),col if plane==0 else (255,120,0))
    img.save(sys.argv[2] if len(sys.argv)>2 else name+'.png')
    print(name,{k:v for k,v in f.items() if k not in('d','codes')})
render(sys.argv[1])
