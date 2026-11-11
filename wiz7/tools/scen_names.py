"""SCENARIO.GLD の名前欄 (品物・怪物・人物) を取り出し、訳を当てる。

SCENARIO.GLD は固定長レコードの並びで、名前は 16 バイトの欄 (NUL 終端、残りは前の名前の残骸のことがある)。
  品物: ファイル位置 ≡ 32 (mod 72) の欄 (0x380〜0xa400)。"/" は 2 行に分けて表示する位置
  怪物: 16 バイト間隔で 4 つ並ぶ欄 (単数・複数・未識別の単数・複数)。0x37000〜0x45000
  人物: ファイル位置 ≡ 72 (mod 148) の欄 (0x45000〜0x49000)
DLL は SCENARIO.GLD の読み込みを横取りし、jpmod/scen_ja.txt の「ファイル位置<TAB>訳」をその位置に書き込む。

  python tools/scen_names.py extract   translation/scen_en.tsv (位置・種類・原文) を書く
  python tools/scen_names.py build     translation/scen_names_ja.tsv (原文→訳) から translation/scen_ja.txt を作る
  python tools/scen_names.py import F..  原文<TAB>訳 のファイルを scen_names_ja.tsv に取り込む (規則を検査)
  python tools/scen_names.py status    訳の進み具合
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402

GLD = batch.MOD.parent / "wizardry7gold" / "SCENARIO.GLD"
EN_TSV = batch.TR / "scen_en.tsv"
NAMES = batch.TR / "scen_names_ja.tsv"     # 原文<TAB>訳 (同じ原文は同じ訳)
OUT = batch.TR / "scen_ja.txt"             # ファイル位置<TAB>訳 (DLL が読む)
FIELD = 16
NAME_RE = re.compile(rb"[A-Z0-9][A-Z0-9 '\.\-&,!/()]{0,14}")


def field_at(d, o):
    """o から始まる 16 バイト欄の名前 (NUL まで)。名前らしくなければ None"""
    if o > 0 and re.match(rb"[A-Za-z0-9'\.\-]", d[o - 1:o]):
        return None                     # 名前の途中 (前の名前の残骸) から始まっている
    raw = d[o:o + FIELD]
    s = raw.split(b"\0", 1)[0]
    if not s or len(s) >= FIELD or b"\0" not in raw:
        return None
    if not NAME_RE.fullmatch(s) or not re.search(rb"[A-Z]{2}", s):
        return None
    return s.decode()


def extract(d):
    out = []                    # (offset, kind, name)
    for o in range(0x380, 0xa400, 72):
        s = field_at(d, o)
        if s:
            out.append((o, "item", s))
    o = 0x37000
    while o < 0x45000:
        f = [field_at(d, o + 16 * i) for i in range(4)]
        if all(f):
            for i in range(4):
                out.append((o + 16 * i, "monster", f[i]))
            o += 64
        else:
            o += 1
    for o in range(0x45000 + (72 - 0x45000 % 148) % 148, 0x49000, 148):
        s = field_at(d, o)
        if s:
            out.append((o, "npc", s))
    return out


def cmd_extract():
    d = GLD.read_bytes()
    rows = extract(d)
    with EN_TSV.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# ファイル位置(16進)\t種類\t原文   (tools/scen_names.py extract が生成)\n")
        for o, k, s in rows:
            f.write(f"{o:06x}\t{k}\t{s}\n")
    kinds = {}
    for _, k, s in rows:
        kinds.setdefault(k, set()).add(s)
    print(f"{EN_TSV}: {len(rows)} 欄", {k: len(v) for k, v in kinds.items()})


def load_rows():
    rows = []
    for line in EN_TSV.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line:
            continue
        o, k, s = line.split("\t")
        rows.append((int(o, 16), k, s))
    return rows


def load_names():
    d = {}
    if NAMES.exists():
        for line in NAMES.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or "\t" not in line:
                continue
            e, j = line.split("\t", 1)
            if j.strip():
                d[e] = j.strip()
    return d


def enc_len(t):
    return sum(1 if ord(c) < 128 else 2 for c in t)


LINE_MAX = 10      # 品物の名前は "/" で 2 行に分けて描かれ、1 行 10 バイト (全角 5 字) まで


def check_name(kind, j):
    if enc_len(j) >= FIELD:
        return f"長すぎ ({enc_len(j)} バイト、15 まで)"
    if kind == "item" and any(enc_len(p) > LINE_MAX for p in j.split("/")):
        return "1 行が長すぎ (全角 5 字まで。/ で分ける)"
    if j.count("/") > 1:
        return "/ は 1 つまで"
    return None


def cmd_import(files):
    """Haiku などの出力 (原文<TAB>訳) を scen_names_ja.tsv に取り込む。規則に合わない訳は取り込まない"""
    kinds = {}
    for _, k, s in load_rows():
        kinds.setdefault(s, k)
    lines = NAMES.read_text(encoding="utf-8").splitlines()
    idx = {l.split("\t", 1)[0]: i for i, l in enumerate(lines) if "\t" in l and not l.startswith("#")}
    ok = ng = 0
    for fn in files:
        for line in Path(fn).read_text(encoding="utf-8-sig").splitlines():
            if "\t" not in line:
                continue
            e, j = [x.strip() for x in line.split("\t", 1)]
            if e not in idx or not j:
                continue
            j = j.replace("／", "/")
            err = check_name(kinds.get(e, "item"), j)
            if err:
                print(f"取り込まず ({err}): {e} → {j}")
                ng += 1
                continue
            lines[idx[e]] = f"{e}\t{j}"
            ok += 1
    NAMES.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"取り込み {ok}、規則違反 {ng}")


def cmd_build():
    rows = load_rows()
    names = load_names()
    n = bad = 0
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# SCENARIO.GLD の名前欄の訳: ファイル位置(16進)<TAB>訳  (tools/scen_names.py build が生成)\n")
        for o, k, s in rows:
            j = names.get(s)
            if not j:
                continue
            err = check_name(k, j)
            if err:
                print(f"{err}: {s} → {j}")
                bad += 1
                continue
            f.write(f"{o:06x}\t{j}\n")
            n += 1
    print(f"{OUT}: {n} 欄 (長すぎて除外 {bad})")


def cmd_status():
    rows = load_rows()
    names = load_names()
    uniq = {}
    for _, k, s in rows:
        uniq.setdefault(k, set()).add(s)
    for k, v in uniq.items():
        print(k, f"{sum(1 for s in v if s in names)}/{len(v)}")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    if cmd == "import":
        cmd_import(sys.argv[2:])
    else:
        {"extract": cmd_extract, "build": cmd_build, "status": cmd_status}[cmd]()
