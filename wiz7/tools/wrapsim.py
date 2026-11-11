"""物語本文の折り返しをオフラインで再現し、英語原文と訳文を比べる。

Gold 版の物語表示 (0x4782a6 → 0x47b169 → 0x47b210 → 0x47b7a6) を Python で再現する。
  英語: 元の処理 (空白で語に分け、行バッファに空白でつなぐ)
  日本語: DLL の hk_gold_split (全角 1 字ごと + 行頭/行末禁則、全角どうしは空白を詰める)
  幅 0xb8 (論理 184px) を超えたら改行。ウィンドウは 4 行で、あふれるとクリック待ちで改ページ。
  語頭の制御記号: ! 改行  % 改行+クリック待ち  & 改行+クリック待ち  ] 消去
                  @ 改行+改ページ  # 看板表示  ~x 引数付き (効果音など)
  語中の _ は空白 (そこで改行しない)。

使い方:
  python tools/wrapsim.py                 集計と要注意の一覧
  python tools/wrapsim.py show ID [ID..]  そのメッセージを含むブロックの表示を再現
  python tools/wrapsim.py png ID [ID..]   同じく画像 (translation/wrapsim/*.png) に描く
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402

MOD = batch.MOD
GAME = MOD.parent / "wizardry7gold"
WRAP_WIDTH = 0xB8
def _ini_adv():
    """jpmod.ini の [font2] adv (日本語 1 字の送り幅、論理 px)"""
    import configparser
    c = configparser.ConfigParser()
    c.read(MOD / "jpmod.ini", encoding="utf-8-sig")
    return c.getint("font2", "adv", fallback=10)


JP_ADV = _ini_adv()
ROWS = 4
CTRL = "!%&]@#~"
KINSOKU = set("、。，．・：；？！ー…‥」』）］｝〕〉》】ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々゛゜")
NO_LINE_END = set("「『（［｛〔〈《【")


def font_widths(name="VBFONT2.VGA"):
    d = (GAME / name).read_bytes()
    sp = d[7] - 256 if d[7] >= 128 else d[7]
    return [d[17 + i * 2] + sp for i in range(128)]


W2 = font_widths()


def decode(t):
    """{xx} を 1 文字の制御バイトに。"""
    return re.sub(r"\{([0-9a-fA-F]{2})\}", lambda m: chr(int(m.group(1), 16)), t)


def is_jp(c):
    return ord(c) >= 128


def width(s):
    return sum(JP_ADV if is_jp(c) else W2[ord(c) & 127] for c in s)


class Screen:
    """4 行のテキストウィンドウ。ページ (クリック待ちで区切られた画面) ごとに行を記録する。"""

    def __init__(self):
        self.pages = [[]]
        self.line = ""      # 組み立て中の行 (0x4b6130)

    def flush(self):
        """0x47b7a6: 行を描く。4 行を超えていたら改ページしてから。"""
        if len(self.pages[-1]) >= ROWS:
            self.pages.append([])
        self.pages[-1].append(self.line)
        self.line = ""

    def wait(self):
        if self.pages[-1]:
            self.pages.append([])

    def control(self, c):
        if c == "!":
            self.flush()
        elif c in "%&":
            self.flush()
            self.wait()
        elif c == "@":
            self.flush()
            self.wait()
        elif c == "]":
            self.wait()


def ctrl_len(w):
    n = 0
    while n < len(w) and w[n] in CTRL:
        n += 2 if w[n] == "~" and n + 1 < len(w) else 1
    return n


def add_en(scr, w):
    """0x47b210"""
    c = ctrl_len(w)
    for ch in w[:c]:
        scr.control(ch) if ch in CTRL else None
    w = w[c:].replace("_", " ")
    tmp = (scr.line + " " if scr.line else "") + w
    if width(tmp) > WRAP_WIDTH:
        scr.flush()
        scr.line = w
    else:
        scr.line = tmp


def unit_end(s, i, end):
    """DLL の unit_end と同じ単位分け。"""
    j = i
    while True:
        while j < end and not is_jp(s[j]):
            j += 1
        if j >= end:
            return end
        if s[j] in NO_LINE_END and j + 1 < end:
            j += 1
            continue
        j += 1
        while j < end and s[j] in KINSOKU:
            j += 1
        if j < end and s[j] == "(":
            r = j
            while r < end and s[r] != ")" and not is_jp(s[r]):
                r += 1
            if r < end and s[r] == ")":
                j = r + 1
                while j < end and s[j] in KINSOKU:
                    j += 1
        r = j
        while r < end and not is_jp(s[r]) and not s[r].isalnum():
            r += 1
        return end if r == end else j


def split_jp(scr, msg):
    """hk_gold_split"""
    sp = True
    i = 0
    while i < len(msg):
        if msg[i] == " ":
            sp, i = True, i + 1
            continue
        we = msg.find(" ", i)
        we = len(msg) if we < 0 else we
        c = ctrl_len(msg[i:we])
        for ch in msg[i:i + c]:
            scr.control(ch) if ch in CTRL else None
        i += c
        while i < we:
            ue = unit_end(msg, i, we)
            u = msg[i:ue].replace("_", " ")
            joiner = " " if (scr.line and sp and not (is_jp(scr.line[-1]) and is_jp(u[0]))) else ""
            tmp = scr.line + joiner + u
            if scr.line and width(tmp) > WRAP_WIDTH:
                scr.flush()
                scr.line = u
            else:
                scr.line = tmp
            sp = False
            i = ue
        i = we


def run_block(texts, jp):
    """0x4782a6: 連続するメッセージを 1 つずつ処理し、最後に残りの行を描く。"""
    scr = Screen()
    for t in texts:
        t = decode(t)
        if jp:
            split_jp(scr, t)
        else:
            for w in t.split(" "):
                add_en(scr, w)
    if scr.line:
        scr.flush()
    if not scr.pages[-1]:
        scr.pages.pop()
    return scr


def body_blocks():
    en = batch.load_en()
    ja = batch.load(batch.OUT)
    out = []
    for blk in batch.blocks(en):
        if not any(batch.is_body(en, k) for k in blk):
            continue
        if not any(k in ja for k in blk):
            continue
        out.append((blk, [en[k] for k in blk], [ja.get(k, en[k]) for k in blk]))
    return out


def summary():
    blks = body_blocks()
    n_en = n_ja = p_en = p_ja = 0
    overlong, kinsoku, line_end, more_pages = [], [], [], []
    for blk, en_t, ja_t in blks:
        se, sj = run_block(en_t, False), run_block(ja_t, True)
        le = [l for p in se.pages for l in p if l.strip()]
        lj = [l for p in sj.pages for l in p if l.strip()]
        n_en += len(le)
        n_ja += len(lj)
        p_en += len(se.pages)
        p_ja += len(sj.pages)
        if len(sj.pages) > len(se.pages):
            more_pages.append((len(sj.pages) - len(se.pages), blk[0], blk[-1]))
        for l in lj:
            if width(l) > WRAP_WIDTH and any(is_jp(c) for c in l):
                overlong.append((blk[0], width(l), l))
            s = l.lstrip()
            if s[:1] in KINSOKU and s[:1] != "…":
                kinsoku.append((blk[0], l))
            if l.rstrip()[-1:] in NO_LINE_END:
                line_end.append((blk[0], l))
    print(f"本文ブロック {len(blks)} 件")
    print(f"  行数   英語 {n_en} / 日本語 {n_ja} ({n_ja / n_en:.2f} 倍)")
    print(f"  ページ 英語 {p_en} / 日本語 {p_ja} (クリック回数の目安)")
    print(f"幅 {WRAP_WIDTH}px を超える日本語の行: {len(overlong)}")
    for x in overlong[:10]:
        print("  ", x[0], x[1], x[2])
    print(f"行頭禁則違反 (… 以外): {len(kinsoku)}  行末に開き括弧: {len(line_end)}")
    for x in (kinsoku + line_end)[:10]:
        print("  ", x[0], x[1])
    more_pages.sort(reverse=True)
    print(f"英語よりページが増えるブロック: {len(more_pages)}")
    for d, a, b in more_pages[:15]:
        print(f"  {a}-{b}: +{d}")


def find_block(k):
    en = batch.load_en()
    ja = batch.load(batch.OUT)
    for blk in batch.blocks(en):
        if blk[0] <= k <= blk[-1]:
            return blk, [en[x] for x in blk], [ja.get(x, en[x]) for x in blk]
    return None


def fmt(scr):
    out = []
    for n, page in enumerate(scr.pages):
        out.append(f"  -- {n + 1}")
        for l in page:
            out.append(f"  |{l}")
    return "\n".join(out)


def show(ids):
    for k in ids:
        r = find_block(k)
        if not r:
            print(k, "なし")
            continue
        blk, en_t, ja_t = r
        print(f"== {blk[0]}-{blk[-1]}")
        print(fmt(run_block(en_t, False)))
        print(fmt(run_block(ja_t, True)))


def png(ids):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype("msgothic.ttc", 20)
    d = MOD / "translation" / "wrapsim"
    d.mkdir(exist_ok=True)
    for k in ids:
        r = find_block(k)
        if not r:
            continue
        blk, _, ja_t = r
        scr = run_block(ja_t, True)
        rows = sum(len(p) for p in scr.pages) + len(scr.pages)
        im = Image.new("RGB", (400, 24 * rows + 16), (24, 24, 40))
        dr = ImageDraw.Draw(im)
        y = 8
        for page in scr.pages:
            dr.line((0, y + 2, 400, y + 2), fill=(90, 40, 40))
            y += 24
            for l in page:
                x = 200 - width(l)
                dr.rectangle((200 - WRAP_WIDTH, y, 200 + WRAP_WIDTH, y + 23), outline=(60, 60, 90))
                for c in l:
                    dr.text((x, y + 2), c if (is_jp(c) or ord(c) >= 32) else "?", font=font, fill=(230, 230, 200))
                    x += JP_ADV * 2 if is_jp(c) else W2[ord(c) & 127] * 2
                y += 24
        p = d / f"{blk[0]}.png"
        im.save(p)
        print(p)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "show":
        show([int(x) for x in sys.argv[2:]])
    elif len(sys.argv) > 2 and sys.argv[1] == "png":
        png([int(x) for x in sys.argv[2:]])
    else:
        summary()
