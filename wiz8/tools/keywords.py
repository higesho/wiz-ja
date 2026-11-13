"""Conversation keywords: players still type them in English, so dialogue shows them as 「訳語(KEYWORD)」.

  python keywords.py collect   -> work/kw67/in_<N>.json : keywords that occur in the English dialogue, with examples
  python keywords.py check <N> -> validate work/kw67/out_<N>.json
  python keywords.py stats     -> how many mentions get annotated / where the Japanese term was not found

work/kw67/out_<N>.json: {"KEYWORD": "訳語" | "訳語,別表記" | ""}   ("" = do not annotate)
annotate(id, en, ja) is used by build_patch.py for every NSF line.
"""
import os, re, sys, json, glob

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
W = os.path.join(ROOT, 'work')
D = os.path.join(W, 'kw67')
RAW = os.path.join(W, 'raw', 'NPC SCRIPTS')
KW_RE = re.compile(rb'(?<=[\x00-\x1f])([\x01-\x40])\x00([A-Z0-9][A-Z0-9 \'\-\.]{1,63})')


def nsf_keywords():
    """file name -> set of keywords (ASCII, u16 length prefix) defined in that NSF."""
    per = {}
    for p in glob.glob(os.path.join(RAW, '*.NSF')):
        d = open(p, 'rb').read()
        ks = set()
        for m in KW_RE.finditer(d):
            L = m.group(1)[0]
            s = m.group(2)[:L]
            if len(s) == L:
                w = s.decode('ascii')
                if re.fullmatch(r"[A-Z0-9][A-Z0-9 '\-\.]*[A-Z0-9']", w) and len(w) >= 2:
                    ks.add(w)
        per['NPC SCRIPTS/' + os.path.basename(p)] = ks
    return per


def kw_regex(k):
    return re.compile(r"(?<![A-Za-z'])" + re.escape(k).replace(r"\ ", r"\s+") + r"(?![A-Za-z]|'[A-Za-z])", re.I)


def mentions(per, lid, en):
    f = lid.split('@')[0]
    found = [k for k in per.get(f, ()) if len(k) >= 3 and kw_regex(k).search(en)]
    # drop keywords that only occur inside a longer found keyword (SAVANT inside DARK SAVANT)
    found.sort(key=lambda k: (-len(k), k))
    keep = []
    for k in found:
        spans = [m.span() for m in kw_regex(k).finditer(en)]
        covered = all(any(a >= s and b <= e for s, e in
                          [m.span() for kk in keep for m in kw_regex(kk).finditer(en)]) for a, b in spans)
        if not covered:
            keep.append(k)
    return keep


def nsf_lines():
    for cp in sorted(glob.glob(os.path.join(W, 'chunks', '*.json'))):
        b = os.path.basename(cp)
        if not (b.startswith('nsf_')):
            continue
        c = json.load(open(cp, encoding='utf-8'))
        t = json.load(open(os.path.join(W, 'ja', b), encoding='utf-8'))
        for l in c['lines']:
            if isinstance(t.get(l['id']), str):
                yield l['id'], l['en'], t[l['id']]


def collect():
    os.makedirs(D, exist_ok=True)
    per = nsf_keywords()
    names = {k.upper(): v for k, v in json.load(open(os.path.join(W, 'names.json'), encoding='utf-8')).items()}
    for l in open(os.path.join(ROOT, 'translation', 'glossary.tsv'), encoding='utf-8').read().splitlines()[1:]:
        c = l.split('\t')
        if len(c) > 1:
            names.setdefault(c[0].upper(), c[1])
    ex = {}
    for lid, en, ja in nsf_lines():
        for k in mentions(per, lid, en):
            e = ex.setdefault(k, {'keyword': k, 'count': 0, 'hint': names.get(k, ''), 'examples': []})
            e['count'] += 1
            if len(e['examples']) < 2:
                e['examples'].append({'en': en[:300], 'ja': ja[:200]})
    items = sorted(ex.values(), key=lambda x: x['keyword'])
    n = 4
    size = (len(items) + n - 1) // n
    for i in range(n):
        json.dump({'lines': items[i * size:(i + 1) * size]}, open(os.path.join(D, 'in_%d.json' % (i + 1)), 'w', encoding='utf-8'),
                  ensure_ascii=False, indent=1)
    print(len(items), 'keywords mentioned in dialogue,', sum(x['count'] for x in items), 'mentions')


