"""Wizardry 6 日本語化プロジェクト (../Wizardry6/JPN_PATCH) の訳語と、こちらの訳を突き合わせる。

1. 同じ ID で英語原文が同じメッセージ (UI・種族・職業・スキル・呪文など) の訳の違い
2. Wiz6 の用語表 (glossary.tsv, parts/spells_ui2.tsv, parts/terms_s*.tsv, scen_ja.txt) の語が
   こちらの原文に出てくるとき、訳文に Wiz6 の訳語が使われているか

使い方: python tools/wiz6cmp.py [出力先.md]
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402

W6 = batch.MOD.parent.parent / "Wizardry6" / "JPN_PATCH" / "trans"


def norm(t):
    return re.sub(r"\s+", " ", re.sub(r"\{..\}|\\x..", "", t)).strip().upper()


def load_w6_msgs():
    en, ja = {}, {}
    for line in (W6 / "msg_en.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if "\t" in line:
            k, t = line.split("\t", 1)
            if k.isdigit():
                en[int(k)] = t
    for f in sorted((W6 / "parts").glob("ja_*.txt")):
        for line in f.read_text(encoding="utf-8").splitlines():
            p = line.split("\t")
            if len(p) >= 3 and p[0].isdigit():
                ja[int(p[0])] = p[2]
    return en, ja


def load_w6_terms():
    terms = {}

    def add(e, j, src):
        e = e.strip()
        j = j.strip()
        if e and j and not e.startswith("#"):
            terms.setdefault(e.upper(), (j, src))

    for line in (W6 / "glossary.tsv").read_text(encoding="utf-8").splitlines():
        p = line.split("\t")
        if len(p) >= 2:
            add(p[0], p[1], "glossary")
    for line in (W6 / "parts" / "spells_ui2.tsv").read_text(encoding="utf-8").splitlines():
        p = line.split("\t")
        if len(p) >= 2:
            add(p[0], p[1], "spells")
    for f in sorted((W6 / "parts").glob("terms_s*.tsv")):
        for line in f.read_text(encoding="utf-8").splitlines():
            p = line.split("\t")
            if len(p) >= 2:
                add(p[0], p[1], f.stem)
    se = dict(l.split("\t", 1) for l in (W6 / "scen_en.txt").read_text(encoding="utf-8").splitlines() if "\t" in l)
    sj = dict(l.split("\t", 1) for l in (W6 / "scen_ja.txt").read_text(encoding="utf-8").splitlines() if "\t" in l)
    for k, e in se.items():
        if k in sj and re.search(r"[A-Za-z]{3}", e):
            add(e, sj[k], "scen")
    return terms


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    en7 = batch.load_en()
    ja7 = batch.load(batch.OUT)
    ja7.pop("__bad__", None)
    en6, ja6 = load_w6_msgs()
    lines = ["# Wizardry 6 日本語化との訳語の違い", ""]

    # 1. 同じ ID・同じ原文
    lines += ["## 1. 同じ ID で原文が同じメッセージ", "", "| ID | 原文 | Wiz7 (こちら) | Wiz6 |", "|---|---|---|---|"]
    same = diff = 0
    for k in sorted(en7):
        if k in en6 and k in ja6 and k in ja7 and norm(en7[k]) == norm(en6[k]) and re.search(r"[A-Za-z]{2}", en7[k]):
            same += 1
            a, b = ja7[k].strip(), ja6[k].strip().replace("_", " ").strip()
            if a != b:
                diff += 1
                lines.append(f"| {k} | {en7[k].strip()} | {a} | {b} |")
    lines.insert(2, f"同じ原文 {same} 件のうち訳が違うもの {diff} 件。")
    lines.append("")

    # 2. 用語
    terms = load_w6_terms()
    lines += ["## 2. Wiz6 の用語がこちらの原文に出る箇所", "",
              "| 原文 | Wiz6 の訳語 | 出典 | 出現 | うち Wiz6 訳語あり | こちらの訳の例 |", "|---|---|---|---|---|---|"]
    rows = []
    for e, (j, src) in terms.items():
        if len(e) < 3:
            continue
        pat = re.compile(r"(?<![A-Za-z])" + re.escape(e) + r"(?![A-Za-z])", re.I)
        hits = [k for k, t in en7.items() if k in ja7 and pat.search(t) and not batch.is_keyword(t)]
        if not hits:
            continue
        ok = [k for k in hits if j in ja7[k]]
        if len(ok) == len(hits):
            continue
        ex = next((ja7[k] for k in hits if j not in ja7[k]), "")
        rows.append((len(hits) - len(ok), e, j, src, len(hits), len(ok), ex[:40]))
    rows.sort(key=lambda r: (-r[0], r[1]))
    for miss, e, j, src, n, ok, ex in rows:
        lines.append(f"| {e} | {j} | {src} | {n} | {ok} | {ex} |")
    text = "\n".join(lines) + "\n"
    if out:
        out.write_text(text, encoding="utf-8")
        print(f"{out}: 同ID差分 {diff}/{same}, 用語差分 {len(rows)}")
    else:
        print(text)


if __name__ == "__main__":
    main()
