import struct
K=0x9696
def decode(data):
    n=struct.unpack_from('<I',data,0)[0]; p=4; out=[]
    for i in range(n):
        ln=struct.unpack_from('<I',data,p)[0]; p+=4
        ws=struct.unpack_from('<%dH'%(ln//2),data,p); p+=ln
        s=''.join(chr((K-w)&0xffff) for w in ws)
        out.append(s)
    assert p==len(data),(p,len(data))
    return out
def encode(strs):
    b=bytearray(struct.pack('<I',len(strs)))
    for s in strs:
        ws=[(K-ord(c))&0xffff for c in s]
        b+=struct.pack('<I',len(ws)*2)+struct.pack('<%dH'%len(ws),*ws)
    return bytes(b)
