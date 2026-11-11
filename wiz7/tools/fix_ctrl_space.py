"""語の途中に紛れた制御記号の前に空白を入れる。

ゲームの本文表示 (Gold: 0x47b210) は、制御記号 (@ ! & % ] ~) を語頭 (空白の直後かメッセージの先頭) でしか
解釈しない。訳文で「……@次の文」のように空白なしで続くと、記号がそのまま表示される。
原文でその記号が語頭にあるメッセージについて、訳文の該当箇所の前に空白を入れる
(全角どうしの間の空白は DLL が詰めるので、見た目は変わらない)。

  python tools/fix_ctrl_space.py         一覧だけ
  python tools/fix_ctrl_space.py --fix   translation/ja/*.txt を直す
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import batch  # noqa: E402

CT = "@!&%]~"
TOKEN = re.compile(r"\{[0-9a-fA-F]{2}\}|.", re.S)


def tokens(t):
    return TOKEN.findall(t)


def fix_text(t, en_t):
    tk = tokens(t)
    out = []
    i = 0
    at_word_start = True
    changed = False
    while i < len(tk):
        c = tk[i]
        if c == " ":
            out.append(c)
            at_word_start = True
            i += 1
            continue
        if c in CT:
            # 制御記号の並び (~ は次の 1 字が引数)
            j = i
            while j < len(tk) and tk[j] in CT:
                j += 2 if tk[j] == "~" else 1
            run = tk[i:j]
            # 直前が全角のときだけ (023@41A2 のような英数字の中の記号は原文どおり)
            prev = out[-1] if out else ""
            if not at_word_start and len(prev) == 1 and ord(prev) >= 0x80 and \
                    re.search(r"(^| )" + re.escape(c), en_t):
                out.append(" ")
                changed = True
            out.extend(run)
            i = j
            at_word_start = False
            continue
        out.append(c)
        at_word_start = False
        i += 1
    return "".join(out), changed


def main():
    fix = "--fix" in sys.argv
    en = batch.load_en()
    n = 0
    for f in sorted(batch.JA_DIR.glob("*.txt")):
        lines = f.read_text(encoding="utf-8").split("\n")
        dirty = False
        for i, line in enumerate(lines):
            if "\t" not in line or line.startswith("#"):
                continue
            k, t = line.split("\t", 1)
            if not k.isdigit() or int(k) not in en or int(k) < 8000 or batch.is_keyword(en[int(k)]):
                continue
            new, ch = fix_text(t, en[int(k)])
            if ch:
                n += 1
                print(f"{k}\t{t}\n\t→ {new}")
                lines[i] = f"{k}\t{new}"
                dirty = True
        if dirty and fix:
            f.write_text("\n".join(lines), encoding="utf-8")
    print(f"{n} 件{'を直した' if fix else ' (--fix で直す)'}")


if __name__ == "__main__":
    main()
