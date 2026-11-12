"""DOS 版の日本語の文字コード (ゲーム内の 2 バイト表現)

  日本語 1 文字 = 2 バイト  [0x80 + (n >> 7)] [0x80 + (n & 0x7f)]   n = 字の番号 (0〜16383)
  どちらのバイトも 0x80 以上なので、ゲームが見る ASCII の制御文字 (空白・$ ^ _ など) とぶつからない。
  字の番号は、訳文で使う字を出現回数の多い順に並べたもの (jfont の並びと同じ)。

訳文 (msg_ja.txt) の書き方は Gold 版と同じ: {xx} は 1 バイトの制御コード、ほかの ASCII はそのまま。
"""
import re
from collections import Counter
from pathlib import Path

MOD = Path(__file__).resolve().parents[1]           # jpmod
TR = MOD / "translation"
CTRL = re.compile(r"\{([0-9a-fA-F]{2})\}")

# 全角に直してから字形を引く記号 (ASCII 以外で、Shinonome にない字の置き換え)
FIX = {"〜": "～", "―": "─", "‐": "-", "−": "－", "‖": "∥"}


def load_msg_ja():
    """訳文 {ID: 本文}。dos/msg_dos.txt の DOS 版だけの差し替えを反映する (本文が空なら英語のまま)"""
    d = {}
    for l in (TR / "msg_ja.txt").read_text(encoding="utf-8").splitlines():
        if l.startswith("#") or "\t" not in l:
            continue
        i, t = l.split("\t", 1)
        d[int(i)] = t
    for l in (Path(__file__).resolve().parent / "msg_dos.txt").read_text(encoding="utf-8").splitlines():
        if l.startswith("#") or "\t" not in l:
            continue
        i, t = l.split("\t", 1)
        if t.strip():
            d[int(i)] = t
        else:
            d.pop(int(i), None)
    return d


def load_scen_ja():
    """[(file_offset, en, ja)]"""
    rows = []
    for l in (TR / "scen_ja.txt").read_text(encoding="utf-8").splitlines():
        if l.startswith("#") or not l.strip():
            continue
        o, e, j = l.split("\t")
        rows.append((int(o, 16), e, j))
    return rows


def norm(ch):
    return FIX.get(ch, ch)


def charset(texts):
    """使う字を数え、多い順に並べた一覧"""
    c = Counter()
    for t in texts:
        for ch in CTRL.sub("", t):
            if ord(ch) >= 0x80:
                c[norm(ch)] += 1
    return [ch for ch, _ in sorted(c.items(), key=lambda kv: (-kv[1], kv[0]))]


def encode(text, index):
    """訳文 → ゲーム内のバイト列"""
    out = bytearray()
    i = 0
    while i < len(text):
        m = CTRL.match(text, i)
        if m:
            out.append(int(m.group(1), 16))
            i = m.end()
            continue
        ch = text[i]
        i += 1
        if ord(ch) < 0x80:
            out.append(ord(ch))
        else:
            n = index[norm(ch)]
            out += bytes([0x80 + (n >> 7), 0x80 + (n & 0x7F)])
    return bytes(out)


def ordered_chars():
    """(字の一覧, 小さい字の数)。UI (ID 8000 未満) と名前で使う字を先に並べる。
    小さいゲームのフォントで描く日本語は、先頭の「小さい字」だけ美咲 8x8 を持つ (ドライバの 64KB に収めるため)"""
    ja = load_msg_ja()
    names = [j.replace("/", "") for _, _, j in load_scen_ja()]
    small = charset([t for i, t in ja.items() if i < 8000] + names)
    if "〓" not in small:
        small.append("〓")
    rest = [c for c in charset(list(ja.values()) + names) if c not in set(small)]
    return small + rest, len(small)
