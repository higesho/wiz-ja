"""Compare our glossary / DB names with the Wizardry 6 and 7 Japanese translation projects.

  python compare67.py   -> work/terms67/compare.tsv, work/terms67/compare.md

Reference sources (read only):
  Wiz6: JPN_PATCH/trans/glossary.tsv, parts/spells_ui2.tsv, parts/terms_s*.tsv,
        scen_en/scen_ja (items I*, monsters M*, NPC N*), parts/en_ui*/ja_ui* (UI messages)
  Wiz7: jpmod/translation/glossary.md (tables), msg_en/msg_ja (UI, spells, skills, items)
"""
import os, re, sys, json, glob
from collections import defaultdict

COMMON = r'L:\SteamLibrary\steamapps\common'
W6 = os.path.join(COMMON, 'Wizardry6', 'JPN_PATCH', 'trans')
W7 = os.path.join(COMMON, 'Wizardry7', 'jpmod', 'translation')
ROOT = os.path.join(COMMON, 'Wizardry8', 'JPMod')
OUT = os.path.join(ROOT, 'work', 'terms67')


def key(s):
    s = s.upper().replace('\u2019', "'").replace('&', ' AND ')
    s = re.sub(r'^[^A-Z0-9]+|[^A-Z0-9]+$', '', s)
    s = re.sub(r'[^A-Z0-9\']+', ' ', s).strip()
    return s


def clean_ja(s):
    s = re.sub(r'\\x[0-9a-fA-F]{2}|\{[0-9A-Za-z]{1,2}\}', '', s)
    s = s.strip(' _%@!^$"*(~]`{}?')
    return s


def tsv(p, skip='#'):
    for l in open(p, encoding='utf-8'):
        l = l.rstrip('\n')
        if not l or l.startswith(skip):
            continue
        yield l.split('\t')


def load_refs():
    ref = defaultdict(list)   # key -> [(src, en, ja)]

    def add(src, en, ja):
        ja = clean_ja(ja)
        k = key(en)
        if k and ja:
            if (src, ja) not in [(a, c) for a, b, c in ref[k]]:
                ref[k].append((src, en, ja))

    # ---- Wiz6
    for r in tsv(os.path.join(W6, 'glossary.tsv')):
        if len(r) >= 2:
            add('6用語', r[0], r[1])
    for r in tsv(os.path.join(W6, 'parts', 'spells_ui2.tsv')):
        if len(r) >= 2:
            add('6呪文', r[0], r[1])
    for p in sorted(glob.glob(os.path.join(W6, 'parts', 'terms_s*.tsv'))):
        for r in tsv(p):
            if len(r) >= 2:
                add('6固有', r[0], r[1])
    en = dict(r[:2] for r in tsv(os.path.join(W6, 'scen_en.txt')) if len(r) >= 2)
    for r in tsv(os.path.join(W6, 'scen_ja.txt')):
        if len(r) >= 2 and r[0] in en:
            kind = {'I': '6品', 'M': '6魔', 'N': '6人'}.get(r[0][0], '6')
            add(kind, en[r[0]], r[1])
    for p in sorted(glob.glob(os.path.join(W6, 'parts', 'en_ui*.txt'))):
        e = {r[0]: r[1] for r in tsv(p) if len(r) >= 2}
        for r in tsv(p.replace('en_ui', 'ja_ui')):
            if len(r) >= 3 and r[0] in e and len(e[r[0]]) <= 30 and '\\x' not in e[r[0]]:
                add('6UI', e[r[0]], r[2])

    # ---- Wiz7
    md = open(os.path.join(W7, 'glossary.md'), encoding='utf-8').read()
    for line in md.splitlines():
        if not line.startswith('|') or '---' in line:
            continue
        cells = [c.strip().strip('`') for c in line.strip('|').split('|')]
        pairs = [(cells[i], cells[i + 1]) for i in range(0, len(cells) - 1, 2)]
        for a, b in pairs:
            if a in ('原文', 'ID', '記号') or not re.search(r'[A-Za-z]', a):
                continue
            ens = [x.strip() for x in a.split(' / ')]
            jas = [x.strip() for x in b.split(' / ')]
            if len(ens) == len(jas):
                for x, y in zip(ens, jas):
                    add('7用語', x, y)
            else:
                for x in ens:
                    add('7用語', x, jas[0])
    # "EN 訳 / EN 訳" lists (stats, skills, statuses)
    for line in md.splitlines():
        if line.startswith('|') or ' / ' not in line:
            continue
        for item in re.split(r'\s*/\s*', line.rstrip(' /')):
            m = re.fullmatch(r'([A-Z][A-Z&\-\' ]*[A-Z])\s+(\S.*)', item.strip())
            if m and not re.search(r'[A-Za-z]{3}', m.group(2)):
                add('7用語', m.group(1).replace('&', ' & '), m.group(2).split(' ')[0])
    e7 = {}
    for r in tsv(os.path.join(W7, 'msg_en.txt')):
        if len(r) >= 2 and r[0].isdigit():
            e7[r[0]] = r[1]
    for r in tsv(os.path.join(W7, 'msg_ja.txt')):
        if len(r) >= 2 and r[0] in e7 and r[0].isdigit():
            i = int(r[0])
            src = '7呪文' if 4000 <= i < 4200 else '7品' if 6000 <= i < 6200 else '7技能' if 5500 <= i < 5600 else '7UI'
            if i < 7000 and len(e7[r[0]]) <= 30:
                add(src, e7[r[0]], r[1])
    return ref


