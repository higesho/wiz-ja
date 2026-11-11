import sys,struct,pefile,capstone
EXE=str(__import__("pathlib").Path(__file__).resolve().parents[2] / "wizardry7gold" / "WIZARD.EXE")
pe=pefile.PE(EXE); data=pe.get_memory_mapped_image(); BASE=0x400000
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=False
TEXT=(0x401000,0x401000+0x9b110)
imports={}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports: imports[i.address]=(e.dll.decode()+'!'+(i.name.decode() if i.name else str(i.ordinal)))
def rd(va,n): return data[va-BASE:va-BASE+n]
def dis(va,n=60,stop_ret=True):
    out=[]
    for ins in md.disasm(rd(va,4000),va):
        s=f'{ins.address:08x}  {ins.mnemonic} {ins.op_str}'
        for a,nm in imports.items():
            if hex(a) in ins.op_str: s+='   ; '+nm
        out.append(s); n-=1
        if n<=0 or (stop_ret and ins.mnemonic=='ret'): break
    return out
def xrefs(val):
    b=struct.pack('<I',val); t=data[TEXT[0]-BASE:TEXT[1]-BASE]; r=[]; i=t.find(b)
    while i>=0: r.append(TEXT[0]+i); i=t.find(b,i+1)
    return r
def calls_to(target):
    t=data[TEXT[0]-BASE:TEXT[1]-BASE]; r=[]
    for i in range(len(t)-5):
        if t[i]==0xe8 and (TEXT[0]+i+5+struct.unpack('<i',t[i+1:i+5])[0])&0xffffffff==target: r.append(TEXT[0]+i)
    return r
def func_start(va):
    # scan back for push ebp; mov ebp,esp
    for a in range(va,va-0x4000,-1):
        if rd(a,3) in (b'\x55\x8b\xec',): return a
if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='x':
        for a in sys.argv[2:]:
            for r in xrefs(int(a,16)): print(a,'<-',hex(r),'func',hex(func_start(r) or 0))
    elif cmd=='d':
        print('\n'.join(dis(int(sys.argv[2],16),int(sys.argv[3]) if len(sys.argv)>3 else 80, len(sys.argv)<=4)))
    elif cmd=='c':
        for r in calls_to(int(sys.argv[2],16)): print(hex(r),'in',hex(func_start(r) or 0))
