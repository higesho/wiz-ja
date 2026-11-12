"""DOS 版オーバーレイの「行の間隔」を調べる (日本語の小さい字が重なりそうな一覧を探す)
  python dos/survey_rows.py
  ・mov ax,K / imul ... / add ax,BASE  (行番号 x K + 基準位置) の K=5,6 の箇所
  ・描画位置の設定 (0x3850) を定数で呼ぶ所のうち、同じ x で y が 6 ずつ並ぶもの
"""
import collections
import re
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1] / "DSAVANT"
OVB = 0x5047
SETPOS = 0x3850


def target(o, i):
    return (OVB + i + 3 + struct.unpack("<h", o[i + 1:i + 3])[0]) & 0xFFFF


def main():
    for f in sorted(GAME.glob("*.OVR")):
        src = HERE / "original" / f.name
        o = (src if src.exists() else f).read_bytes()
        pit = collections.Counter()
        for m in re.finditer(rb"\xb8([\x05\x06])\x00\xf7\x6e(.)\x05(..)", o, re.S):
            pit[(m.group(1)[0], struct.unpack("<H", m.group(3))[0])] += 1
        ys = collections.defaultdict(set)
        for m in re.finditer(rb"\xb8(..)\x50\xb8(..)\x50\xe8(..)", o, re.S):
            if target(o, m.start() + 8) != SETPOS:
                continue
            ys[struct.unpack("<H", m.group(2))[0]].add(struct.unpack("<H", m.group(1))[0])
        runs = []
        for x, s in ys.items():
            s = sorted(s)
            run = [s[0]]
            for a, b in zip(s, s[1:]):
                if b - a == 6:
                    run.append(b)
                else:
                    if len(run) >= 3:
                        runs.append((x, run))
                    run = [b]
            if len(run) >= 3:
                runs.append((x, run))
        print(f.name, "行番号x5/6:", {f"{k}*i+{b:#x}": n for (k, b), n in sorted(pit.items())}, " 固定の6おき:", runs)


if __name__ == "__main__":
    main()
