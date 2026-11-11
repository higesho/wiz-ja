# -*- coding: utf-8 -*-
"""SCENARIO.DBS name fields (items / monsters / NPCs).

Keys: I<n>       item n, name (16-byte field at record offset 0)
      M<n>.<k>   monster n, name field k (0..3, 16 bytes each at offset 16*k)
      N<n>       NPC n, name (16 bytes at offset 0)
python tools/scentool.py export   -> trans/scen_en.txt
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))

TABLES = [  # (prefix, start, record size, count, name fields)
    ('I', 0x380, 74, 500, 1),
    ('M', 0x154e8, 222, 250, 4),
    ('N', 0x22db4, 142, 32, 1),
]
FIELD = 16


def fields(d):
    for pre, start, size, count, nf in TABLES:
        for n in range(count):
            for k in range(nf):
                off = start + n * size + k * FIELD
                raw = d[off:off + FIELD].split(b'\0')[0]
                key = '%s%d' % (pre, n) if nf == 1 else '%s%d.%d' % (pre, n, k)
                yield key, off, raw


def export():
    d = open(os.path.join(ROOT, 'original', 'SCENARIO.DBS'), 'rb').read()
    with open(os.path.join(ROOT, 'trans', 'scen_en.txt'), 'w', encoding='utf-8') as f:
        for key, off, raw in fields(d):
            if raw and all(32 <= c < 127 for c in raw):
                f.write('%s\t%s\n' % (key, raw.decode('ascii')))


def load_ja():
    p = os.path.join(ROOT, 'trans', 'scen_ja.txt')
    res = {}
    if os.path.exists(p):
        for ln in open(p, encoding='utf-8'):
            ln = ln.rstrip('\n')
            if not ln or ln.startswith('#') or '\t' not in ln:
                continue
            k, t = ln.split('\t', 1)
            res[k] = t
    return res


def apply(d, encode_small):
    """d: bytearray of SCENARIO.DBS; encode_small(text)->bytes. Returns count."""
    ja = load_ja()
    n = 0
    for key, off, raw in list(fields(bytes(d))):
        if key in ja:
            b = encode_small(ja[key])
            assert len(b) <= FIELD - 1, (key, len(b))
            d[off:off + FIELD] = b + bytes(FIELD - len(b))
            n += 1
    return n


if __name__ == '__main__':
    if sys.argv[1:] == ['export']:
        export()
        print(sum(1 for _ in open(os.path.join(ROOT, 'trans', 'scen_en.txt'), encoding='utf-8')), 'names')
