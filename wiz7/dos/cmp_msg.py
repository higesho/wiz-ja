"""DOS 版 (DSAVANT/MSG.DBS) と Gold 版 (translation/msg_en.txt) の原文を比べる"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load(p):
    d = {}
    for l in open(p, encoding="utf-8"):
        if l.startswith("#") or "\t" not in l:
            continue
        i, t = l.rstrip("\n").split("\t", 1)
        d[int(i)] = t
    return d


def norm(t):
    return re.sub(r"\{([0-9a-f]{2})\}", lambda m: "\\x" + m.group(1), t)


if __name__ == "__main__":
    g = load(HERE.parent / "translation" / "msg_en.txt")
    s = load(HERE / "msg_dos_en.txt")
    diff = [i for i in g if i in s and norm(g[i]) != s[i]]
    print("差のある ID", len(diff), "(8000 未満", sum(1 for i in diff if i < 8000), ")")
    for i in diff[:60]:
        print(i, "\n G:", g[i][:170], "\n D:", s[i][:170])
    for i in sorted(set(s) - set(g)):
        print("DOS のみ", i, s[i])
