"""Report glossary violations across all translated chunks.

For every line whose English contains a glossary/name term, check that the Japanese contains the
expected translation. Prints terms sorted by number of violations with the variants actually used.
usage: python consistency.py [min_violations]
"""
import os, re, sys, json, glob
from collections import Counter, defaultdict

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
sys.path.insert(0, os.path.dirname(__file__))
from make_chunks import load_glossary

SKIP_CATS = {'能力値', '呪文系統', '状態', 'UI', '技能', '略称', '性格', '呼称', '通貨'}


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    minv = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    cats = {}
    for line in open(os.path.join(ROOT, 'translation', 'glossary.tsv'), encoding='utf-8').read().splitlines()[1:]:
        p = line.split('\t')
        if len(p) >= 3:
            cats[p[0]] = p[2]
    gl = {en: ja for en, (ja, _) in load_glossary().items()
          if cats.get(en) not in SKIP_CATS and en[:1].isupper() and len(en) >= 3}
    # longest terms first so "Rapax Castle" is not also counted as "Rapax"
    terms = sorted(gl, key=len, reverse=True)
    bad = defaultdict(list)
    total = Counter()
    for cp in glob.glob(os.path.join(ROOT, 'work', 'chunks', '*.json')):
        if os.path.basename(cp).startswith('_'):
            continue
        jp = os.path.join(ROOT, 'work', 'ja', os.path.basename(cp))
        if not os.path.exists(jp):
            continue
        c = json.load(open(cp, encoding='utf-8'))
        t = json.load(open(jp, encoding='utf-8'))
        for l in c['lines']:
            ja = t.get(l['id'])
            if not isinstance(ja, str):
                continue
            en = l['en']
            for term in terms:
                if term in en and re.search(r'(?<![A-Za-z])' + re.escape(term) + r'(?![a-z])', en):
                    en = en.replace(term, ' ')
                    total[term] += 1
                    want = gl[term]
                    if want not in ja and want.replace('・', '') not in ja:
                        bad[term].append((c['chunk'], l['id'], ja[:70]))
    rows = sorted(bad.items(), key=lambda kv: -len(kv[1]))
    for term, items in rows:
        if len(items) < minv:
            continue
        print('%-28s want %-18s  bad %3d / %3d' % (term, gl[term], len(items), total[term]))
        for ch, i, ja in items[:3]:
            print('      %s: %s' % (ch, ja))
    json.dump({k: v for k, v in rows}, open(os.path.join(ROOT, 'work', 'ja', '_consistency.json'), 'w',
                                            encoding='utf-8'), ensure_ascii=False, indent=1)


if __name__ == '__main__':
    main()
