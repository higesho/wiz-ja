import json, glob, re, os
tot = 0
for p in glob.glob(os.path.join(os.path.dirname(__file__), '..', 'work', 'ja', '*.json')):
    if os.path.basename(p).startswith('_'):
        continue
    s = open(p, encoding='utf-8').read(); o = s
    s = re.sub(r'(軍曹|将軍|大尉|二等兵)(グルンフ|ラブル|ヤミール|ヤモ|エモンズ|クナール|パンラック|バルブラク|ビルダブル)', r'\2\1', s)
    if s != o:
        json.loads(s); open(p, 'w', encoding='utf-8').write(s); tot += 1; print(os.path.basename(p))
print(tot)
