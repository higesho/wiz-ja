"""Monster names stored as "<name>#<note>" in MONSTERS.DBS (group dbs_monsters_hash).

  python monster_hash.py prep    -> work/chunks/dbs_monsters_hash.json, work/hash67/in_<N>.json (names still to translate),
                                    work/hash67/reference.tsv (Wiz6 monster names)
  python monster_hash.py apply   -> work/ja/dbs_monsters_hash.json (automatic + work/hash67/out_<N>.json)

Automatic translations: exact match in work/names.json, otherwise the translated plural/singular of the same record.
"""
import os, re, sys, json, glob, struct

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
W = os.path.join(ROOT, 'work')
D = os.path.join(W, 'hash67')
STRIDE, BASE = 663, 4
W6 = r'L:\SteamLibrary\steamapps\common\Wizardry6\JPN_PATCH\trans'


def records():
    """record index -> {field offset: (id, en)} for every monster name field, plus current ja by id."""
    ja = {}
    en = {}
    for cp in glob.glob(os.path.join(W, 'chunks', 'dbs_monsters*.json')):
        if 'hash' in cp:
            continue
        c = json.load(open(cp, encoding='utf-8'))
        t = json.load(open(cp.replace('chunks', 'ja'), encoding='utf-8'))
        for l in c['lines']:
            ja[l['id']] = t.get(l['id'])
            en[l['id']] = l['en']
    hashes = json.load(open(os.path.join(W, 'text', 'dbs_monsters_hash.json'), encoding='utf-8'))
    for e in hashes:
        en[e['id']] = e['en']
    rec = {}
    for i, e in en.items():
        o = int(i.split('@')[1], 16)
        rec.setdefault((o - BASE) // STRIDE, {})[(o - BASE) % STRIDE] = i
    return rec, en, ja, hashes


def auto(rec, en, ja, hashes):
    names = json.load(open(os.path.join(W, 'names.json'), encoding='utf-8'))
    hid = {e['id'] for e in hashes}
    out = {}
    for r, f in rec.items():
        for k in (0, 48):
            i = f.get(k)
            if i not in hid:
                continue
            b = en[i]
            other = f.get(48 if k == 0 else 0)
            if b in names:
                out[i] = names[b]
            elif other and other not in hid and ja.get(other):
                out[i] = ja[other]
    return out


def prep():
    os.makedirs(D, exist_ok=True)
    rec, en, ja, hashes = records()
    json.dump({'group': 'dbs_monsters_hash', 'who': 'モンスター名(単数形・複数形)', 'voice': '', 'terms': [],
               'lines': [{'id': e['id'], 'en': e['en']} for e in hashes]},
              open(os.path.join(W, 'chunks', 'dbs_monsters_hash.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    done = auto(rec, en, ja, hashes)
    hid = {e['id'] for e in hashes}
    todo = {}
    for r, f in sorted(rec.items()):
        sing, plur = f.get(0), f.get(48)
        typ = ja.get(f.get(96)) or (en.get(f.get(96)) or '')
        for i, other in ((sing, plur), (plur, sing)):
            if i in hid and i not in done:
                b = en[i]
                item = todo.setdefault(b, {'en': b, 'pair': en.get(other, ''), 'pair_ja': (done.get(other) or ja.get(other) or ''), 'type': typ})
    todo = sorted(todo.values(), key=lambda x: (x['pair'] or x['en']).lower())
    n = 2
    size = (len(todo) + n - 1) // n
    for k in range(n):
        json.dump({'lines': todo[k * size:(k + 1) * size]}, open(os.path.join(D, 'in_%d.json' % (k + 1)), 'w', encoding='utf-8'),
                  ensure_ascii=False, indent=1)
    en6 = {}
    for l in open(os.path.join(W6, 'scen_en.txt'), encoding='utf-8'):
        p = l.rstrip('\n').split('\t')
        if len(p) >= 2 and p[0].startswith('M'):
            en6[p[0]] = p[1]
    with open(os.path.join(D, 'reference.tsv'), 'w', encoding='utf-8') as f:
        f.write('en\tja\n')
        for l in open(os.path.join(W6, 'scen_ja.txt'), encoding='utf-8'):
            p = l.rstrip('\n').split('\t')
            if len(p) >= 2 and p[0] in en6:
                f.write(en6[p[0]] + '\t' + p[1] + '\n')
    print(len(hashes), 'hash names;', len(done), 'automatic;', len(todo), 'unique names to translate')


def apply():
    rec, en, ja, hashes = records()
    done = auto(rec, en, ja, hashes)
    tr = {}
    for p in glob.glob(os.path.join(D, 'out_*.json')):
        for k, v in json.load(open(p, encoding='utf-8')).items():
            if not k.startswith('_') and isinstance(v, str) and v.strip():
                tr[k] = v.strip()
    out, miss = {}, []
    for e in hashes:
        v = done.get(e['id']) or tr.get(e['en'])
        if v:
            out[e['id']] = v
        else:
            miss.append(e['en'])
    json.dump(out, open(os.path.join(W, 'ja', 'dbs_monsters_hash.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(len(out), 'translated;', len(miss), 'missing', sorted(set(miss))[:20])


def check(n):
    inp = [l['en'] for l in json.load(open(os.path.join(D, 'in_%s.json' % n), encoding='utf-8'))['lines']]
    out = json.load(open(os.path.join(D, 'out_%s.json' % n), encoding='utf-8'))
    bad = 0
    for k in inp:
        v = out.get(k)
        why = None
        if not isinstance(v, str) or not v.strip():
            why = 'missing'
        elif len(v) > 12:
            why = 'too long (%d > 12)' % len(v)
        elif re.search(r'[A-Za-z]{2}', v):
            why = 'English left'
        if why:
            bad += 1
            print('ERROR', k, '->', v, ':', why)
    for k in out:
        if not k.startswith('_') and k not in inp:
            bad += 1
            print('ERROR unknown key', k)
    print('out_%s: %d names, %d errors' % (n, len(inp), bad))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    if sys.argv[1] == 'check':
        check(sys.argv[2])
    else:
        {'prep': prep, 'apply': apply}[sys.argv[1]]()
