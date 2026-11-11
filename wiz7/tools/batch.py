"""翻訳作業用ツール。

  python tools/batch.py split [行数]     未訳メッセージをブロック単位でバッチに分割 -> translation/work/en/bNNN.txt
  python tools/batch.py check FILE...    訳文ファイルを検証 (FILE は translation/ja/bNNN.txt など)
  python tools/batch.py merge            translation/ja/*.txt を結合して translation/msg_ja.txt を生成
  python tools/batch.py status           進捗表示

訳文ファイルの書式は msg_ja.txt と同じ (ID<TAB>訳文、# で始まる行はコメント)。
原文は translation/msg_en.txt (tools/extract_msgs.py で生成)。
"""
import re
import sys
from pathlib import Path

MOD = Path(__file__).resolve().parent.parent
TR = MOD / "translation"
EN_FILE = TR / "msg_en.txt"
JA_DIR = TR / "ja"
WORK_EN = TR / "work" / "en"
OUT = TR / "msg_ja.txt"

# 訳さない ID (glossary.md の「訳さないもの」)
SKIP_RANGES = [(454, 454), (456, 456), (1800, 1810), (2500, 2599), (2900, 2999), (7000, 7199)]
BODY_LIMIT = 160    # 本文 1 メッセージの上限バイト数 (内部エンコード後)。Gold の物語表示は 256 バイト枠。
                    # 旧来の表示 (0x40f176, 行枠 128 バイト) は DLL が行枠の残りに切り詰める
HARD_LIMIT = 250    # どのメッセージでも超えてはいけない上限
CTRL = "@[&%]~"     # 個数を保持すべき制御記号
VARS = "^$#"        # 置換変数


