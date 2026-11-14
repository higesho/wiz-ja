"""Split work/text/*.json into translation chunks (work/chunks/*.json).

Each chunk carries the speaker voice (translation/voices.json) and the glossary terms
(translation/glossary.tsv + work/names.json, i.e. already translated DB names) that occur in it.
"""
import os, re, json, glob, sys

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
TEXT = os.path.join(ROOT, 'work', 'text')
CHUNKS = os.path.join(ROOT, 'work', 'chunks')
TR = os.path.join(ROOT, 'translation')
WORDS_PER_CHUNK = 2500

# order = translation priority
ORDER = ['dbs_spelltables', 'dbs_levels', 'dbs_npc', 'dbs_items', 'dbs_monsters',
         'stringdata', 'dbs_spelldesc', 'dbs_itemdesc', 'messages']


def load_glossary():
    g = {}
    for line in open(os.path.join(TR, 'glossary.tsv'), encoding='utf-8').read().splitlines()[1:]:
        p = line.split('\t')
        if len(p) >= 2 and p[0]:
            g[p[0]] = (p[1], p[3] if len(p) > 3 else '')
    # translated database names (filled after the dbs_ chunks are done)
    names = os.path.join(ROOT, 'work', 'names.json')
    if os.path.exists(names):
        for en, ja in json.load(open(names, encoding='utf-8')).items():
            g.setdefault(en, (ja, ''))
    return g


def voice_for(group):
    for v in json.load(open(os.path.join(TR, 'voices.json'), encoding='utf-8')):
        if re.search(v['match'], group) if v['match'] else True:
            out = {'who': v['who']}
            if 'voice' in v:
                out['voice'] = v['voice']
            else:
                out['voice'] = v['female'] if group.startswith('quotes_F') else v['male']
            return out


def terms_in(text, gl):
    """case-sensitive for capitalised names, case-insensitive for lower-case UI terms."""
    found = {}
    low = text.lower()
    for en, (ja, note) in gl.items():
        if len(en) < 3 and en not in ('Vi',):
            continue
        hay, needle = (low, en.lower()) if en[0].islower() else (text, en)
        if needle in hay and re.search(r'(?<![A-Za-z])' + re.escape(needle) + r'(?![a-z])', hay):
            found[en] = ja + (' (' + note + ')' if note else '')
    return found


INTERNAL = re.compile(r'^(?:<\w+>|Spare\d+|[A-Za-z0-9]+(?:_[A-Za-z0-9]+)+|[A-Z][a-z]+[A-Z]\w*)$')


SHOWN = {'VapoRizer', 'NegatAir', 'ArmeGazers'}   # item / monster names that look like internal ids


def is_internal(s):
    """engine-internal names that are never shown (level ids, reserved slots)."""
    return bool(INTERNAL.match(s.strip())) and s.strip() not in SHOWN


def norm(s):
    return s.replace('\x92', "'").replace('\x93', '"').replace('\x94', '"').replace('\x85', '...').replace('\x96', '-')


def main():
    os.makedirs(CHUNKS, exist_ok=True)
    gl = load_glossary()
    groups = [os.path.basename(f)[:-5] for f in glob.glob(os.path.join(TEXT, '*.json'))]
    rest = sorted(g for g in groups if g not in ORDER)
    # dialogue: party quotes first (short, many personalities), then NPC scripts
    rest = sorted([g for g in rest if g.startswith('quotes_')]) + sorted([g for g in rest if not g.startswith('quotes_')])
    index = []
    for g in [x for x in ORDER if x in groups] + rest:
        es = json.load(open(os.path.join(TEXT, g + '.json'), encoding='utf-8'))
        parts, cur, w = [], [], 0
        for e in es:
            if g.startswith('dbs_') and is_internal(e['en']):
                continue
            cur.append({'id': e['id'], 'en': norm(e['en'])})
            w += len(e['en'].split())
            if w >= WORDS_PER_CHUNK:
                parts.append(cur); cur, w = [], 0
        if cur:
            parts.append(cur)
        for k, lines in enumerate(parts):
            name = g if len(parts) == 1 else '%s_%02d' % (g, k + 1)
            v = voice_for(g)
            chunk = {'chunk': name, 'group': g, 'who': v['who'], 'voice': v['voice'],
                     'terms': terms_in(' '.join(l['en'] for l in lines), gl), 'lines': lines}
            json.dump(chunk, open(os.path.join(CHUNKS, name + '.json'), 'w', encoding='utf-8'),
                      ensure_ascii=False, indent=1)
            index.append((name, len(lines), sum(len(l['en'].split()) for l in lines)))
    json.dump(index, open(os.path.join(CHUNKS, '_index.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(len(index), 'chunks', sum(i[2] for i in index), 'words')


if __name__ == '__main__':
    main()
