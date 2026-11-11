"""改ページの端切れ (4 行で埋まったページの次に、数文字だけのページが出る所) を探す。

  python tools/orphans.py            一覧と件数
  python tools/orphans.py export N   translation/work/orphans/oNN.txt に N 分割で書き出す (Haiku に縮めてもらう)
  python tools/orphans.py apply      translation/work/orphans/outNN.tsv の訳を translation/ja/*.txt に反映

端切れの判定: 直前のページが 4 行で埋まり、そのページが 1 行だけで、その行が 8 字以下。
表示に出ない行 (制御バイトだけ・キーワード表) は除く。折り返しは tools/wrapsim.py と同じ (訳文は msg_ja.txt)。
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402
import wrapsim as w  # noqa: E402

WORK = batch.TR / "work" / "orphans"
MAX_ORPHAN = 8


class TrackScreen(w.Screen):
    """行ごとに、その行に文字を出したメッセージの番号を記録する。"""

    def __init__(self):
        self._line = ""
        self.cur = None
        self.src = set()
        self.page_src = [[]]
        self.page_why = ["start"]    # そのページが始まった理由: overflow (4 行あふれ) / wait (クリック待ち)
        super().__init__()

    @property
    def line(self):
        return self._line

    @line.setter
    def line(self, v):
        if v and v != self._line and self.cur is not None:
            self.src.add(self.cur)
        self._line = v

    def flush(self):
        if len(self.pages[-1]) >= w.ROWS:
            self.pages.append([])
            self.page_src.append([])
            self.page_why.append("overflow")
        self.pages[-1].append(self._line)
        self.page_src[-1].append(set(self.src))
        self._line = ""
        self.src = {self.cur} if self.cur is not None else set()
        self.src.discard(None)

    def wait(self):
        if self.pages[-1]:
            self.pages.append([])
            self.page_src.append([])
            self.page_why.append("wait")


def visible(line):
    s = line.strip()
    return s and not re.search(r"[\x00-\x1f]", s) and re.search(r"[^\x00-\x7f]", s)


def find_orphans():
    en = batch.load_en()
    out = []
    for blk, en_t, ja_t in w.body_blocks():
        scr = TrackScreen()
        for i, t in enumerate(ja_t):
            scr.cur = i
            w.split_jp(scr, w.decode(t))
        if scr.line:
            scr.flush()
        for p in range(1, len(scr.pages)):
            page, prev = scr.pages[p], scr.pages[p - 1]
            if scr.page_why[p] == "overflow" and len(prev) == w.ROWS and len(page) == 1 and visible(page[0]) and \
                    width_chars(page[0]) <= MAX_ORPHAN and all(visible(l) for l in prev):
                idx = set().union(*scr.page_src[p - 1], *scr.page_src[p])
                ids = [blk[i] for i in sorted(idx) if not batch.is_keyword(en[blk[i]])]
                out.append({"block": blk[0], "orphan": page[0], "prev": prev, "ids": ids,
                            "need": width_chars(page[0]) + 1})
    return out


def width_chars(s):
    return -(-w.width(s) // w.JP_ADV)


def cmd_list():
    o = find_orphans()
    print(f"端切れページ {len(o)} か所")
    for x in o[:40]:
        print(f"  {x['block']}: 「{x['orphan'].strip()}」  ID {x['ids'][0]}〜{x['ids'][-1]}  {x['need']} 字以上縮める")


def cmd_export(n):
    en = batch.load_en()
    src = {}
    for f in sorted(batch.JA_DIR.glob("*.txt")):
        for k, t in batch.load(f).items():
            if isinstance(k, int):
                src[k] = t
    o = find_orphans()
    WORK.mkdir(parents=True, exist_ok=True)
    for old in WORK.glob("o*.txt"):
        old.unlink()
    size = -(-len(o) // n)
    for part in range(n):
        lines = []
        for j, x in enumerate(o[part * size:(part + 1) * size]):
            lines.append(f"## case {part * size + j + 1}  縮める字数: {x['need']} 字以上")
            lines.append("今の表示 (1 行 15 字、4 行で改ページ):")
            for l in x["prev"]:
                lines.append(f"  |{l}")
            lines.append(f"  |---- 改ページ ----")
            lines.append(f"  |{x['orphan']}")
            for k in x["ids"]:
                lines.append(f"EN {k}\t{en[k]}")
                lines.append(f"JA {k}\t{src.get(k, '')}")
            lines.append("")
        (WORK / f"o{part + 1:02d}.txt").write_text("\n".join(lines), encoding="utf-8")
    print(f"{len(o)} か所を {n} 個のファイルに書き出した: {WORK}")


def cmd_apply():
    new = {}
    for f in sorted(WORK.glob("out*.tsv")):
        for line in f.read_text(encoding="utf-8").splitlines():
            if "\t" in line and line.split("\t", 1)[0].strip().isdigit():
                k, t = line.split("\t", 1)
                new[int(k)] = t
    changed = 0
    for f in sorted(batch.JA_DIR.glob("*.txt")):
        lines = f.read_text(encoding="utf-8").split("\n")
        dirty = False
        for i, line in enumerate(lines):
            if "\t" not in line or line.startswith("#"):
                continue
            k = line.split("\t", 1)[0]
            if k.isdigit() and int(k) in new and line != f"{k}\t{new[int(k)]}":
                lines[i] = f"{k}\t{new[int(k)]}"
                dirty = True
                changed += 1
        if dirty:
            f.write_text("\n".join(lines), encoding="utf-8")
    print(f"{changed} 行を反映 (候補 {len(new)})")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "export":
        cmd_export(int(a[1]) if len(a) > 1 else 4)
    elif a and a[0] == "apply":
        cmd_apply()
    else:
        cmd_list()
