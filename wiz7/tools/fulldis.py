import wdis
out=open('../wizard_full.asm','w')
va=wdis.TEXT[0]
code=wdis.rd(va,wdis.TEXT[1]-va)
off=0
while off<len(code):
    got=False
    for ins in wdis.md.disasm(code[off:],va+off):
        out.write(f'{ins.address:08x}  {ins.mnemonic} {ins.op_str}\n'); off=ins.address+ins.size-va; got=True
    if off<len(code): out.write(f'{va+off:08x}  db {code[off]:02x}\n'); off+=1