def load(path):
    d = {}
    for ln, line in enumerate(Path(path).read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            d.setdefault("__bad__", []).append((ln, line))
            continue
        i, t = line.split("\t", 1)
        try:
            d[int(i)] = t
        except ValueError:
            d.setdefault("__bad__", []).append((ln, line))
    return d


def load_en():
    return load(EN_FILE)


def skipped(k, t):
    if any(a <= k <= b for a, b in SKIP_RANGES):
        return True
    raw = re.sub(r"\{..\}", "", t)
    if not re.search(r"[A-Za-z0-9]", raw):
        return True                         # 空・空白・記号/制御バイトのみ
    if re.fullmatch(r"[a-z_]+\}?", raw):    # party_options, cast_on_who} など
        return True
    return is_keyword(t)


def is_keyword(t):
    """会話で入力語と照合するキーワード見出し (英語のまま残す)。
    例: !NEW CITY@  {1f}OUR PARTY@  "GUILD%  BARLONE^0'  {02}NO/{03}YE/{03}SURE/"""
    raw = re.sub(r"\{..\}", "", t)
    if re.search(r"[a-z]", raw) or not re.search(r"[A-Z]", raw) or len(raw) > 40:
        return False
    if t.startswith("{") and "/" in raw:
        return True
    if re.search(r"[A-Z0-9'.)]\s*[@%]$", t):
        return True
    if re.search(r"\^\d", t):
        return True
    return False


def blocks(en):
    """連続 ID の並びを 1 ブロックとして返す。"""
    ks = sorted(en)
    out, cur = [], [ks[0]]
    for k in ks[1:]:
        if k == cur[-1] + 1:
            cur.append(k)
        else:
            out.append(cur)
            cur = [k]
    out.append(cur)
    return out


def is_body(en, k):
    """本文ブロック (連結されて折り返される文章) とみなすか。"""
    return bool(re.search(r"[a-z]{2}", en[k])) and len(en[k]) > 12


def enc_len(t):
    """DLL 内部エンコード後のバイト数。ASCII=1, 全角=2, {xx}=1。"""
    t2 = re.sub(r"\{[0-9a-fA-F]{2}\}", "\x01", t)
    return sum(1 if ord(c) < 128 else 2 for c in t2)


def disp_width(t):
    """表示幅 (英語 1 文字 = 1)。全角 = 1 (font0 基準)。"""
    t2 = re.sub(r"\{..\}", "", t)
    return len(t2)


_CAPS = None


def buf_caps():
    """tools/msgbuf_scan.py が求めた、ID ごとの書き込み先バッファ容量 (終端 NUL 込み)。"""
    global _CAPS
    if _CAPS is None:
        _CAPS = {}
        f = TR / "msgbuf.tsv"
        if f.exists():
            for line in f.read_text(encoding="utf-8").splitlines():
                if line and not line.startswith("#"):
                    k, v = line.split("\t")
                    _CAPS[int(k)] = int(v)
    return _CAPS


_FIELD = {}


def field_width(en, k):
    """同じブロック (同じ欄に並ぶ一覧とみなす) の原文の最大幅。"""
    if not _FIELD:
        for blk in blocks(en):
            w = max(disp_width(en[x]) for x in blk)
            for x in blk:
                _FIELD[x] = w
    return _FIELD.get(k, disp_width(en[k]))


def lead_ctrl(t):
    """行頭 (先頭の空白は無視) の制御記号の並び。全角に置き換えた！＃＠なども半角に戻して比較できるよう検出する。"""
    t = t.lstrip(" ")
    # " は本文の制御記号ではない (0x40ebee の表にない) ので含めない。「 」に置き換えてよい
    m = re.match(r'(?:\{[0-9a-fA-F]{2}\}|[!#@\[\]&%~！＃＠＆％］［～])*', t)
    return m.group(0)


def lead_count(t, ch):
    """語頭 (行頭または空白の直後) にある ch の数。"""
    return len(re.findall(r"(?:^|(?<= ))" + re.escape(ch), t))


def check_line(k, en_t, ja_t, en):
    errs, warns = [], []
    if ja_t.strip() == "":
        errs.append("訳文が空")
    if re.findall(r"\{..\}", en_t) != re.findall(r"\{..\}", ja_t):
        errs.append("制御バイト {xx} の並びが原文と違う: %s -> %s"
                    % ("".join(re.findall(r"\{..\}", en_t)), "".join(re.findall(r"\{..\}", ja_t))))
    body = is_body(en, k)
    for c in VARS:
        a, b = en_t.count(c), ja_t.count(c)
        if a != b:
            errs.append(f"変数 '{c}' の数が原文 {a} 個 / 訳 {b} 個")
    sym = []
    for c in CTRL:
        a, b = en_t.count(c), ja_t.count(c)
        if a != b:
            sym.append(f"記号 '{c}' の数が原文 {a} 個 / 訳 {b} 個")
    lc = lead_ctrl(en_t)
    a = lead_count(en_t, "!") + lc.count("!") - (1 if lc.startswith("!") else 0)
    b = ja_t.count("!")
    if a != b:
        sym.append(f"半角 '!' は制御記号。原文の制御 '!' は {a} 個、訳に {b} 個 (感嘆は全角 ！ を使う)")
    a, b = lead_count(en_t, "?"), ja_t.count("?")
    if a != b:
        sym.append(f"半角 '?' が {b} 個 (原文の行頭 ? は {a} 個)。疑問符は全角 ？ を使う")
    (errs if body else warns).extend(sym)
    pe, pj = lead_ctrl(en_t), lead_ctrl(ja_t)
    if pe != pj:
        errs.append(f"行頭の制御記号の並びが原文と違う: '{pe}' -> '{pj}' (全角にせず原文どおり半角で残す)")
    if "{" in re.sub(r"\{[0-9a-fA-F]{2}\}", "", ja_t) or "}" in re.sub(r"\{[0-9a-fA-F]{2}\}", "", ja_t):
        errs.append("{ } は制御バイト専用")
    if "～" in ja_t:
        errs.append("全角チルダ ～ は MS ゴシックで化けることがある。〜 を使う")
    try:
        ja_t.encode("cp932")
    except UnicodeEncodeError as e:
        errs.append(f"cp932 に無い文字 '{ja_t[e.start]}' (MS ゴシックで表示できない可能性)")
    n = enc_len(ja_t)
    if n > HARD_LIMIT:
        errs.append(f"長すぎる ({n} バイト > {HARD_LIMIT})")
    cap = buf_caps().get(k)
    if cap and n + 1 > cap:
        errs.append(f"ゲーム側のバッファは {cap - 1} バイト (全角 {(cap - 1) // 2} 字) まで。現在 {n} バイト。短くする")
    if body:
        if n > BODY_LIMIT:
            errs.append(f"本文 1 メッセージは全角 {BODY_LIMIT // 2} 字まで (現在 {n / 2:.0f} 字相当)")
    else:
        w, ew = disp_width(ja_t), field_width(en, k)
        if w > ew and w > 3:
            errs.append(f"UI 文字列が原文より長い (全角 {w} 字 > 原文 {ew} 字)。短く言い換える")
        elif w > ew * 0.6 + 1.5:
            warns.append(f"UI 文字列がやや長い (全角 {w} 字 / 原文 {ew} 字、目安 {int(ew * 0.6 + 1.5)} 字以下)")
    if re.search(r"[A-Za-z]{4,}", re.sub(r"\{..\}", "", ja_t)) and re.search(r"[a-z]{3}", ja_t):
        warns.append("英単語が残っている (固有のキーワードなら可)")
    return errs, warns


def cmd_split(size=150):
    en = load_en()
    done = set()
    for f in sorted(JA_DIR.glob("*.txt")):
        done |= {k for k in load(f) if isinstance(k, int)}
    WORK_EN.mkdir(parents=True, exist_ok=True)
    for f in WORK_EN.glob("b*.txt"):
        f.unlink()
    batches, cur = [], []
    for blk in blocks(en):
        todo = [k for k in blk if k not in done and not skipped(k, en[k])]
        if not todo:
            continue
        if cur and len(cur) + len(todo) > size:
            batches.append(cur)
            cur = []
        cur.extend(todo)
    if cur:
        batches.append(cur)
    for n, b in enumerate(batches, 1):
        with (WORK_EN / f"b{n:03d}.txt").open("w", encoding="utf-8", newline="\n") as f:
            f.write(f"# batch b{n:03d}: {len(b)} messages, ID {b[0]}-{b[-1]}\n")
            f.write("# 空行はブロック (連続 ID) の区切り。ブロック内は 1 つの文章として流し込まれる。\n")
            prev, kind = None, None
            for k in b:
                if prev is not None and k != prev + 1:
                    f.write("\n")
                    kind = None
                body = is_body(en, k)
                if body != kind:
                    f.write("# [本文: ブロック内は続けて流し込まれる, 1 件 全角34字まで]\n" if body
                            else "# [UI: 固定欄。短く]\n")
                    kind = body
                f.write(f"{k}\t{en[k]}\n")
                prev = k
    total = sum(len(b) for b in batches)
    print(f"{len(batches)} batches, {total} messages -> {WORK_EN}")


def cmd_check(files):
    en = load_en()
    bad_total = 0
    for f in files:
        p = Path(f)
        ja = load(p)
        errs = 0
        for ln, line in ja.pop("__bad__", []):
            print(f"{p.name}:{ln}: 書式エラー (ID<TAB>訳文 になっていない): {line[:40]}")
            errs += 1
        # 対応する原文バッチがあれば、抜けを報告
        src = WORK_EN / p.name
        if src.exists():
            want = [k for k in load(src) if isinstance(k, int)]
            missing = [k for k in want if k not in ja]
            if missing:
                print(f"{p.name}: 未訳 {len(missing)} 件: {missing[:20]}{' ...' if len(missing) > 20 else ''}")
                errs += len(missing)
        nw = 0
        for k, t in ja.items():
            if k not in en:
                print(f"{p.name}: {k}: 原文に無い ID")
                errs += 1
                continue
            if skipped(k, en[k]):
                print(f"{p.name}: {k}: [警告] この行は会話キーワード等で英語のまま残す。訳さなくてよい (出力しても結合時に除外される): {en[k]}")
                nw += 1
                continue
            e, w = check_line(k, en[k], t, en)
            for x in e:
                print(f"{p.name}: {k}: [エラー] {x}\n    原文: {en[k]}\n    訳文: {t}")
            for x in w:
                print(f"{p.name}: {k}: [警告] {x}\n    原文: {en[k]}\n    訳文: {t}")
            errs += len(e)
            nw += len(w)
        print(f"{p.name}: {len(ja)} 件, エラー {errs}, 警告 {nw}")
        bad_total += errs
    return 1 if bad_total else 0


def cmd_merge():
    en = load_en()
    merged, origin = {}, {}
    files = sorted(JA_DIR.glob("*.txt"))
    for f in files:
        for k, t in load(f).items():
            if not isinstance(k, int):
                continue
            if k in merged and merged[k] != t:
                print(f"重複 {k}: {origin[k]} と {f.name} -> 後者を採用")
            merged[k] = t
            origin[k] = f.name
    nerr = 0
    # 会話キーワードの書き添え (tools/keywords.py)。検査の後で付けるので、訳文の検査には影響しない
    try:
        import keywords
        if keywords.KW_FILE.exists():
            ok = {k: t for k, t in merged.items() if k in en and not skipped(k, en[k])}
            annotated = keywords.annotate(ok, en, report=True)
        else:
            annotated = {}
    except Exception as ex:          # 書き添えに失敗しても結合は続ける
        print(f"キーワードの書き添えに失敗: {ex}")
        annotated = {}
    with OUT.open("w", encoding="utf-8", newline="\n") as fo:
        fo.write("# Wizardry 7 Gold 日本語化MOD 翻訳データ (UTF-8)\n")
        fo.write("# 自動生成: tools/batch.py merge  (編集は translation/ja/*.txt で行う)\n")
        for k in sorted(merged):
            if k in en and skipped(k, en[k]):
                continue
            if k in en:
                e, _ = check_line(k, en[k], merged[k], en)
                if e:
                    nerr += 1
                    print(f"除外 {k} ({origin[k]}): {e[0]}")
                    continue
            fo.write(f"{k}\t{annotated.get(k, merged[k])}\n")
    print(f"{len(merged) - nerr} 件 -> {OUT} (エラーで除外 {nerr})")


def cmd_status():
    en = load_en()
    target = [k for k in en if not skipped(k, en[k])]
    done = set()
    for f in JA_DIR.glob("*.txt"):
        done |= {k for k in load(f) if isinstance(k, int)}
    n = len([k for k in target if k in done])
    print(f"翻訳対象 {len(target)} 件中 {n} 件 ({n * 100 / len(target):.1f}%)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 0
    sys.stdout.reconfigure(encoding="utf-8")
    c = sys.argv[1]
    if c == "split":
        cmd_split(int(sys.argv[2]) if len(sys.argv) > 2 else 150)
    elif c == "check":
        return cmd_check(sys.argv[2:])
    elif c == "merge":
        cmd_merge()
    elif c == "status":
        cmd_status()
    else:
        print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main())
