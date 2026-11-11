"""MSG.HDR / MSG.GLD から全メッセージを抽出し、翻訳用テキストを書き出す。

出力: translation/msg_en.txt  (書式は msg_ja.txt と同じ: ID<TAB>本文, 制御バイトは {xx})

MSG.HDR: int16 件数, 続いて 7 バイトのレコード
    int16 先頭ID, uint16 ブロック内オフセット, uint8 追加件数, int16 ブロック番号(1KB単位)
MSG.GLD: 各メッセージは Pascal 文字列 (長さ1バイト + 本文)。
    レコードの位置から (追加件数+1) 個が連続して並ぶ。
"""
import struct
from pathlib import Path

MOD = Path(__file__).resolve().parent.parent
GAME = MOD.parent / "wizardry7gold"


def read_messages():
    hdr = (GAME / "MSG.HDR").read_bytes()
    gld = (GAME / "MSG.GLD").read_bytes()
    n = struct.unpack("<h", hdr[:2])[0]
    msgs = {}
    for i in range(n):
        first, off, extra, blk = struct.unpack("<hHBh", hdr[2 + i * 7: 9 + i * 7])
        p = blk * 1024 + off
        for k in range(extra + 1):
            ln = gld[p]
            msgs[first + k] = gld[p + 1: p + 1 + ln]
            p += 1 + ln
    return msgs


def escape(b: bytes) -> str:
    return "".join(chr(c) if 32 <= c < 127 and c != ord("{") else "{%02x}" % c for c in b)


def main():
    msgs = read_messages()
    out = MOD / "translation" / "msg_en.txt"
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# Wizardry 7 Gold MSG.GLD 原文 (自動抽出)\n")
        for k in sorted(msgs):
            f.write(f"{k}\t{escape(msgs[k])}\n")
    print(f"{len(msgs)} messages -> {out}")


if __name__ == "__main__":
    main()
