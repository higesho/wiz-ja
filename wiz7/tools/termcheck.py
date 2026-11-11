"""用語集 (glossary.md 5 章の表) の固有名詞が訳文で守られているかを調べる。

原文に英語の用語があるのに、訳文に用語集の訳語が無いメッセージを数え、
代わりに使われていそうなカタカナ語を挙げる (表記揺れの候補)。

使い方: python tools/termcheck.py [最小件数=1]
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402

GLOSS = batch.TR / "glossary.md"


def pairs():
    out = []
    sec = ""
    for line in GLOSS.read_text(encoding="utf-8").splitlines():
        if line.startswith("### "):
            sec = line[4:].split(" ")[0]
            continue
        if not sec.startswith("5-") or sec in ("5-1", "5-2", "5-3", "5-4"):
            continue
        if not line.startswith("|") or "---" in line:
            continue
        cols = [c.strip() for c in line.strip("|").split("|")]
        if len(cols) < 2 or cols[0] in ("原文",):
            continue
        en = [re.sub(r"\(.*?\)", "", x).strip() for x in cols[0].split(" / ")]
        ja = [re.sub(r"[（(].*?[)）]", "", x).strip() for x in cols[1].split(" / ")]
        if len(en) != len(ja):
            if len(ja) == 1:
                ja = ja * len(en)
            else:
                en, ja = en[:1], ja[:1]
        for e, j in zip(en, ja):
            if e and j and "..." not in e and "〜" not in j:
                out.append((sec, e, j))
    return out


def main():
    mincount = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    en = batch.load_en()
    ja = batch.load(batch.OUT)
    ja.pop("__bad__", None)
    # 長い用語を先に (Gorn King を Gorn より先に判定)
    ps = sorted(pairs(), key=lambda p: -len(p[1]))
    report = []
    for sec, e, j in ps:
        pat = re.compile(r"(?<![A-Za-z'])" + re.escape(e) + r"(?:'?s)?(?![A-Za-z])", re.I)
        hit = miss = 0
        alt = Counter()
        ids = []
        for k, t in en.items():
            if k not in ja or not pat.search(t):
                continue
            hit += 1
            if j in ja[k]:
                continue
            miss += 1
            ids.append(k)
            for w in re.findall(r"[ァ-ヶー・＝]{3,}", ja[k]):
                alt[w] += 1
        if miss >= mincount:
            report.append((miss, hit, sec, e, j, alt.most_common(4), ids[:6]))
    report.sort(key=lambda r: (-r[0], r[3]))
    print(f"用語 {len(ps)} 件中、訳語が使われていないメッセージがある用語: {len(report)}")
    for miss, hit, sec, e, j, alt, ids in report:
        a = ", ".join(f"{w}×{n}" for w, n in alt)
        print(f"{miss:4d}/{hit:<4d} {e} → {j}   候補: {a}   例: {ids}")


if __name__ == "__main__":
    main()
