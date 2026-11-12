"""Summarise wiz8jp.dat: per kind of file, how much of the original is reused (copy) and how much
is new data (insert). The original bytes that are not copied are what the translation replaced."""
import os, sys, struct, collections

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'


def read(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'W8JP'
    n = struct.unpack_from('<I', d, 8)[0]
    p = 12
    for _ in range(n):
        L = struct.unpack_from('<H', d, p)[0]; p += 2
        name = d[p:p + L].decode('latin1'); p += L
        osz, crc, nsz, nops = struct.unpack_from('<IIII', d, p); p += 16
        ops = []
        for _ in range(nops):
            t = d[p]; p += 1
            if t == 0:
                o, l = struct.unpack_from('<II', d, p); p += 8; ops.append((0, o, l))
            else:
                l = struct.unpack_from('<I', d, p)[0]; p += 4; ops.append((1, d[p:p + l])); p += l
        yield name, osz, crc, nsz, ops


def main():
    cat = collections.defaultdict(lambda: [0, 0, 0, 0])
    rows = []
    for name, osz, crc, nsz, ops in read(os.path.join(GAME, 'wiz8jp.dat')):
        cp = sum(o[2] for o in ops if o[0] == 0)
        ins = sum(len(o[1]) for o in ops if o[0] == 1)
        k = 'fonts(sti)' if name.endswith('.sti') else name.split(chr(92))[0]
        c = cat[k]; c[0] += 1; c[1] += osz; c[2] += cp; c[3] += ins
        rows.append((osz - cp, name, osz, cp, ins))
    for k, v in sorted(cat.items()):
        print('%-14s files=%3d orig=%9d copied=%9d (%3.0f%%) insert=%9d' % (k, v[0], v[1], v[2], 100 * v[2] / max(1, v[1]), v[3]))
    rows.sort(reverse=True)
    print('largest parts of an original that are not reused:')
    for r in rows[:int(sys.argv[1]) if len(sys.argv) > 1 else 8]:
        print('  %8d  %-40s orig %8d copied %8d insert %8d' % r)


if __name__ == '__main__':
    main()
