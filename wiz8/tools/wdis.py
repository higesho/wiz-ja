import sys, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r'L:\SteamLibrary\steamapps\common\Wizardry8\Wiz8.exe'
d = open(EXE, 'rb').read()
IB = 0x400000
# .text: va 0x1000 raw 0x1000 ; .rdata va 0x1eb000 raw 0x1eb000 ; .data va 0x1ff000 raw 0x1ff000 -> raw == rva for these
def va2off(va):
    return va - IB

def dis(va, n=60, stop_ret=False):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    o = va2off(va)
    out = []
    for i in md.disasm(d[o:o + n * 8], va):
        out.append('%08x: %-24s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
        n -= 1
        if n <= 0 or (stop_ret and i.mnemonic == 'ret'):
            break
    return '\n'.join(out)

def find_calls(target):
    """find E8 rel32 calls to target va"""
    res = []
    tgt = target
    for o in range(0x1000, 0x1000 + 0x1ea000 - 5):
        if d[o] == 0xE8:
            rel = struct.unpack_from('<i', d, o + 1)[0]
            if o + IB + 5 + rel == tgt:
                res.append(o + IB)
    return res

def find_refs(val):
    b = struct.pack('<I', val)
    res = []
    i = d.find(b)
    while i != -1:
        res.append(i + IB)
        i = d.find(b, i + 1)
    return res

if __name__ == '__main__':
    va = int(sys.argv[1], 16)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    print(dis(va, n))
