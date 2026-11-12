import struct, re, sys

def scan(path, K=0x9696, minlen=12):
    d = open(path, 'rb').read()
    res = []
    for par in (0, 1):
        ws = struct.unpack_from('<%dH' % ((len(d) - par) // 2), d, par)
        s = ''.join(chr((K - w) & 0xffff) if 32 <= ((K - w) & 0xffff) < 127 else '\x01' for w in ws)
        for m in re.finditer(r'[ -~]{%d,}' % minlen, s):
            res.append((par + m.start() * 2, m.group()))
    return res

def scan_plain(path, minlen=12):
    d = open(path, 'rb').read()
    a = [(m.start(), m.group().decode()) for m in re.finditer(rb'[ -~]{%d,}' % minlen, d)]
    u = [(m.start(), m.group().decode('utf-16le')) for m in re.finditer(rb'(?:[ -~]\x00){%d,}' % minlen, d)]
    return a, u

if __name__ == '__main__':
    for p in sys.argv[1:]:
        r = scan(p)
        a, u = scan_plain(p)
        print('==', p, 'obf:', len(r), 'ascii:', len(a), 'utf16:', len(u))
        for x in (r[:4] + a[:4] + u[:4]):
            print('   ', x[0], repr(x[1][:150]))
