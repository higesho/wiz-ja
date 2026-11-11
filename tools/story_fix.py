# -*- coding: utf-8 -*-
"""物語本文の訳文（trans/parts/ja_sNN.txt）を機械的に直す（何度実行しても同じ結果）。
  python tools/story_fix.py

1. 会話のキーワード行（プレイヤーが英単語で入力する言葉との照合用）は原文のまま戻す
     - 末尾が % か @ の短い行 / ^数字 を含む行 / 先頭が \\x1c〜\\x1f の行
2. 看板の枠（\\x01〜\\x08）だけの行は原文のまま戻す
3. \\x04 … \\x05 で囲まれた行は、内側を原文と同じ幅にそろえる（足りなければ両側に半角スペースで埋める）
"""
import os, re, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import msgtool

BS = chr(92)
EN = msgtool.load_en(ROOT)

LEAD = re.compile(r'[ !"]*' + re.escape(BS) + r'x([0-9a-f]{2})')


def is_keyword(en):
    if en.endswith(('%', '@')) and len(en) <= 26:
        return True
    if re.search(r'\^\d', en):
        return True
    m = LEAD.match(en)
    if m and int(m.group(1), 16) in (0x1c, 0x1d, 0x1e, 0x1f):
        return True
    return False


def tok2text(toks):
    out = []
    for t in toks:
        if isinstance(t, str):
            out.append(t)
        elif 32 <= t < 127 and t != 92:
            out.append(chr(t))
        else:
            out.append(BS + 'x%02x' % t)
    return ''.join(out)


def fix_line(mid, text, report):
    en = EN[mid]
    if is_keyword(en):
        return en
    etoks = msgtool.unesc(en)
    if etoks and all(isinstance(t, int) and 1 <= t <= 8 for t in etoks):
        return en
    # 看板の 1 行: \x04 ... \x05
    if etoks and etoks[0] == 4 and etoks[-1] == 5:
        jtoks = msgtool.unesc(text)
        if jtoks and jtoks[0] == 4 and jtoks[-1] == 5:
            inner, want = jtoks[1:-1], len(etoks) - 2
            if len(inner) > want:
                report.append('%d: frame text too long (%d > %d)' % (mid, len(inner), want))
            else:
                pad = want - len(inner)
                left = pad // 2
                inner = [0x20] * left + inner + [0x20] * (pad - left)
                return tok2text([4] + inner + [5])
        else:
            report.append('%d: frame characters missing' % mid)
    return text


def main():
    total = 0
    report = []
    for p in sorted(glob.glob(os.path.join(ROOT, 'trans', 'parts', 'ja_s[0-9][0-9].txt'))):
        out, changed = [], 0
        for ln in open(p, encoding='utf-8'):
            ln = ln.rstrip('\r\n')
            parts = ln.split('\t')
            if len(parts) < 3 or not parts[0].isdigit():
                out.append(ln); continue
            mid = int(parts[0])
            text = '\t'.join(parts[2:])
            new = fix_line(mid, text, report) if mid in EN else text
            if new != text:
                changed += 1
            out.append('%s\t%s\t%s' % (parts[0], parts[1], new))
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(out) + '\n')
        print('%s: %d lines restored/adjusted' % (os.path.basename(p), changed))
        total += changed
    for r in report:
        print('REPORT', r)
    print('total', total)


if __name__ == '__main__':
    main()
