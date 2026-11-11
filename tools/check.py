# -*- coding: utf-8 -*-
"""Validate translation files:  python tools/check.py trans/parts/ja_03.txt [...]  (or no args = trans/msg_ja.txt)"""
import os, sys, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import msgtool, fontgen

EN = msgtool.load_en(ROOT)

_mis = None


def mis():
    global _mis
    if _mis is None:
        _mis = fontgen.Misaki()
    return _mis


def prefix(text):
    """leading control run: non-alnum, non-space ASCII chars and \\xNN escapes."""
    toks = msgtool.unesc(text)
    p = []
    for t in toks:
        if isinstance(t, int) and t < 0x80 and not chr(t).isalnum() and t != 0x20:
            p.append(t)
        else:
            break
    return p


def measure(text):
    text = re.sub(r'\{[A-Za-z0-9]\}', '', text)
    toks = msgtool.unesc(text)
    width = len(toks)
    nbytes = sum(1 if isinstance(t, int) else 2 for t in toks)
    return width, nbytes, toks


def row_x(toks, w, state, consecutive):
    """迷宮メッセージの開始桁。先頭が $ なら 0、^ なら直前の行の開始桁、それ以外は中央揃え。"""
    first = toks[0] if toks else None
    if first == 0x24:
        x, disp = 0, w - 1
    elif first == 0x5e:
        x, disp = (state.get('x', 0) if consecutive else None), w - 1
    else:
        x, disp = 20 - (w + 1) // 2, w
    if x is not None:
        state['x'] = x
    return x, disp


def check_file(path):
    errs = []
    ids = []
    st_ja, st_en = {}, {}
    prev_mid = None
    for n, ln in enumerate(open(path, encoding='utf-8'), 1):
        ln = ln.rstrip('\r\n')
        if not ln or ln.startswith('#'):
            continue
        parts = ln.split('\t')
        if len(parts) < 3:
            errs.append('%s:%d: bad format' % (path, n)); continue
        try:
            mid = int(parts[0])
        except ValueError:
            errs.append('%s:%d: bad id' % (path, n)); continue
        ids.append(mid)
        text = '\t'.join(parts[2:])
        if mid not in EN:
            errs.append('%d: unknown id' % mid); continue
        en = EN[mid]
        ew, eb, _ = measure(en)
        w, b, toks = measure(text)
        pe, pj = prefix(en), prefix(text)
        if pe != pj[:len(pe)]:
            errs.append('%d: control prefix changed  en=%r ja=%r' % (mid, bytes(pe), bytes(pj)))
        if b > 78:
            errs.append('%d: %d bytes > 78' % (mid, b))
        if ew <= 16 and b > max(ew + 4, 12):
            errs.append('%d: short line %d bytes > max(%d+4,12)' % (mid, b, ew))
        if w > max(ew, 1) and text != en:
            errs.append('%d: width %d > original %d' % (mid, w, ew))
        # 迷宮メッセージ: ^ 行の右端が 40 桁を越えないか（原文が収まっているのに訳文が越える場合）
        consecutive = prev_mid is not None and mid == prev_mid + 1
        etoks = msgtool.unesc(re.sub(r'\{[A-Za-z0-9]\}', '', en))
        xe, de = row_x(etoks, ew, st_en, consecutive)
        xj, dj = row_x(toks, w, st_ja, consecutive)
        if xj is not None and xj + dj > 40 and not (xe is not None and xe + de > 40):
            errs.append('%d: overflow: starts at column %d, width %d (> 40). Make the first line of the group longer or shorten this line' % (mid, xj, dj))
        prev_mid = mid
        # 会話文は半角スペースの位置で折り返される: スペースで区切った 1 塊が長すぎると折り返せない
        if toks and toks[0] not in (0x24, 0x5e) and text != en and 0x5f not in toks:
            run = 0
            for t in toks + [0x20]:
                if t == 0x20:
                    if run > 30:
                        errs.append('%d: %d cells without a space (> 30). Insert half-width spaces at phrase breaks' % (mid, run))
                    run = 0
                else:
                    run += 1
        for t in toks:
            if isinstance(t, str) and not mis().has(t):
                errs.append('%d: glyph missing in Misaki: %r' % (mid, t))
            if isinstance(t, str) and ord(t) < 0x100:
                errs.append('%d: non-ASCII Latin char %r' % (mid, t))
    return ids, errs


def main():
    paths = sys.argv[1:] or [os.path.join(ROOT, 'trans', 'msg_ja.txt')]
    total = 0
    for p in paths:
        ids, errs = check_file(p)
        if len(ids) != len(set(ids)):
            errs.append('duplicate ids')
        for e in errs:
            print(p, e)
        total += len(errs)
        print('%s: %d lines, %d problems' % (p, len(ids), len(errs)))
    sys.exit(1 if total else 0)


if __name__ == '__main__':
    main()
