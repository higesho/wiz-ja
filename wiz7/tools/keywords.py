"""会話キーワードの抽出と、訳文への (英語) の書き添え。

会話では、プレイヤーが英単語を打ち込み、メッセージ中のキーワード表 (`!NEW CITY@` `"MAP%` など) と照合する。
日本語の台詞からは何を打てばよいか分からないので、キーワードに当たる訳語の後ろに「(NEW CITY)」と添える。

  python tools/keywords.py extract   キーワードの一覧と、訳語の候補を translation/keywords_ja.tsv に書く
                                      (既にある行は残す。訳語が空の行は手か Haiku で埋める)
  python tools/keywords.py annotate  translation/msg_ja.txt に書き添える (batch.py merge から呼ばれる)
  python tools/keywords.py stats     何件に書き添えるかの集計だけ

規則:
- 1 つの会話ブロック (連続 ID) で、そのキーワードが原文に出てくるとき、訳文で最初に出る訳語の直後に 1 回だけ添える。
- 添えると長さの上限 (batch.BODY_LIMIT・書き込み先バッファ) を超える場合は添えない。
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402

KW_FILE = batch.TR / "keywords_ja.tsv"
MIN_ID = 8000      # これより前の ID (UI・名前の表など) には書き添えない
STOP = {"NO", "YES", "YEA", "YEP", "SURE", "OK", "YE", "MAYBE", "NOPE", "NAY", "YOU", "ALL", "US", "WE",
        "ME", "I", "IT", "THE", "AND", "OF", "TO", "A", "AN", "IS", "ARE"}


def kw_from_header(t):
    """キーワード表の 1 行から、キーワードを取り出す。"""
    t = re.sub(r"\{..\}", "", t)
    t = re.sub(r"^\d+\}", "", t.strip())
    out = []
    for part in t.split("/"):
        w = part.strip()
        w = re.sub(r"\^\d.*$", "", w)              # PROPHECY^1IES → PROPHECY
        w = re.sub(r"^[!\"'#&%@\[\]~ ]+", "", w)
        w = re.sub(r"[@%'\s]+$", "", w)
        if len(w) >= 2 and re.search(r"[A-Z]", w) and w not in STOP:
            out.append(w)
    return out


def all_keywords():
    en = batch.load_en()
    kws = {}
    for k, t in en.items():
        if batch.is_keyword(t):
            for w in kw_from_header(t):
                kws.setdefault(w, k)
    return kws


def kw_pattern(w):
    return re.compile(r"(?<![A-Za-z])" + re.escape(w).replace(r"\ ", r"\s+") + r"(?![A-Za-z])", re.I)


def glossary_guess():
    """用語集の固有名詞・呪文・000_manual の短い訳から、キーワード → 訳語 の候補"""
    import termcheck
    g = {}
    for _, e, j in termcheck.pairs():
        g.setdefault(e.upper(), j)
    en = batch.load_en()
    man = batch.load(batch.JA_DIR / "000_manual.txt")
    for k, j in man.items():
        if isinstance(k, int) and k in en and len(en[k].strip()) <= 24:
            g.setdefault(en[k].strip().upper(), j.strip())
    return g


def load_kw_file():
    d = {}
    if KW_FILE.exists():
        for line in KW_FILE.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or "\t" not in line:
                continue
            p = line.split("\t")
            d[p[0]] = [x.strip() for x in p[1].split(",") if x.strip()] if len(p) > 1 else []
    return d


def cmd_extract():
    kws = all_keywords()
    old = load_kw_file()
    g = glossary_guess()
    en = batch.load_en()
    body = {k for k, t in en.items() if not batch.is_keyword(t) and not batch.skipped(k, t)}
    lines = ["# キーワード<TAB>訳文中の訳語 (表記の揺れはカンマ区切り)<TAB>原文の本文での出現数",
             "# 訳語が空ならそのキーワードは書き添えない"]
    for w in sorted(kws):
        n = sum(1 for k in body if kw_pattern(w).search(en[k]))
        if w in old:
            v = old[w]
        elif w in g:
            v = [g[w]]
        else:
            v = []
        lines.append(f"{w}\t{','.join(v)}\t{n}")
    KW_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")
    empty = sum(1 for l in lines[2:] if l.split("\t")[1] == "")
    print(f"{KW_FILE}: {len(kws)} 語 (訳語未定 {empty})")


KANJI = re.compile(r"[一-鿿々]")
KATA = re.compile(r"[ァ-ヶー]")


def find_word(t, v):
    """訳文 t の中で訳語 v が語として出てくる位置の直後を返す (無ければ -1)。
    すでに (英語) が付いている所、カタカナ語の一部、1 字の訳語が熟語の一部 (王 → 王国) の所は除く。"""
    i = t.find(v)
    while i >= 0:
        j = i + len(v)
        nxt, prv = t[j:j + 1], t[i - 1:i] if i else ""
        ok = nxt != "("
        if ok and KATA.match(v[-1]) and KATA.match(nxt or " "):
            ok = False
        if ok and KATA.match(v[0]) and KATA.match(prv or " "):
            ok = False
        if ok and len(v) == 1 and (KANJI.match(nxt or " ") or KANJI.match(prv or " ")):
            ok = False
        if ok:
            return j
        i = t.find(v, i + 1)
    return -1


def annotate(ja, en, report=False):
    """ja (ID→訳文) に書き添えた新しい辞書を返す。"""
    kw = {w: v for w, v in load_kw_file().items() if v}
    caps = batch.buf_caps()
    pats = {w: kw_pattern(w) for w in kw}
    order = sorted(kw, key=lambda w: -len(w))     # 長いキーワードから (BLACK SHIP を SHIP より先に)
    out = dict(ja)
    count = skipped = 0
    for blk in batch.blocks(en):
        # 会話・物語 (ID 8000 以降) だけに添える。それより前は UI・職業名・称号・噂の差し込み語などで、
        # 欄の幅が決まっていたり文の部品だったりするので付けない
        ids = [k for k in blk if k >= MIN_ID and k in out and not batch.is_keyword(en[k])
               and not batch.skipped(k, en[k])]
        if not ids:
            continue
        en_text = " ".join(en[k] for k in blk)
        for w in order:
            if not pats[w].search(en_text):
                continue
            done = False
            for k in ids:
                t = out[k]
                # _ はゲームが改行しない空白として表示する。長い合言葉は途中で折り返せるよう空白のまま
                tag = "(" + (w.replace(" ", "_") if len(w) <= 16 else w) + ")"
                if tag in t:
                    done = True
                    break
                for v in sorted(kw[w], key=len, reverse=True):
                    j = find_word(t, v)
                    if j < 0:
                        continue
                    new = t[:j] + tag + t[j:]
                    n = batch.enc_len(new)
                    cap = caps.get(k)
                    if n > batch.BODY_LIMIT or n > batch.HARD_LIMIT or (cap and n + 1 > cap):
                        skipped += 1
                        done = True
                        break
                    out[k] = new
                    count += 1
                    done = True
                    break
                if done:
                    break
    moved, dropped = gather_sign_tags(out, en, kw)
    if report:
        print(f"書き添え {count} 件 (長さの上限で見送り {skipped} 件)")
        print(f"看板 {moved} 枚の書き添えを最後の行にまとめた (幅で外した書き添え {dropped} 件)")
    return out


# 看板 (# で始まるメッセージと、続く ! で始まる行) は枠が狭く、書き添えで行が増えると枠からはみ出す。
# 看板の書き添えは本文から外し、最後の行の後ろに「 !(A)(B)」として 1 行にまとめる
# (看板は空行 + 2 行が基本で、3 行目は英字だけなら入る。DLL が看板の行を 4 ドット上げている)。
# まとめた行が SIGN_ALL を超えるときは、前から SIGN_FIT に収まる分だけ残す (外したものは台詞などに付いている)。
SIGN_ALL = 140
SIGN_FIT = 131
TAG = re.compile(r"\([^()]+\)")
TRAIL_CTRL = re.compile(r"(\s+[!%&\]@][!%&\]@]*)+$")


def sign_blocks(en):
    for k in sorted(en):
        if en[k].startswith("#"):
            ids = [k]
            j = k + 1
            while j in en and en[j].startswith("!") and not en[j].startswith("#"):
                ids.append(j)
                j += 1
            yield ids


def gather_sign_tags(out, en, kw):
    import wrapsim
    kwset = set(kw)

    def is_kw_tag(tag):
        return tag[1:-1].replace("_", " ") in kwset

    moved = dropped = 0
    for ids in sign_blocks(en):
        ids = [k for k in ids if k in out]
        if not ids:
            continue
        tags = []
        for k in ids:
            t = out[k]
            # 前に作った「 !(A)(B)」の行も集め直す (何度実行しても同じ結果にする)
            m = re.search(r"\s+!((?:\([^()]+\))+)(?=(\s+[!%&\]@]\S*)*$)", t)
            if m and all(is_kw_tag(x) for x in TAG.findall(m.group(1))):
                tags += TAG.findall(m.group(1))
                t = t[:m.start()] + t[m.end():]
            for x in TAG.findall(t):
                if is_kw_tag(x):
                    tags.append(x)
                    t = t.replace(x, "", 1)
            out[k] = t
        if not tags:
            continue
        tags = list(dict.fromkeys(tags))
        if wrapsim.width("".join(tags).replace("_", " ")) > SIGN_ALL:
            keep = []
            for x in tags:
                if wrapsim.width("".join(keep + [x]).replace("_", " ")) <= SIGN_FIT:
                    keep.append(x)
            dropped += len(tags) - len(keep)
            tags = keep
        if not tags:
            continue
        last = ids[-1]
        t = out[last]
        m = TRAIL_CTRL.search(t)
        cut = m.start() if m else len(t)
        out[last] = t[:cut] + " !" + "".join(tags) + t[cut:]
        moved += 1
    return moved, dropped


def cmd_annotate():
    en = batch.load_en()
    lines = batch.OUT.read_text(encoding="utf-8").splitlines()
    ja = {}
    for l in lines:
        if "\t" in l and not l.startswith("#"):
            k, t = l.split("\t", 1)
            ja[int(k)] = t
    new = annotate(ja, en, report=True)
    out = []
    for l in lines:
        if "\t" in l and not l.startswith("#"):
            k = int(l.split("\t", 1)[0])
            out.append(f"{k}\t{new[k]}")
        else:
            out.append(l)
    with batch.OUT.open("w", encoding="utf-8", newline="\n") as f:   # batch.py merge と同じ LF
        f.write("\n".join(out) + "\n")


def cmd_stats():
    en = batch.load_en()
    ja = batch.load(batch.OUT)
    ja.pop("__bad__", None)
    annotate(ja, en, report=True)


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "stats"
    {"extract": cmd_extract, "annotate": cmd_annotate, "stats": cmd_stats}[cmd]()
