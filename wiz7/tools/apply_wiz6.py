"""Wizardry 6 日本語化 (../Wizardry6/JPN_PATCH) の訳語に、こちらの用語をそろえる。

1. 用語の ID (種族・職業・能力値・称号・スキル・呪文・系統・品目分類) は、
   Wiz6 に同じ ID・同じ原文があればその訳にする。呪文は英語名で Wiz6 の呪文表を引く。
   Wiz6 に無い Wiz7 の呪文は WIZ7_ONLY_SPELLS (Wiz6 と同じ意味訳の流儀で決めた名前)。
2. 本文中の旧訳語は、そのメッセージの原文に該当する英語があるときだけ新しい訳語に置き換える。

translation/ja/*.txt を直接書き換える。使い方: python tools/apply_wiz6.py [--dry]
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402
import wiz6cmp  # noqa: E402

WIZ7_ONLY_SPELLS = {
    "PSIONIC FIRES": "念の炎",
    "DAZZLING LIGHTS": "まばゆい光",
    "RESTFULL": "安息",
    "SUPERMAN": "超人",
    "DRAINING CLOUD": "吸精の雲",
    "CURE DISEASE": "病気治療",
    "SHRILL SOUND": "金切り音",
    "DEATH CLOUD": "死の雲",
    "WHIPPING ROCKS": "石つぶて",
    "CRUSH": "圧壊",
    "CONFUSION": "混乱",
    "WATCHBELLS": "警鐘",
    "SPOOKS": "怪異",
    "LOCATE PERSON": "人物探知",
    "HEALTHFULL": "健全",
    "ZAP UNDEAD": "不死撃滅",
    "RECHARGE": "再充填",
    "HELPFOOD": "補給食",
    "MAGICFOOD": "魔法食",
    "MAP": "地図",
    "RESTORE LIFE": "生命回復",
    "INVISIBILITY": "透明化",
}

# 同じ ID・同じ原文なら Wiz6 の訳をそのまま使う範囲 (用語の表)
TERM_RANGES = [(100, 140), (200, 215), (600, 603), (800, 939), (1550, 1566),
               (3753, 3760), (3950, 3955), (5050, 5060), (5500, 5540)]

# 本文中の置き換え: (原文の英語, 旧訳, 新訳)。原文にその英語があるメッセージだけ
TEXT_TERMS = [
    ("LORD", "ロード", "君主"),
    ("SPEED", "素早さ", "速さ"),
    ("PERSONALITY", "個性", "魅力"),
    ("KARMA", "カルマ", "業"),
    ("COSMIC FORGE", "コズミック・フォージ", "魔筆"),
    ("COSMIC FORGE", "コズミックフォージ", "魔筆"),
]


def clean_w6(t):
    t = re.sub(r"^\{[A-Z]\}", "", t.strip())     # Wiz6 のホットキー表記 {E} など
    return t.replace("_", " ").strip()


def pad3(t):
    """能力値の略称 (204〜211) は 3 文字幅にそろえる"""
    t = t.rstrip(" ")
    return t + " " * max(0, 3 - len(t))


def main():
    dry = "--dry" in sys.argv
    en7 = batch.load_en()
    en6, ja6 = wiz6cmp.load_w6_msgs()
    w6sp = {}
    for line in (wiz6cmp.W6 / "parts" / "spells_ui2.tsv").read_text(encoding="utf-8").splitlines():
        p = line.split("\t")
        if len(p) >= 2:
            w6sp[p[0].strip().upper()] = p[1].strip()
    spells = {}
    for k in range(4000, 4102):
        if k in en7:
            e = en7[k].strip().upper()
            spells[e] = w6sp.get(e) or WIZ7_ONLY_SPELLS[e]

    # ID ごとの新しい訳
    new_by_id = {}
    for a, b in TERM_RANGES:
        for k in range(a, b + 1):
            if k in en7 and k in en6 and k in ja6 and wiz6cmp.norm(en7[k]) == wiz6cmp.norm(en6[k]):
                new_by_id[k] = clean_w6(ja6[k])
    for k in range(4000, 4102):
        if k in en7:
            new_by_id[k] = spells[en7[k].strip().upper()]
    for k in range(204, 212):
        if k in new_by_id:
            new_by_id[k] = pad3(new_by_id[k])

    # 本文の置き換え表: 呪文 (旧訳 = 現在の 4000〜 の訳)
    cur = batch.load(batch.OUT)
    text_terms = list(TEXT_TERMS)
    for k in range(4000, 4102):
        if k in en7 and cur.get(k) and cur[k] != new_by_id[k]:
            text_terms.append((en7[k].strip(), cur[k].strip(), new_by_id[k]))
    # 長い旧訳から置き換える (ポイズンガス を ポイズン より先に)
    text_terms.sort(key=lambda x: -len(x[1]))

    changed_id = changed_text = 0
    for f in sorted(batch.JA_DIR.glob("*.txt")):
        lines = f.read_text(encoding="utf-8").split("\n")
        dirty = False
        for i, line in enumerate(lines):
            if "\t" not in line or line.startswith("#"):
                continue
            ks, t = line.split("\t", 1)
            if not ks.isdigit():
                continue
            k = int(ks)
            new = t
            if k in new_by_id:
                new = new_by_id[k]
            elif k in en7 and not batch.is_keyword(en7[k]):
                for e, old, nw in text_terms:
                    if old in new and re.search(r"(?<![A-Za-z])" + re.escape(e) + r"(?![A-Za-z])", en7[k], re.I):
                        # カタカナ語の一部 (デススター の デス など) は置き換えない
                        new = re.sub(r"(?<![ァ-ヶー・])" + re.escape(old) + r"(?![ァ-ヶー・])", nw, new)
            if new != t:
                if k in new_by_id:
                    changed_id += 1
                else:
                    changed_text += 1
                if dry:
                    print(f"{k}\t{t}\t→\t{new}")
                lines[i] = f"{ks}\t{new}"
                dirty = True
        if dirty and not dry:
            f.write_text("\n".join(lines), encoding="utf-8")
    print(f"用語の ID: {changed_id} 件, 本文: {changed_text} 件 {'(dry run)' if dry else ''}")


if __name__ == "__main__":
    main()
