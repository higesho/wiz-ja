"""Render translation/glossary.tsv and voices.json as readable Markdown (用語表.md / 話し方.md)."""
import os, json
from collections import OrderedDict

TR = r'L:\SteamLibrary\steamapps\common\Wizardry8\JPMod\translation'


def main():
    rows = [l.split('\t') for l in open(os.path.join(TR, 'glossary.tsv'), encoding='utf-8').read().splitlines()[1:] if l]
    cats = OrderedDict()
    for r in rows:
        r += [''] * (4 - len(r))
        cats.setdefault(r[2] or 'その他', []).append(r)
    out = ['# 用語表', '', '正本は `glossary.tsv`(このファイルは `tools/make_docs.py` で生成)。', '']
    for c, rs in cats.items():
        out += ['## ' + c, '', '| 英語 | 日本語 | メモ |', '|---|---|---|']
        out += ['| %s | %s | %s |' % (r[0], r[1], r[3]) for r in rs]
        out.append('')
    open(os.path.join(TR, '用語表.md'), 'w', encoding='utf-8').write('\n'.join(out))

    vs = json.load(open(os.path.join(TR, 'voices.json'), encoding='utf-8'))
    out = ['# 話し方の表記まとめ', '', '正本は `voices.json`(このファイルは `tools/make_docs.py` で生成)。',
           '`対象` はチャンク名に対する正規表現。上から順に最初に一致したものが使われる。', '']
    pcs = [v for v in vs if v['match'].startswith('quotes_')]
    out += ['## パーティメンバー(性格別)', '', '| 性格 | 男性 | 女性 |', '|---|---|---|']
    out += ['| %s | %s | %s |' % (v['who'].split(':')[-1].strip(' )'), v['male'], v['female']) for v in pcs]
    out += ['', '## NPC・その他', '', '| 話し手 | 話し方 | 対象 |', '|---|---|---|']
    out += ['| %s | %s | `%s` |' % (v['who'], v['voice'], v['match'].replace('|', '\\|') or '(既定)')
            for v in vs if v not in pcs]
    open(os.path.join(TR, '話し方.md'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print('wrote 用語表.md (%d terms), 話し方.md (%d voices)' % (len(rows), len(vs)))


if __name__ == '__main__':
    main()
