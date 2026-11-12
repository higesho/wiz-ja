"""Group untranslated chunks into agent batches of about MAX_WORDS words (work/batches.json)."""
import os, json, sys
ROOT = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod'
MAX_WORDS = int(sys.argv[1]) if len(sys.argv) > 1 else 7000
only = sys.argv[2] if len(sys.argv) > 2 else ''
ix = json.load(open(os.path.join(ROOT, 'work', 'chunks', '_index.json'), encoding='utf-8'))
todo = [c for c in ix if not os.path.exists(os.path.join(ROOT, 'work', 'ja', c[0] + '.json')) and c[0].startswith(only)]
batches, cur, w = [], [], 0
for name, n, words in todo:
    if cur and w + words > MAX_WORDS:
        batches.append(cur); cur, w = [], 0
    cur.append(name); w += words
if cur:
    batches.append(cur)
json.dump(batches, open(os.path.join(ROOT, 'work', 'batches.json'), 'w'), indent=0)
print(len(todo), 'chunks', sum(c[2] for c in todo), 'words ->', len(batches), 'batches')
for i, b in enumerate(batches):
    print(i, ' '.join(b))
