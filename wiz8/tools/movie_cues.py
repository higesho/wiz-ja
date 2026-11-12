"""Turn the Whisper output (work/movies/<name>.en.json) into subtitle cues: one sentence per cue,
long sentences split at commas so a cue stays under ~6.5 s / ~110 chars, short ones merged.
Writes work/movies/cues.json: {movie: [{"id", "start", "end", "en"}, ...]}. Keeps "ja" from an
existing cues.json when the English is unchanged."""
import os, json, glob, re

D = os.path.join(os.path.dirname(__file__), '..', 'work', 'movies')
MAXLEN, MAXDUR = 110, 6.5


def split_words(words):
    """words: [[start, end, text]] -> list of word groups, sentence by sentence"""
    out, cur = [], []
    for w in words:
        cur.append(w)
        if re.search(r'[.!?]["\']?$', w[2].strip()):
            out.append(cur); cur = []
    if cur:
        out.append(cur)
    return out


def text(ws):
    return ''.join(w[2] for w in ws).strip()


def fit(ws):
    """split a sentence at the comma nearest its middle until every piece fits"""
    if len(text(ws)) <= MAXLEN and ws[-1][1] - ws[0][0] <= MAXDUR:
        return [ws]
    cuts = [i + 1 for i, w in enumerate(ws[:-1]) if re.search(r'[,;:—-]$', w[2].strip())]
    if not cuts:
        cuts = list(range(2, len(ws) - 1))
    if not cuts:
        return [ws]
    mid = len(ws) / 2
    c = min(cuts, key=lambda i: abs(i - mid))
    return fit(ws[:c]) + fit(ws[c:])


def main():
    path = os.path.join(D, 'cues.json')
    old = json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}
    oldja = {(m, c['en']): c.get('ja') for m, cs in old.items() for c in cs}
    res = {}
    for f in sorted(glob.glob(os.path.join(D, '*.en.json'))):
        name = os.path.basename(f)[:-8]
        words = [w for s in json.load(open(f, encoding='utf-8')) for w in s['words']]
        groups = []
        for sent in split_words(words):
            groups += fit(sent)
        # merge very short cues into the previous one when the gap is small
        merged = []
        for g in groups:
            if merged and (len(text(g)) < 25 or len(text(merged[-1])) < 25) \
                    and g[0][0] - merged[-1][-1][1] < 0.6 \
                    and len(text(merged[-1] + g)) <= MAXLEN and g[-1][1] - merged[-1][0][0] <= MAXDUR:
                merged[-1] = merged[-1] + g
            else:
                merged.append(g)
        cues = []
        for i, g in enumerate(merged):
            c = {'id': '%s#%d' % (name, i), 'start': g[0][0], 'end': g[-1][1], 'en': text(g)}
            if oldja.get((name, c['en'])):
                c['ja'] = oldja[(name, c['en'])]
            cues.append(c)
        res[name] = cues
        print(name, len(cues), 'cues')
    json.dump(res, open(path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)


if __name__ == '__main__':
    main()