def load_table():
    tab = {}
    for p in glob.glob(os.path.join(D, 'out_*.json')):
        for k, v in json.load(open(p, encoding='utf-8')).items():
            if not k.startswith('_') and isinstance(v, str):
                tab[k] = [x.strip() for x in v.split(',') if x.strip()]
    return tab


_cache = {}
KW_MARK = '\ue001'   # invisible start of a clickable "term(KEYWORD)" span (hook: log word splitting / click)


def annotate(lid, en, ja, stats=None):
    """Insert "(KEYWORD)" after the Japanese term of every annotated keyword the English line mentions,
    with KW_MARK before the term so the whole "term(KEYWORD)" is one clickable word in the log."""
    if not lid.startswith('NPC SCRIPTS/'):
        return ja
    if 'per' not in _cache:
        _cache['per'] = nsf_keywords()
        _cache['tab'] = load_table()
        mp = os.path.join(D, 'manual.json')
        _cache['man'] = json.load(open(mp, encoding='utf-8')) if os.path.exists(mp) else {}
    per, tab, man = _cache['per'], _cache['tab'], _cache['man']
    for k in mentions(per, lid, en):
        mk = lid + '|' + k
        if mk in man:       # placed by hand (tools/fix_release.py): a term in this line, or None = leave out
            if man[mk] is None:
                continue
            terms = [man[mk]]
        else:
            terms = [t for t in tab.get(k) or [] if t.upper() != k]   # already written in English
        if not terms:
            continue
        tag = '(%s)' % k
        if tag in ja:
            continue
        for t in sorted(terms, key=len, reverse=True):
            i = ja.find(t)
            while i >= 0 and ja[i + len(t):i + len(t) + 1] == '(':
                i = ja.find(t, i + 1)
            if i >= 0:
                ja = ja[:i] + KW_MARK + ja[i:i + len(t)] + tag + ja[i + len(t):]
                if stats is not None:
                    stats['ok'] = stats.get('ok', 0) + 1
                break
        else:
            if stats is not None:
                stats.setdefault('miss', []).append((k, lid, en[:80], ja[:60]))
    return ja


def check(n):
    inp = [l['keyword'] for l in json.load(open(os.path.join(D, 'in_%s.json' % n), encoding='utf-8'))['lines']]
    out = json.load(open(os.path.join(D, 'out_%s.json' % n), encoding='utf-8'))
    bad = 0
    for k in inp:
        if k not in out or not isinstance(out[k], str):
            print('ERROR missing', k); bad += 1
        elif re.search('[A-Za-z]{2}', out[k]):
            print('ERROR English in term', k, out[k]); bad += 1
    for k in out:
        if not k.startswith('_') and k not in inp:
            print('ERROR unknown key', k); bad += 1
    print('out_%s: %d keywords, %d annotated, %d errors' % (n, len(inp), sum(1 for k in inp if out.get(k)), bad))


def stats():
    st = {}
    for lid, en, ja in nsf_lines():
        annotate(lid, en, ja, st)
    miss = st.get('miss', [])
    print(st.get('ok', 0), 'annotations;', len(miss), 'mentions whose Japanese term was not found')
    from collections import Counter
    print(Counter(m[0] for m in miss).most_common(40))
    json.dump(miss, open(os.path.join(D, 'missing.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    a = sys.argv[1]
    if a == 'collect':
        collect()
    elif a == 'check':
        check(sys.argv[2])
    else:
        stats()
