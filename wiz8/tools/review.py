"""Full-text review support.

  python review.py make [MAX_WORDS]   -> work/review_in/<chunk>.json (en + ja side by side) and work/review_batches.json
  python review.py apply [chunk ...]  -> merge work/review_out/<chunk>.json into work/ja/, validate, log to work/review_log/<chunk>.json

Reviewer output (work/review_out/<chunk>.json): {"<id>": "corrected Japanese", ..., "_log": ["<id>: reason", ...]}
Only lines that change are listed. An empty object {} (or only "_log") means "no changes".
"""
import os, re, sys, json, glob

ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
W = os.path.join(ROOT, 'work')
FMT = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l)?[sdiuxXcfgeEp%]')


def make(max_words):
    os.makedirs(os.path.join(W, 'review_in'), exist_ok=True)
    os.makedirs(os.path.join(W, 'review_out'), exist_ok=True)
    ix = json.load(open(os.path.join(W, 'chunks', '_index.json'), encoding='utf-8'))
    batches, cur, w = [], [], 0
    for name, n, words in ix:
        c = json.load(open(os.path.join(W, 'chunks', name + '.json'), encoding='utf-8'))
        t = json.load(open(os.path.join(W, 'ja', name + '.json'), encoding='utf-8'))
        c['lines'] = [{'id': l['id'], 'en': l['en'], 'ja': t.get(l['id'], '')} for l in c['lines']]
        json.dump(c, open(os.path.join(W, 'review_in', name + '.json'), 'w', encoding='utf-8'),
                  ensure_ascii=False, indent=1)
        if os.path.exists(os.path.join(W, 'review_out', name + '.json')):
            continue
        if cur and w + words > max_words:
            batches.append(cur); cur, w = [], 0
        cur.append(name); w += words
    if cur:
        batches.append(cur)
    json.dump(batches, open(os.path.join(W, 'review_batches.json'), 'w'), indent=0)
    print(len(batches), 'review batches')


def apply(names):
    os.makedirs(os.path.join(W, 'review_log'), exist_ok=True)
    names = names or sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(W, 'review_out', '*.json')))
    for name in names:
        rp = os.path.join(W, 'review_out', name + '.json')
        if not os.path.exists(rp):
            print('no review for', name); continue
        try:
            r = json.load(open(rp, encoding='utf-8'))
        except Exception as e:
            print('BAD JSON', name, e); continue
        c = json.load(open(os.path.join(W, 'chunks', name + '.json'), encoding='utf-8'))
        en = {l['id']: l['en'] for l in c['lines']}
        jp = os.path.join(W, 'ja', name + '.json')
        t = json.load(open(jp, encoding='utf-8'))
        changed, rejected = [], []
        for k, v in r.items():
            if k.startswith('_'):
                continue
            if k not in en or not isinstance(v, str) or not v.strip():
                rejected.append((k, 'unknown id or empty')); continue
            if FMT.findall(en[k]) != FMT.findall(v):
                rejected.append((k, 'format specifiers changed')); continue
            if en[k].count('\n') != v.count('\n'):
                rejected.append((k, 'newline count changed')); continue
            if t.get(k) != v:
                changed.append({'id': k, 'old': t.get(k), 'new': v})
                t[k] = v
        json.dump(t, open(jp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        json.dump({'changed': changed, 'rejected': rejected, 'notes': r.get('_log', [])},
                  open(os.path.join(W, 'review_log', name + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        for k, why in rejected:
            print('   rejected:', k, why)
        print('%-28s changed %3d rejected %d' % (name, len(changed), len(rejected)))


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    if sys.argv[1] == 'make':
        make(int(sys.argv[2]) if len(sys.argv) > 2 else 7000)
    else:
        apply(sys.argv[2:])
