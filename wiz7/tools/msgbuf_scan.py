"""msg_fetch (0x467e33) の全呼び出し箇所について、書き込み先バッファの容量を推定する。

ゲームはメッセージを英語原文に合わせた固定長のスタック上バッファに読み込むことがあり、
長い訳文はそこからあふれてスタックを壊す。ここで求めた容量を DLL が使って切り詰める。

出力:
  src/msgbuf_sites.h      { 戻りアドレス, 容量 } の表 (DLL が実行時に参照)
  translation/msgbuf.tsv  ID ごとの容量 (チェッカーが参照。ID が静的に分かる箇所のみ)

使い方: python tools/msgbuf_scan.py [wizard_full.asm]
  wizard_full.asm は tools/fulldis.py で生成した全体の逆アセンブル。
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

MOD = Path(__file__).resolve().parent.parent
MSG_FETCH = 0x467e33
LINE = re.compile(r"^([0-9a-f]{8})  (.*)$")


def load(path):
    out = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE.match(line)
        if m:
            out.append((int(m.group(1), 16), m.group(2).strip()))
    return out


def func_bounds(ins, i):
    """i を含む関数の開始・終了インデックス (push ebp; mov ebp, esp で区切る)。"""
    s = i
    while s > 0 and not (ins[s][1] == "push ebp" and ins[s + 1][1] == "mov ebp, esp"):
        s -= 1
    e = i
    while e + 1 < len(ins) and not (ins[e + 1][1] == "push ebp" and e + 2 < len(ins) and ins[e + 2][1] == "mov ebp, esp"):
        e += 1
    return s, e


def frame_offsets(ins, s, e):
    """関数内で使われる [ebp - X] の X の集合と、確保サイズ。"""
    offs = set()
    size = 0
    for _, t in ins[s:e + 1]:
        # 変数とみなすのは dword/word アクセスか lea の参照のみ。
        # byte アクセス・添字付き・奇数オフセットはバッファ途中の要素アクセスとみなす。
        for m in re.finditer(r"(?:(?:dword|word) ptr |lea e[a-d]x, )\[ebp - 0x([0-9a-f]+)\]", t):
            o = int(m.group(1), 16)
            if o % 2 == 0:
                offs.add(o)
        m = re.match(r"sub esp, 0x([0-9a-f]+)$", t)
        if m and not size:
            size = int(m.group(1), 16)
    return offs, size


_CALLERS = None


def callers_of(ins, func_addr):
    global _CALLERS
    if _CALLERS is None:
        _CALLERS = defaultdict(list)
        for k, (_, t) in enumerate(ins):
            m = re.match(r"call 0x([0-9a-f]+)$", t)
            if m:
                _CALLERS[int(m.group(1), 16)].append(k)
    return _CALLERS.get(func_addr, [])


def push_args(ins, k, n):
    """call (インデックス k) の直前の push を n 個、第 1 引数から順に返す。"""
    out = []
    j = k - 1
    while j > k - 40 and len(out) < n:
        if ins[j][1].startswith("push "):
            out.append(ins[j][1][5:])
        j -= 1
    return out


def arg_ids(ins, i, pid):
    """id が関数の引数 (+定数 / +ループ変数) から作られている場合、呼び出し元の即値から ID を求める。"""
    win = [t for _, t in ins[max(pid - 6, 0):pid]]
    arg = add = None
    loop = False
    for t in win:
        m = re.match(r"(?:movsx eax, word|mov eax, dword) ptr \[ebp \+ (0x[0-9a-f]+|\d+)\]$", t)
        if m:
            arg, add, loop = (int(m.group(1), 0) - 8) // 4, 0, False
        elif arg is not None:
            if t == "inc eax":
                add += 1
            elif re.match(r"add eax, (0x[0-9a-f]+|\d+)$", t):
                add += int(re.match(r"add eax, (0x[0-9a-f]+|\d+)$", t).group(1), 0)
            elif re.match(r"add eax, ecx$", t):
                loop = True            # [ebp+8] + 添字
    if arg is None:
        return []
    s, _ = func_bounds(ins, i)
    ids = set()
    for k in callers_of(ins, ins[s][0]):
        args = push_args(ins, k, arg + 1)
        if len(args) > arg:
            m = re.match(r"(0x[0-9a-f]+|\d+)$", args[arg])
            if m:
                base = int(m.group(1), 0) + add
                ids.update(range(base, base + (8 if loop else 1)))
    return sorted(ids)


def main():
    asm = sys.argv[1] if len(sys.argv) > 1 else None
    if not asm:
        print(__doc__)
        return 1
    ins = load(asm)
    en_len = {}
    for line in (MOD / "translation" / "msg_en.txt").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            k, t = line.split("\t", 1)
            en_len[int(k)] = len(re.sub(r"\{..\}", "x", t))
    sites = []           # (retaddr, cap or None, ids, note)
    id_cap = defaultdict(lambda: 9999)
    for i, (a, t) in enumerate(ins):
        if t != f"call 0x{MSG_FETCH:x}":
            continue
        ret = ins[i + 1][0]
        # 直前の 2 つの push: [i-1] が id、その前が dst
        pushes = []
        j = i - 1
        while j > i - 14 and len(pushes) < 2:
            if ins[j][1].startswith("push "):
                pushes.append(j)
            j -= 1
        if len(pushes) < 2:
            sites.append((ret, None, [], "push not found"))
            continue
        pid, pdst = pushes
        # id
        ids = []
        m = re.match(r"push (0x[0-9a-f]+|\d+)$", ins[pid][1])
        if m:
            ids = [int(m.group(1), 0)]
        else:
            ids = arg_ids(ins, i, pid)
        # dst
        cap, note = None, ""
        dst_t = ins[pdst][1]
        if dst_t == "push eax":
            # push eax の直前の lea を探す
            k = pdst - 1
            while k > pdst - 6 and not ins[k][1].startswith("lea eax"):
                k -= 1
            lt = ins[k][1]
            s, e = func_bounds(ins, i)
            offs, size = frame_offsets(ins, s, e)
            m1 = re.match(r"lea eax, \[ebp - 0x([0-9a-f]+)\]$", lt)
            m2 = re.match(r"lea eax, \[ebp \+ eax - 0x([0-9a-f]+)\]$", lt)
            if m1:
                x = int(m1.group(1), 16)
                above = [o for o in offs if o < x]
                cap = x - max(above) if above else x
                note = f"[ebp-0x{x:x}]"
            elif m2:
                x = int(m2.group(1), 16)
                # 配列: 添字計算から要素サイズを推定 (shl eax,2 / lea eax,[eax+eax*4] など)
                stride = None
                window = " ; ".join(t2 for _, t2 in ins[k - 6:k])
                if "lea eax, [eax + eax*4]" in window and "shl eax, 2" in window:
                    stride = 20
                elif re.search(r"shl eax, (\d)", window):
                    stride = 1 << int(re.search(r"shl eax, (\d)", window).group(1))
                above = [o for o in offs if o < x]
                room = x - max(above) if above else x
                cap = min(stride, room) if stride else room
                note = f"[ebp+i*{stride}-0x{x:x}]"
            else:
                note = "dst: " + lt
        elif re.match(r"push 0x[0-9a-f]+$", dst_t):
            note = "global " + dst_t[5:]
        else:
            note = "dst: " + dst_t
        sites.append((ret, cap, ids, note))
        if cap is not None:
            for x in ids:
                # 推定した ID 範囲が広すぎる場合の除外: 英語原文が入らない ID はこの箇所の対象ではない
                if x in en_len and en_len[x] + 1 > cap:
                    continue
                id_cap[x] = min(id_cap[x], cap)

    hdr = MOD / "src" / "msgbuf_sites.h"
    with hdr.open("w", encoding="utf-8", newline="\n") as f:
        f.write("/* 自動生成: tools/msgbuf_scan.py  msg_fetch 呼び出し箇所の戻りアドレスと書き込み先容量 */\n")
        f.write("static const struct { DWORD ret; unsigned short cap; } MSGBUF_SITES[] = {\n")
        for ret, cap, ids, note in sites:
            if cap is not None:
                f.write(f"    {{0x{ret:06x}, {cap}}},  /* {note} ids={ids} */\n")
        f.write("};\n")
    tsv = MOD / "translation" / "msgbuf.tsv"
    with tsv.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# ID\t書き込み先バッファの容量 (終端 NUL 込み)。tools/msgbuf_scan.py が生成\n")
        for k in sorted(id_cap):
            f.write(f"{k}\t{id_cap[k]}\n")
    n_stack = sum(1 for s in sites if s[1] is not None)
    print(f"{len(sites)} sites, stack dst {n_stack}, ids with known cap {len(id_cap)}")
    for ret, cap, ids, note in sites:
        if cap is not None and cap < 64:
            print(f"  ret {ret:06x} cap {cap:3d} {note} ids={ids}")
    unk = [s for s in sites if s[1] is None and not s[3].startswith("global")]
    for s in unk:
        print(f"  ?? ret {s[0]:06x} {s[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
