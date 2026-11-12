"""Validate translated chunks (work/ja/*.json) against work/chunks/*.json.

usage: python check_ja.py [chunk ...]      (no args = every chunk that has a translation)
Writes work/ja/_report.json and prints a summary. Also rebuilds work/names.json from dbs_ name chunks.
"""
import os, re, sys, json, glob

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
CH = os.path.join(ROOT, 'work', 'chunks')
JA = os.path.join(ROOT, 'work', 'ja')
FMT = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l)?[sdiuxXcfgeEp%]')
NAME_LIMIT = {'dbs_items': 29, 'dbs_monsters': 23}  # UTF-16 chars, excluding the terminator
NAME_GROUPS = ('dbs_spelltables', 'dbs_levels', 'dbs_npc', 'dbs_items', 'dbs_monsters')
ALLOWED_ASCII = {'HLL', 'IUF', 'EWAXX', 'RFS', 'NAS', 'HP', 'SP', 'AC', 'ST', 'NPC', 'NPCs', 'ESC', 'TAB',
                 'ENTER', 'SPACE', 'HOME', 'INSERT', 'DEL', 'BACKSPACE', 'UP', 'DOWN', 'LEFT', 'RIGHT',
                 'PGUP', 'PGDN', 'END', 'CTRL', 'ALT', 'SHIFT', 'OK', 'God', 'Mode',
                 'MATILDA', 'HPIOU1', 'EGGLES', 'VAUGHN', 'TECH', 'www', 'com'}


def check(name):
    c = json.load(open(os.path.join(CH, name + '.json'), encoding='utf-8'))
    p = os.path.join(JA, name + '.json')
    try:
        t = json.load(open(p, encoding='utf-8'))
    except Exception as e:
        return {'chunk': name, 'fatal': 'cannot read: %s' % e}
    probs = []
    ids = [l['id'] for l in c['lines']]
    missing = [i for i in ids if i not in t or not isinstance(t[i], str)]
    extra = [k for k in t if k not in ids and not k.startswith('_')]
    for l in c['lines']:
        ja = t.get(l['id'])
        if not isinstance(ja, str):
            continue
        en = l['en']
        if FMT.findall(en) != FMT.findall(ja):
            probs.append((l['id'], 'format', FMT.findall(en), FMT.findall(ja)))
        if en.count('\n') != ja.count('\n'):
            probs.append((l['id'], 'newlines', en.count('\n'), ja.count('\n')))
        words = [w for w in re.findall(r'[A-Za-z]{3,}', FMT.sub('', ja)) if w not in ALLOWED_ASCII]
        if words and not re.search(r'[\u3040-\u30ff\u4e00-\u9fff]', ja):
            probs.append((l['id'], 'untranslated', ja[:60]))
        elif len(words) >= 3:
            probs.append((l['id'], 'english-left', ' '.join(words[:6])))
        if re.search(r'[\uff61-\uff9f]', ja):
            probs.append((l['id'], 'halfwidth-kana', ja[:40]))
        if c['group'] in NAME_GROUPS and len(ja) > NAME_LIMIT.get(c['group'], 23):
            probs.append((l['id'], 'too-long', en, ja))
    return {'chunk': name, 'lines': len(ids), 'missing': missing, 'extra': extra,
            'problems': probs, 'notes': t.get('_notes', [])}


def rebuild_names():
    names = {}
    for g in NAME_GROUPS:
        for p in sorted(glob.glob(os.path.join(CH, g + '*.json'))):
            n = os.path.basename(p)[:-5]
            jp = os.path.join(JA, n + '.json')
            if not os.path.exists(jp):
                continue
            c = json.load(open(p, encoding='utf-8'))
            t = json.load(open(jp, encoding='utf-8'))
            for l in c['lines']:
                ja = t.get(l['id'])
                if isinstance(ja, str) and len(l['en']) > 2 and l['en'] not in names:
                    names[l['en']] = ja
    tmp = os.path.join(ROOT, 'work', 'names.%d.tmp' % os.getpid())
    json.dump(names, open(tmp, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    os.replace(tmp, os.path.join(ROOT, 'work', 'names.json'))
    return len(names)


def main():
    names = sys.argv[1:] or sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(JA, '*.json'))
                                   if not os.path.basename(p).startswith('_'))
    rep = [check(n) for n in names]
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    json.dump(rep, open(os.path.join(JA, '_report.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    for r in rep:
        if 'fatal' in r:
            print('FATAL', r['chunk'], r['fatal']); continue
        print('%-28s lines %4d  missing %3d  extra %2d  problems %3d  notes %d' % (
            r['chunk'], r['lines'], len(r['missing']), len(r['extra']), len(r['problems']), len(r['notes'])))
        for i in r['missing'][:30]:
            print('   missing id:', i)
        for pr in r['problems'][:30]:
            print('   problem:', json.dumps(pr, ensure_ascii=False)[:200])
    print('names.json:', rebuild_names(), 'names')


if __name__ == '__main__':
    main()
