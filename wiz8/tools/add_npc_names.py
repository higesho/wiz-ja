"""Add the NPC.DBS conversation names that the first extraction missed (see textdb.scan_npc_headers)
to work/text/dbs_npc.json, work/chunks/dbs_npc.json and work/ja/dbs_npc.json, without re-running the
whole extraction. Translations come from the same English name elsewhere in the DBS translations, then
from the table below (glossary.tsv). Run once; re-running only adds what is still missing."""
import os, sys, json, glob
sys.path.insert(0, os.path.dirname(__file__))
from textdb import scan_fields, scan_npc_headers

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
W = os.path.join(ROOT, 'work')
REL = 'DATABASES/NPC.DBS'

# glossary.tsv / NSF usage; "Pvt./Sgt. X" -> X + rank, like グルンフ軍曹, パンラック二等兵
MANUAL = {
    'Anna': 'アンナ', 'Antone': 'アントン', 'Braffit': 'ブラフィット', 'Callisto': 'カリスト',
    'Orbittracker': 'オービットトラッカー', 'EWAXX': 'EWAXX', 'Sadok': 'サドック', 'Nargisst': 'ナーギスト',
    'Pvt. Panrack': 'パンラック二等兵', 'Pvt. Sparkle': 'スパークル二等兵', 'Ferro': 'フェロ',
    'The Rapax King': 'ラパックス王', 'The Rapax Queen': 'ラパックス王妃',
    'Milano Calzone': 'ミラノ・カルツォーネ', 'Rattus Rattus': 'ラッタス・ラッタス',
}


def main():
    p = os.path.join(W, 'raw', 'patch001', 'DATABASES', 'NPC.DBS')
    if not os.path.exists(p):
        p = os.path.join(W, 'raw', 'DATABASES', 'NPC.DBS')
    raw = open(p, 'rb').read()
    known = {o for o, _ in scan_fields(raw)}
    new = scan_npc_headers(raw, known)

    tp = os.path.join(W, 'text', 'dbs_npc.json')
    text = json.load(open(tp, encoding='utf-8'))
    have = {e['id'] for e in text}
    cp = os.path.join(W, 'chunks', 'dbs_npc.json')
    chunk = json.load(open(cp, encoding='utf-8'))
    jp = os.path.join(W, 'ja', 'dbs_npc.json')
    ja = json.load(open(jp, encoding='utf-8'))

    alls = {}
    for f in glob.glob(os.path.join(W, 'ja', '*.json')):
        if not os.path.basename(f).startswith('_'):
            try:
                alls.update(json.load(open(f, encoding='utf-8')))
            except Exception:
                pass
    en2ja = {}
    for g in glob.glob(os.path.join(W, 'text', 'dbs_*.json')):
        for e in json.load(open(g, encoding='utf-8')):
            v = alls.get(e['id'])
            if isinstance(v, str):
                en2ja.setdefault(e['en'], v)

    added, missing = 0, []
    for o, s in new:
        k = '%s@%x' % (REL, o)
        if k in have:
            continue
        v = MANUAL.get(s) or en2ja.get(s)
        if not v:
            missing.append(s)
            continue
        if len(v) > len(s):
            print('too long for the field, kept English:', s, v)
            continue
        text.append({'id': k, 'en': s, 'ctx': 'NPC name'})
        chunk['lines'].append({'id': k, 'en': s})
        ja[k] = v
        added += 1
        print('  %-18s -> %s' % (s, v))
    json.dump(text, open(tp, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    json.dump(chunk, open(cp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    json.dump(ja, open(jp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('added', added, 'names; no translation for', missing)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
