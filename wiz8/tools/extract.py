import sys, os
from slf import read_slf

BS = chr(92)

def extract(slf, names, outdir):
    h, es = read_slf(slf)
    f = open(slf, 'rb')
    for n, o, l, s in es:
        if any(x.lower() in n.lower() for x in names):
            p = os.path.join(outdir, n.replace(BS, '/'))
            os.makedirs(os.path.dirname(p) or '.', exist_ok=True)
            f.seek(o)
            open(p, 'wb').write(f.read(l))

if __name__ == '__main__':
    extract(sys.argv[1], sys.argv[3:], sys.argv[2])
