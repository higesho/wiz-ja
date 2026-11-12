"""Rename Japanese terms across all translations (and the glossary).

  python rename_terms.py <map.tsv> [--apply]

map.tsv columns: old_ja, new_ja, en_regex, scope   (see build_map67.py)
  all   : every line
  keep  : protects old_ja from shorter rules
  word  : lines whose English matches en_regex (re.search)
  exact : lines whose stripped English fully matches en_regex
  item  : like exact for short lines (DB names / UI labels), like word for prose
Longer old terms win; a replaced part is never re-replaced by another rule.
Without --apply: prints counts and samples. With --apply: rewrites work/ja/*.json
(snapshot in work/snapshots/before_rename_<time>/) and translation/glossary.tsv.
"""
import os, re, sys, json, glob, time, shutil

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
W = os.path.join(ROOT, 'work')


def load_map(p):
    m = []
    for l in open(p, encoding='utf-8'):
        l = l.rstrip('\n')
        if not l.strip() or l.startswith('#'):
            continue
        a = (l.split('\t') + ['', ''])[:4]
        old, new, rx, scope = a[0], a[1], a[2], a[3] or 'all'
        if not old:
            continue
        m.append(dict(old=old, new=new, rx=re.compile(rx) if rx else None, scope=scope, src=l))
    pri = {'keep': 0, 'exact': 1, 'item': 2, 'word': 3, 'all': 4}
    m.sort(key=lambda r: (-len(r['old']), pri.get(r['scope'], 5)))
    return m


def applies(r, en, short):
    if r['scope'] in ('all', 'keep') or r['rx'] is None:
        return True
    e = en.strip()
    if r['scope'] == 'exact' or (r['scope'] == 'item' and short):
        return r['rx'].fullmatch(e) is not None
    return r['rx'].search(e) is not None


def rename(s, en, m, short, hits=None):
    marks = []
    for i, r in enumerate(m):
        if r['old'] in s and applies(r, en, short):
            tok = '\uE000%d\uE001' % i
            if hits is not None and r['scope'] != 'keep':
                hits.append(i)
            s = s.replace(r['old'], tok)
            marks.append((tok, r['new']))
    for tok, b in marks:
        s = s.replace(tok, b)
    return s


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    m = load_map(sys.argv[1])
    apply = '--apply' in sys.argv
    counts = [0] * len(m)
    sample = {}
    files = sorted(p for p in glob.glob(os.path.join(W, 'ja', '*.json')) if not os.path.basename(p).startswith('_'))
    if apply:
        snap = os.path.join(W, 'snapshots', 'before_rename_' + time.strftime('%Y%m%d_%H%M%S'))
        os.makedirs(snap)
        for f in files:
            shutil.copy(f, snap)
        shutil.copy(os.path.join(ROOT, 'translation', 'glossary.tsv'), snap)
    changed_lines = 0
    for f in files:
        ch = json.load(open(os.path.join(W, 'chunks', os.path.basename(f)), encoding='utf-8'))
        en = {l['id']: l['en'] for l in ch['lines']}
        t = json.load(open(f, encoding='utf-8'))
        dirty = False
        for k, v in t.items():
            if not isinstance(v, str) or k not in en:
                continue
            short = k.startswith('DATABASES/') and 'DESC' not in k or (k.startswith('STRINGDATA') and len(en[k]) <= 30)
            hits = []
            nv = rename(v, en[k], m, short, hits)
            for i in hits:
                counts[i] += 1
                sample.setdefault(i, []).append('%s | %s -> %s' % (en[k][:50].replace('\n', ' '), v[:40].replace('\n', ' '), nv[:40].replace('\n', ' ')))
            if nv != v:
                t[k] = nv; dirty = True; changed_lines += 1
        if apply and dirty:
            json.dump(t, open(f, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    # glossary: match its own English column
    gp = os.path.join(ROOT, 'translation', 'glossary.tsv')
    rows = open(gp, encoding='utf-8').read().splitlines()
    out = [rows[0]]
    for r in rows[1:]:
        c = r.split('\t')
        if len(c) > 1:
            c[1] = rename(c[1], c[0], m, True)
        out.append('\t'.join(c))
    if apply:
        open(gp, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    verbose = '-v' in sys.argv
    for i, r in enumerate(m):
        if r['scope'] == 'keep':
            continue
        s = sample.get(i, [])
        print('%5d  %s -> %s  [%s %s]' % (counts[i], r['old'], r['new'], r['scope'], r['rx'].pattern if r['rx'] else ''))
        for x in s[:(5 if verbose else 1)]:
            print('         ', x)
    print(changed_lines, 'lines', 'changed' if apply else 'would change')


if __name__ == '__main__':
    main()
