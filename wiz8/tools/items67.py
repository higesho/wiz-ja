"""Item-name restyle (Wiz6 conventions) support.

  python items67.py check <N>   validate work/items67/out_<N>.json
  python items67.py map         -> work/items67/map.tsv (rules for rename_terms.py, scope item)
"""
import os, re, sys, json, glob

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
D = os.path.join(ROOT, 'work', 'items67')
LIMIT = 29


def load(n):
    inp = {l['en']: l['ja'] for l in json.load(open(os.path.join(D, 'in_%s.json' % n), encoding='utf-8'))['lines']}
    out = json.load(open(os.path.join(D, 'out_%s.json' % n), encoding='utf-8'))
    return inp, out


def check(n):
    inp, out = load(n)
    bad = 0
    for k, v in out.items():
        if k.startswith('_'):
            continue
        why = None
        if k not in inp:
            why = 'unknown en'
        elif not isinstance(v, str) or not v.strip():
            why = 'empty'
        elif len(v) > LIMIT:
            why = 'too long (%d > %d)' % (len(v), LIMIT)
        elif re.search(r'\((U|L)\)', v):
            why = 'use （上）/（下）'
        elif re.findall(r'\+\d', k) != re.findall(r'\+\d', v):
            why = '+N bonus changed'
        if why:
            bad += 1
            print('ERROR', k, '->', v, ':', why)
    print('out_%s: %d changes, %d errors' % (n, sum(not k.startswith('_') for k in out), bad))


def make_map():
    rows = []
    for p in sorted(glob.glob(os.path.join(D, 'out_*.json'))):
        n = re.search(r'out_(\d+)', p).group(1)
        inp, out = load(n)
        for k, v in out.items():
            if k.startswith('_') or k not in inp or inp[k] == v or len(v) > LIMIT:
                continue
            rows.append((inp[k], v, r'\b' + re.escape(k) + r'(?![A-Za-z])', 'item'))
    with open(os.path.join(D, 'map.tsv'), 'w', encoding='utf-8') as f:
        f.write('# old\tnew\ten_regex\tscope\n')
        for r in sorted(set(rows)):
            f.write('\t'.join(r) + '\n')
    print(len(rows), 'rules')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    if sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        make_map()