def load_ours():
    ours = []   # (category, en, ja)
    for r in tsv(os.path.join(ROOT, 'translation', 'glossary.tsv'), skip='en\t'):
        if len(r) >= 2:
            ours.append(('用語:' + (r[2] if len(r) > 2 else ''), r[0], r[1]))
    cat = {'dbs_spelltables': '呪文', 'dbs_monsters': 'モンスター', 'dbs_items': 'アイテム',
           'dbs_npc': 'NPC', 'dbs_levels': '地名'}
    for g, c in cat.items():
        for cp in sorted(glob.glob(os.path.join(ROOT, 'work', 'chunks', g + '*.json'))):
            ch = json.load(open(cp, encoding='utf-8'))
            t = json.load(open(cp.replace('chunks', 'ja'), encoding='utf-8'))
            for l in ch['lines']:
                ours.append((c, l['en'], t.get(l['id'], '')))
    # Wiz8 UI words worth checking (STRINGDATA short labels)
    return ours


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    os.makedirs(OUT, exist_ok=True)
    ref = load_refs()
    ours = load_ours()
    rows, seen = [], set()
    for c, en, ja in ours:
        k = key(en)
        cands = ref.get(k) or ref.get(k.rstrip('S')) or ref.get(k + 'S') or []
        if not cands or (k, ja) in seen:
            continue
        seen.add((k, ja))
        w6 = ' / '.join(sorted({j for s, e, j in cands if s.startswith('6')}))
        w7 = ' / '.join(sorted({j for s, e, j in cands if s.startswith('7')}))
        srcs = ','.join(sorted({s for s, e, j in cands}))
        same = ja in {j for s, e, j in cands}
        rows.append((c, en, ja, w6, w7, srcs, 'same' if same else 'DIFF'))
    rows.sort(key=lambda r: (r[6] != 'DIFF', r[0], r[1]))
    with open(os.path.join(OUT, 'compare.tsv'), 'w', encoding='utf-8') as f:
        f.write('category\ten\tours\twiz6\twiz7\tsources\tstatus\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    nd = sum(r[6] == 'DIFF' for r in rows)
    print(len(rows), 'matched terms;', nd, 'differ;', sum(len(v) for v in ref.values()), 'reference entries')


if __name__ == '__main__':
    main()
