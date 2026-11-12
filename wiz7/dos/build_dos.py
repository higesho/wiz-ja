"""DOS 版 (DSAVANT, Crusaders of the Dark Savant) の日本語化ファイルを作る

  python dos/build_dos.py            dos/out/ に作る
  python dos/build_dos.py --install  さらに ../DSAVANT に上書きする (初回に元のファイルを dos/original/ へ保管)

訳文は Gold 版と同じ translation/msg_ja.txt と translation/scen_ja.txt を使う (ID・名前欄の位置が同じ)。

変更するファイル:
  VGA.DRV       後ろに日本語表示モジュール (jpdrv.asm) と字形を足す。1 文字描画・位置設定の飛び先を差し替え
  DS.EXE        ドライバの領域を 64KB に広げる。文字列幅 (0x38ca) を日本語対応版への中継に差し替え
  VBASE/VMAZE/VPCVW/VTREA.OVR  物語の語分けを、日本語の折り返し単位で分ける処理に差し替え
  MSG.DBS / MSG.HDR / MISC.HDR  訳文 (ハフマン圧縮し直し)
  SCENARIO.DBS  品物・怪物・人物の名前
"""
import hashlib
import re
import shutil
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import jpenc      # noqa: E402
import msgtool    # noqa: E402

MOD = HERE.parent
GAME = MOD.parent / "DSAVANT"
ORIG = HERE / "original"
OUT = HERE / "out"
BDF = MOD / "fonts" / "shinonome" / "shnmk12.bdf"
MISAKI = MOD / "fonts" / "misaki" / "misaki_gothic.bdf"
SMALLH = 8               # この高さ以下のゲームのフォントでは、日本語を美咲 8x8 で描く

OVERLAYS = ["VBASE.OVR", "VMAZE.OVR", "VPCVW.OVR", "VTREA.OVR", "VPCMK.OVR", "VPCLV.OVR",
            "VDOPT.OVR", "VMNPC.OVR", "VPOPS.OVR", "VINIT.OVR", "VMELE.OVR", "VMEXE.OVR", "VMEXT.OVR"]
SPLIT_OVERLAYS = ["VBASE.OVR", "VMAZE.OVR", "VPCVW.OVR", "VTREA.OVR"]
FILES = ["DS.EXE", "VGA.DRV"] + OVERLAYS + ["MSG.DBS", "MSG.HDR", "MISC.HDR", "SCENARIO.DBS"]
PRISTINE = {"DS.EXE": "b458dc3212c7823a651a323325738090", "VGA.DRV": "04b435f5b2e13f2a94a8ab1e2627f27c",
            "MSG.DBS": "358bfda539881e049817bda492210567"}

OVB = 0x5047            # オーバーレイのコードの置き場所 (CS 内)
CHKSTK = 0x4452         # 関数の入口処理 (DS.EXE)
STRWIDTH = 0x38ca       # 文字列幅 (DS.EXE)。ここに 2 つの中継を置く
UNIT_STUB = STRWIDTH + 21
DRV_SEGVAR = 0x2d52     # DS.EXE が持つドライバのセグメント
DRV_LOAD = 0x100        # ドライバはセグメントの 0x100 から読まれる
MODF = 0x4900           # モジュールを置くドライバのファイル位置
MODR = MODF + DRV_LOAD
ENTRY = {"putc": MODR + 0x40, "setpos": MODR + 0x43, "width": MODR + 0x46, "unit": MODR + 0x49, "dlen": MODR + 0x4C}
DLEN_STUB = STRWIDTH + 42
STRLEN = 0x4AD7
VARS = dict(PEND=MODR + 0, OPQ=MODR + 1, ROWS=MODR + 2, YOFF=MODR + 4, WEND=MODR + 6,
            SPB=MODR + 8, LASTJP=MODR + 9, GW=MODR + 0x0A, GH=MODR + 0x0C, TOP=MODR + 0x0E,
            ROWBUF=MODR + 0x10, RSTART=MODR + 0x28, CY1=MODR + 0x2A, ROPQ=MODR + 0x2C, OPUTC=MODR + 0x30, OSETPOS=MODR + 0x32)
# 元の処理の番地 (間接ジャンプで戻る。keystone は数値の番地への相対ジャンプを誤って計算するため)
ORIG_PTRS = {"OPUTC": 0x293 + 0x100, "OSETPOS": 0x45C + 0x100}
VARSIZE = 0x40

NO_START = "、。，．・：；？！ー…‥」』）］｝〕〉》】ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々゛゜"
NO_END = "「『（［｛〔〈《【"

INV_CC = {"ae": "jb", "b": "jae", "e": "jne", "ne": "je", "a": "jbe", "be": "ja",
          "l": "jge", "ge": "jl", "g": "jle", "le": "jg"}


# ----------------------------------------------------------------------------- asm

def preprocess(src, subst):
    out = []
    n = 0
    for line in src.split("\n"):
        line = line.split(";", 1)[0].rstrip()
        if not line.strip():
            continue
        for k, v in subst.items():
            line = line.replace("{%s}" % k, str(v))
        t = line.split()
        op = t[0] if t else ""
        if op in ("CALL", "JMP"):
            n += 1
            out += [" .byte 0x%s" % ("e8" if op == "CALL" else "e9"), " .word %s - _m%d" % (t[1], n), "_m%d:" % n]
        elif op == "JCCF":
            n += 1
            out += [" %s _m%d" % (INV_CC[t[1]], n), " .byte 0xe9", " .word %s - _n%d" % (t[2], n), "_n%d:" % n, "_m%d:" % n]
        elif op == "RET":
            out.append(" .byte 0xc3")
        elif op == "RETF":
            out.append(" .byte 0xcb")
        else:
            out.append(line)
    return "\n".join(out)


def assemble(src, base):
    import capstone
    from keystone import KS_ARCH_X86, KS_MODE_16, Ks
    enc, _ = Ks(KS_ARCH_X86, KS_MODE_16).asm(src, base)
    code = bytes(enc)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    for ins in md.disasm(code, base):
        if ins.bytes[0] in (0x66, 0x67):
            raise SystemExit("32 ビットの接頭辞が出た %x: %s %s" % (ins.address, ins.mnemonic, ins.op_str))
    return code


def rel16(site_next, target):
    return struct.pack("<h", ((target - site_next + 0x8000) & 0xFFFF) - 0x8000)


# ----------------------------------------------------------------------------- font

def load_bdf():
    """{Unicode 字: 12 行のビット列 (12 bit)}"""
    glyphs = {}
    enc = None
    rows = None
    for line in BDF.read_text(encoding="latin-1").splitlines():
        if line.startswith("ENCODING"):
            enc = int(line.split()[1])
        elif line == "BITMAP":
            rows = []
        elif line == "ENDCHAR":
            if enc and rows is not None:
                hi, lo = enc >> 8, enc & 0xFF
                try:
                    ch = bytes([0x1B, 0x24, 0x42, hi, lo, 0x1B, 0x28, 0x42]).decode("iso2022_jp")
                    glyphs[ch] = [(int(r, 16) >> 4) & 0xFFF for r in rows[:12]] + [0] * (12 - len(rows))
                except UnicodeDecodeError:
                    pass
            rows = None
        elif rows is not None:
            rows.append(line.strip())
    return glyphs


def load_misaki():
    """{Unicode 字: 8 行のバイト}  (美咲ゴシック。BDF の字形は BBX で位置がずれている)"""
    glyphs = {}
    enc = bbx = rows = None
    asc = 6
    for line in MISAKI.read_text(encoding="latin-1").splitlines():
        if line.startswith("FONTBOUNDINGBOX"):
            _, w, h, x, y = line.split()
            asc = int(h) + int(y)
        elif line.startswith("ENCODING"):
            enc = int(line.split()[1])
        elif line.startswith("BBX"):
            bbx = [int(v) for v in line.split()[1:]]
        elif line == "BITMAP":
            rows = []
        elif line == "ENDCHAR":
            if enc is not None and rows is not None:
                w, h, xo, yo = bbx
                g = [0] * 8
                top = asc - (yo + h)
                for i, r in enumerate(rows):
                    v = int(r, 16) >> (len(r) * 4 - 8) if r else 0
                    if 0 <= top + i < 8:
                        g[top + i] = (v >> xo) & 0xFF
                glyphs[chr(enc)] = g
            rows = None
        elif rows is not None:
            rows.append(line.strip())
    return glyphs


def pack_glyph(rows):
    out = bytearray()
    for i in range(0, 12, 2):
        a, b = rows[i], rows[i + 1]
        out += bytes([a >> 4, ((a & 0xF) << 4) | (b >> 8), b & 0xFF])
    return bytes(out)


# ----------------------------------------------------------------------------- build

def originals():
    ORIG.mkdir(exist_ok=True)
    for f in FILES:
        dst = ORIG / f
        if not dst.exists():
            src = GAME / f
            if f in PRISTINE and hashlib.md5(src.read_bytes()).hexdigest() != PRISTINE[f]:
                raise SystemExit(f"{src} が元のファイルではない (dos/original/ に元のファイルを置いてください)")
            shutil.copyfile(src, dst)
    return {f: bytearray((ORIG / f).read_bytes()) for f in FILES}


def build_driver(drv, chars, glyphs, nsmall, small):
    n = len(chars)
    bm_size = (n + 7) // 8
    src = (HERE / "jpdrv.asm").read_text(encoding="utf-8")
    subst = dict(VARS)
    subst.update(VARPAD=",".join(["0"] * VARSIZE),
                 NGLYPH=n, GLYPHS=0x1234, BM_NOSTART=0x1234, BM_NOEND=0x1234,
                 SMALLH=SMALLH, NSMALL=nsmall, SGLYPHS=0x1234)
    entries = "\n".join(" JMP %s" % lab for lab in ("putc_jp", "setpos_jp", "width_far", "unit_far", "dlen_far"))
    src = src.replace("vars:\n    .byte {VARPAD}", "vars:\n    .byte {VARPAD}\n" + entries)
    code = assemble(preprocess(src, subst), MODR)
    bm1 = MODR + len(code)
    bm2 = bm1 + bm_size
    sg = bm2 + bm_size
    gl = sg + nsmall * 7
    subst.update(BM_NOSTART=bm1, BM_NOEND=bm2, GLYPHS=gl, SGLYPHS=sg)
    code2 = assemble(preprocess(src, subst), MODR)
    assert len(code2) == len(code), (len(code), len(code2))
    idx = {c: i for i, c in enumerate(chars)}
    bms = []
    for s in (NO_START, NO_END):
        bm = bytearray(bm_size)
        for ch in s:
            if ch in idx:
                bm[idx[ch] >> 3] |= 1 << (idx[ch] & 7)
        bms.append(bytes(bm))
    missing = []
    gdata = bytearray()
    for ch in chars:
        g = glyphs.get(ch)
        if g is None:
            missing.append(ch)
            g = glyphs["〓"]
        gdata += pack_glyph(g)
    sdata = bytearray()
    smiss = []
    for ch in chars[:nsmall]:
        g = small.get(ch)
        if g is None:
            smiss.append(ch)
            g = small["〓"]
        sdata += bytes(g[:7])
    if missing:
        print("字形が無い字 (〓で表示):", "".join(missing))
    if smiss:
        print("美咲に無い字 (〓で表示):", "".join(smiss))
    for k, v in ORIG_PTRS.items():
        o = VARS[k] - MODR
        code2 = code2[:o] + struct.pack("<H", v) + code2[o + 2:]
    out = bytearray(drv[:MODF].ljust(MODF, b"\0")) + code2 + bms[0] + bms[1] + sdata + gdata
    if len(out) + DRV_LOAD > 0xFF00:
        raise SystemExit(f"ドライバが 64KB に収まらない ({len(out):#x})")
    # 飛び先表: 11 番 (ファイル 0x2f) = 1 文字描画、13 番 (0x37) = 位置設定
    for site, target in ((0x2F, ENTRY["putc"]), (0x37, ENTRY["setpos"])):
        assert out[site] == 0xE8 and out[site + 3] == 0xCB
        out[site + 1:site + 3] = rel16(site + DRV_LOAD + 3, target)
    print(f"VGA.DRV: {len(drv):#x} -> {len(out):#x} (コード {len(code2)} バイト, 字 {n}, 小さい字 {nsmall}, 残り {0xFF00 - DRV_LOAD - len(out)})")
    return out


def build_exe(exe):
    img = 0x200

    def patch(at, old, new):
        assert exe[img + at:img + at + len(old)] == bytes(old), (hex(at), exe[img + at:img + at + len(old)].hex())
        exe[img + at:img + at + len(new)] = bytes(new)
    patch(0x2D50, [0x30, 0x05], [0x00, 0x10])               # ドライバの領域 0x530 → 0x1000 段落 (64KB)
    patch(0x334D, [0xB9, 0x00, 0x53], [0xB9, 0x00, 0xFF])   # ドライバの読み込み量 0x5300 → 0xff00

    def stub(at, target):
        ret = at + 19
        return bytes([0x55, 0x8B, 0xEC, 0xFC, 0x0E, 0xB8]) + struct.pack("<H", ret) + bytes([0x50, 0x2E, 0xFF, 0x36]) + \
            struct.pack("<H", DRV_SEGVAR) + bytes([0xB8]) + struct.pack("<H", target) + bytes([0x50, 0xCB, 0x5D, 0xC3])
    s = stub(STRWIDTH, ENTRY["width"]) + stub(UNIT_STUB, ENTRY["unit"]) + stub(DLEN_STUB, ENTRY["dlen"])
    assert len(s) == 63 and exe[img + STRWIDTH:img + STRWIDTH + 3] == b"\x55\x8b\xec"
    exe[img + STRWIDTH:img + STRWIDTH + len(s)] = s
    code = bytearray(exe[img:img + 0xFF60])
    n = patch_strlen6(code, 0)
    exe[img:img + 0xFF60] = code
    print(f"DS.EXE: 中央寄せ・幅 {n} か所を日本語の幅で")
    return exe


SPLIT_HEAD = bytes.fromhex("b8fcffe8")
SPLIT_PAT = bytes.fromhex("b82000 50 ff7604 e8".replace(" ", ""))


def build_split(base, addw, sep):
    """物語の語分け (元の関数と同じ場所・同じ入口処理) を、日本語の単位で分ける処理にする"""
    c = bytearray()
    c += bytes([0xB8, 0xFA, 0xFF, 0xE8]) + rel16(base + 6, CHKSTK)         # mov ax,-6 / call chkstk
    c += bytes([0xC7, 0x46, 0xFA, 0x01, 0x00])                               # mov [bp-6],1
    loop = len(c)
    c += bytes([0xB8]) + struct.pack("<H", sep) + bytes([0x50])              # push sep
    c += bytes([0xFF, 0x76, 0xFA])                                           # push [bp-6]
    c += bytes([0x8D, 0x46, 0x04, 0x50])                                     # lea ax,[bp+4] / push ax
    c += bytes([0xE8]) + rel16(base + len(c) + 3, UNIT_STUB)                 # call unit
    c += bytes([0x83, 0xC4, 0x06])                                           # add sp,6
    c += bytes([0xC7, 0x46, 0xFA, 0x00, 0x00])                               # mov [bp-6],0
    c += bytes([0x0B, 0xC0, 0x74, 0x00])                                     # or ax,ax / je done
    je_at = len(c) - 1
    c += bytes([0x89, 0x46, 0xFC])                                           # mov [bp-4],ax
    c += bytes([0x8B, 0x5E, 0x04, 0x03, 0xD8])                               # mov bx,[bp+4] / add bx,ax
    c += bytes([0x8A, 0x07, 0x88, 0x46, 0xFE])                               # mov al,[bx] / mov [bp-2],al
    c += bytes([0xC6, 0x07, 0x00])                                           # mov byte [bx],0
    c += bytes([0xFF, 0x76, 0x04])                                           # push [bp+4]
    c += bytes([0xE8]) + rel16(base + len(c) + 3, addw)                      # call add_word
    c += bytes([0x59])                                                       # pop cx
    c += bytes([0x8B, 0x5E, 0x04, 0x03, 0x5E, 0xFC])                         # mov bx,[bp+4] / add bx,[bp-4]
    c += bytes([0x8A, 0x46, 0xFE, 0x88, 0x07])                               # mov al,[bp-2] / mov [bx],al
    c += bytes([0x89, 0x5E, 0x04])                                           # mov [bp+4],bx
    c += bytes([0xEB, (loop - (len(c) + 2)) & 0xFF])                         # jmp loop
    c[je_at] = len(c) - (je_at + 1)
    c += bytes([0xC6, 0x06]) + struct.pack("<H", sep) + bytes([0x20, 0xC3])  # mov byte [sep],' ' / ret
    return bytes(c)


def build_overlay(name, ov):
    """語分け関数を探して差し替える。add_word と、その中のつなぎの " " の位置も探す"""
    csz = struct.unpack("<H", ov[4:6])[0]
    dbase = 0x908A
    i = ov.find(SPLIT_PAT)
    assert i >= 0 and ov[i - 6:i - 2] == SPLIT_HEAD, name
    s = i - 6                                           # 関数の先頭 (b8 fc ff e8 ..)
    e = ov.find(b"\xc3", s + 0x50) + 1
    assert e - s == 0x63, (name, e - s)
    calls = [(OVB + s + k + 3 + struct.unpack("<h", ov[s + k + 1:s + k + 3])[0]) & 0xFFFF
             for k in range(e - s - 2) if ov[s + k] == 0xE8]
    addw = calls[-1]
    sep = None
    for k in range(addw - OVB, s):
        if ov[k] == 0xB8 and ov[k + 3] == 0x50:
            v = struct.unpack("<H", ov[k + 1:k + 3])[0]
            if dbase <= v < dbase + len(ov) - csz and ov[csz + v - dbase:csz + v - dbase + 2] == b" \0":
                sep = v
    assert sep, name
    new = build_split(OVB + s, addw, sep)
    assert len(new) <= e - s, (name, len(new))
    ov[s:s + len(new)] = new
    ov[s + len(new):e] = b"\x90" * (e - s - len(new))
    print(f"{name}: 語分け {OVB + s:#x} (add_word {addw:#x}, つなぎ {sep:#x})")
    return ov


PUTC, DRAWSTR, MSGFETCH, SETPOS = 0x3877, 0x3895, 0x582, 0x3850
INITIAL = re.compile(re.escape(bytes.fromhex("8a46")) + b"(.)" + re.escape(bytes.fromhex("2ae450e8")), re.S)


def build_initials(name, ov):
    """メッセージを読んだ直後に先頭の 1 バイトだけ描く所 (性別の頭文字 M/F) を、文字列全体を描くように変える。
    日本語の 1 字は 2 バイトなので、先頭 1 バイトでは字にならない。
      mov al,[bp+X] / sub ah,ah / push ax / call putc  ->  lea ax,[bp+X] / push ax / call drawstring / nop nop"""
    def target(i):
        return (OVB + i + 3 + struct.unpack("<h", ov[i + 1:i + 3])[0]) & 0xFFFF
    n = 0
    for m in list(INITIAL.finditer(bytes(ov))):
        i = m.start()
        if target(i + 6) != PUTC:
            continue
        back = ov[max(0, i - 40):i]
        if bytes.fromhex("8d46") + m.group(1) not in back:
            continue
        if not any(back[k] == 0xE8 and target(max(0, i - 40) + k) == MSGFETCH for k in range(len(back) - 2)):
            continue
        ov[i:i + 9] = bytes([0x8D, 0x46]) + m.group(1) + bytes([0x50, 0xE8]) + rel16(OVB + i + 7, DRAWSTR) + bytes([0x90, 0x90])
        n += 1
    if n:
        print(f"{name}: 頭文字だけ描く所 {n} か所を文字列全体に")
    return ov


# 能力値・技能の一覧は「行番号 x 6 + 基準位置」で描画とクリックの範囲を決めている (行の間隔 6 ドット)。
# 日本語 (美咲 7 ドット) が上下で重ならないよう 7 ドットに広げる。基準位置で一覧の式だけを選ぶ
#   0x23/0x28 = 能力値の行と矢印、0x71/0x72/0x76 = 技能の行と矢印
LIST_PITCH = 7
LIST_BASES = {0x23, 0x28, 0x71, 0x72, 0x76}
LIST_OVERLAYS = {"VPCMK.OVR", "VPCVW.OVR", "VPCLV.OVR"}
PITCH = re.compile(re.escape(bytes.fromhex("b80600f76e")) + b"(.)" + re.escape(bytes([0x05])) + b"(..)", re.S)


# 一覧の間隔を広げたのに合わせて、固定位置で描く行も下げる: (x, 元の y) -> 新しい y
#   左上の 性別-種族 / 職業 / レベル (x=90) と右上の 称号 / 経験 / 倒数 (x=228): 6, 12, 18 -> 6, 13, 20
#   ボーナス (x=103, y=89) -> 94、技能ポイント (x=6, y=174) -> 178
FIXED_ROWS = {(90, 12): 13, (90, 18): 20, (228, 12): 13, (228, 18): 20, (103, 89): 94, (6, 174): 178}
SETPOS_CONST = re.compile(re.escape(bytes([0xB8])) + b"(..)" + re.escape(bytes([0x50, 0xB8])) + b"(..)"
                          + re.escape(bytes([0x50, 0xE8])) + b"(..)", re.S)
# 技能ポイントの右の数字 (6/6/1): y=0xae を積んでから x を計算する
SKILLPT_VALUE = (bytes.fromhex("b8ae0050b81a00"), bytes.fromhex("b8b20050b81a00"))
# 描いたあと画面へ転送する範囲 (x1, y1, x2, y2 を積む順は y2, x2, y1, x1)。下げた行・広げた行が収まるように
#   能力値とボーナス: y 35..96 -> 35..100、カルマの値のセル: y 77..83 -> 84..90
BLIT_RECTS = [(bytes.fromhex("b8600050b8a80050b8230050b86600"), bytes.fromhex("b8640050b8a80050b8230050b86600")),
              (bytes.fromhex("b8530050b8970050b84d0050b88b00"), bytes.fromhex("b85a0050b8970050b8540050b88b00"))]


def build_pitch(name, ov):
    n = 0
    for m in list(PITCH.finditer(bytes(ov))):
        if struct.unpack("<H", m.group(2))[0] in LIST_BASES:
            ov[m.start() + 1] = LIST_PITCH
            n += 1
    k = 0
    for m in list(SETPOS_CONST.finditer(bytes(ov))):
        i = m.start() + 8
        if (OVB + i + 3 + struct.unpack("<h", ov[i + 1:i + 3])[0]) & 0xFFFF != SETPOS:
            continue
        y, x = struct.unpack("<H", m.group(1))[0], struct.unpack("<H", m.group(2))[0]
        if (x, y) in FIXED_ROWS:
            ov[m.start() + 1:m.start() + 3] = struct.pack("<H", FIXED_ROWS[(x, y)])
            k += 1
    for old, new in [SKILLPT_VALUE] + BLIT_RECTS:
        k += bytes(ov).count(old)
        ov[:] = bytes(ov).replace(old, new)
    if n or k:
        print(f"{name}: 一覧の行の間隔を {LIST_PITCH} に ({n} か所)、固定位置の行 {k} か所")
    return ov


# 仲間を加える一覧 (VBASE) と作成画面 (VPCMK) の 1 行 = 名前 10 文字 + 性別の頭文字 + "-" + 職業名の先頭 3 バイト。
# 日本語では字の途中で切れるので、性別 1 字 (2 バイト) + "-" + 職業の略称 2 字 (msg_dos.txt の 9980〜) に組み直す
ABBR_BASE = 9980
PARTY_OLD = re.compile(re.escape(bytes.fromhex("8a8660fd50b81400f76ef88bd8588887420ec687430e2d")) + b".{50,90}?"
                       + re.escape(bytes.fromhex("c687470e00")), re.S)


def party_line_code(base):
    c = bytearray()
    c += bytes.fromhex("b81400f76ef88bd8")                 # bx = 20 * 行
    c += bytes.fromhex("8b8660fd" "8987420e")             # 性別 2 バイト -> [10..11]
    c += bytes.fromhex("c687440e2d")                      # "-" -> [12]
    c += bytes.fromhex("8d8660fd50" "8a46e92ae4")         # lea ax,[bp-0x2a0] / push / al = 職業
    c += bytes([0x05]) + struct.pack("<H", ABBR_BASE) + bytes([0x50])
    c += bytes([0xE8]) + rel16(base + len(c) + 3, MSGFETCH) + bytes.fromhex("83c404")
    c += bytes.fromhex("b81400f76ef88bd8")
    c += bytes.fromhex("8b8660fd" "8987450e" "8b8662fd" "8987470e")   # 略称 4 バイト -> [13..16]
    c += bytes.fromhex("c687490e00")                      # [17] = 0
    return bytes(c)


def build_partyline(name, ov):
    m = PARTY_OLD.search(bytes(ov))
    if not m:
        return ov
    s, e = m.start(), m.end()
    new = party_line_code(OVB + s)
    assert len(new) <= e - s, (name, len(new), e - s)
    ov[s:s + len(new)] = new
    ov[s + len(new):e] = bytes([0x90]) * (e - s - len(new))
    print(f"{name}: 仲間の一覧の 1 行の組み立てを日本語用に ({OVB + s:#x})")
    return ov


# 中央寄せ「x = (N - strlen) * 6 / 2 + ...」の strlen を、日本語の幅で数える dlen に替える
CENTER = re.compile(re.escape(bytes([0xE8])) + b"(..)" + re.escape(bytes.fromhex("598bc8b8")) + b"(.)"
                    + re.escape(bytes.fromhex("002bc1b90600f7e9")), re.S)
# 保安認証の 2〜4 行目 (y = 基準 + 15, 21, 27) を 7 ドット以上の間隔に (+15, 22, 29)
SECURITY_ROWS = [(bytes.fromhex("051500508d46a850e8"), bytes.fromhex("051600508d46a850e8")),
                 (bytes.fromhex("051b00508d46a850e8"), bytes.fromhex("051d00508d46a850e8"))]


# 「strlen(s) * 6」で表示の幅を出す所 (ボタンの中央寄せ、名前の後ろの位置など) も dlen に替える
WIDTH6 = re.compile(re.escape(bytes([0xE8])) + b"(..)" + re.escape(bytes.fromhex("59b90600f7e9")), re.S)


def patch_strlen6(code, base):
    """code (bytearray) の strlen*6 の呼び出しを dlen に替える。base = code[0] の実行時オフセット"""
    n = 0
    for pat in (CENTER, WIDTH6):
        for m in list(pat.finditer(bytes(code))):
            i = m.start()
            if (base + i + 3 + struct.unpack("<h", code[i + 1:i + 3])[0]) & 0xFFFF != STRLEN:
                continue
            code[i + 1:i + 3] = rel16(base + i + 3, DLEN_STUB)
            n += 1
    return n


# 人物の台詞の折り返し (VMNPC 0xe5ce): 語の終わりを strchr(' ') で探し、" " (DS:0x90ee) でつなぐ。
# 日本語には空白が無いので 1 行に収まらない。単位の区切りを unit_far に替え、つなぎは関数内の [bp-2] に置く
# (unit_far が全角どうしの間では "" にする)。語の後ろの空白を飛ばす +1 は unit_far がするので 0 に
NPCWRAP_FIND = bytes.fromhex("b820005053e8")            # push ' ' / push bx / call strchr
NPCWRAP_JOIN = bytes.fromhex("b8ee9050")                # push 0x90ee (" ")
NPCWRAP_ADV = bytes.fromhex("8346f801")                 # add word [bp-8],1


def build_npcwrap(name, ov):
    if name != "VMNPC.OVR":
        return ov
    i = bytes(ov).find(NPCWRAP_FIND + bytes.fromhex("8e5883c4048946fe50"))
    assert i >= 0, "VMNPC: 台詞の折り返しが見つからない"
    end = i + 0x1C
    assert ov[end:end + 3] == bytes.fromhex("8946fa"), "VMNPC: 語の長さの保存が違う"
    c = bytearray()
    c += bytes.fromhex("c746fe2000")                    # mov word [bp-2],' '  (つなぎ " ")
    c += bytes.fromhex("8d46fe50")                      # push &[bp-2]  (つなぎを書く所)
    c += bytes.fromhex("b8000050")                      # push 0 (先頭扱いしない)
    c += bytes.fromhex("8d46f850")                      # push &[bp-8]  (読む位置)
    c += bytes([0xE8]) + rel16(OVB + i + len(c) + 3, UNIT_STUB)
    c += bytes.fromhex("83c406")
    c += bytes([0x90]) * (0x1C - len(c))
    ov[i:end] = c
    j = bytes(ov).find(NPCWRAP_JOIN, end, end + 0x80)
    assert j >= 0, "VMNPC: つなぎの push が見つからない"
    ov[j:j + 4] = bytes.fromhex("8d46fe50")
    k = bytes(ov).find(NPCWRAP_ADV, j, j + 0xA0)
    assert k >= 0, "VMNPC: 語送りが見つからない"
    ov[k + 3] = 0
    print(f"{name}: 台詞の折り返し {OVB + i:#x} を日本語の単位で")
    return ov


# 「遭遇！」(msg 2025) の帯: 背景 (y 0x92〜0x98) を取っておき、赤い字を y=0x92 に描き、背景を戻して消す。
# 美咲は小さいフォントの枠の上下中央に置くので 1 行上 (0x91) から描かれ、その 1 行が消えずに残る
# (人物の画面の「パーティ」の上の赤い点)。字の y だけ 0x93 にして、取っておく範囲に収める
BANNER = {
    # 名前: (探す並び, 並びの中の 0x92 の位置)
    "VMNPC.OVR": (bytes.fromhex("b81600508b460605920050ff76fe8b4606059800"), 8),   # 0x623f の中
    "VMELE.OVR": (bytes.fromhex("b80600f76e0605920050ff76fee8"), 7),                # 0x51ab の中 (setpos の前)
}


def build_banner(name, ov):
    if name not in BANNER:
        return ov
    pat, k = BANNER[name]
    hits = []
    i = bytes(ov).find(pat)
    while i >= 0:
        if name != "VMELE.OVR":
            hits.append(i)
        else:
            c = i + len(pat) - 1                     # call の位置
            if (OVB + c + 3 + struct.unpack("<h", ov[c + 1:c + 3])[0]) & 0xFFFF == SETPOS:
                hits.append(i)
        i = bytes(ov).find(pat, i + 1)
    assert len(hits) == 1, (name, "遭遇の帯", len(hits))
    assert ov[hits[0] + k] == 0x92
    ov[hits[0] + k] = 0x93
    print(f"{name}: 「遭遇！」の字を 1 ドット下に ({OVB + hits[0] + k - 1:#x})")
    return ov


def build_center(name, ov):
    n = patch_strlen6(ov, OVB)
    k = 0
    if name == "VBASE.OVR":
        for old, new in SECURITY_ROWS:
            k += bytes(ov).count(old)
            ov[:] = bytes(ov).replace(old, new)
    if n or k:
        print(f"{name}: 中央寄せ {n} か所を日本語の幅で" + (f"、保安認証の行 {k} か所" if k else ""))
    return ov


# 品物名を "/" で 2 行に分けて描く関数 (6 つのオーバーレイにある): 2 行目は y+6。
# y+7 にすると下の品物の行に隠れる (品物 1 つ分の高さに余裕がない) ので使わない。1 ドット接するのは許容
TWOLINE_SIG = bytes.fromhex("c647d600c647e100")      # 1 行 10 バイトで切る所
TWOLINE_ADD = bytes.fromhex("8b460805060050ff76")    # mov ax,[bp+8] / add ax,6 / push ax / push [bp+6]


# 2 行の品物名の 2 行目を +7 にしてよいオーバーレイ (品物の行の間隔が 14 の所だけ。持ち物の一覧は 6 のまま)
TWOLINE7_OVERLAYS = ["VMNPC.OVR"]       # 店・交換の一覧 (0x8dc6, 0x973c から。行の間隔 14)


def build_twoline(name, ov):
    n = 0
    i = bytes(ov).find(TWOLINE_SIG)
    while i >= 0:
        j = bytes(ov).find(TWOLINE_ADD, i, i + 0x60)
        if j >= 0:
            ov[j + 4] = 7
            n += 1
        i = bytes(ov).find(TWOLINE_SIG, i + 1)
    if n:
        print(f"{name}: 2 行の品物名の間隔を 7 に ({n} か所)")
    return ov


def build_msgs(files, idx):
    orig = msgtool.read_all(bytes(files["MSG.HDR"]), bytes(files["MSG.DBS"]), bytes(files["MISC.HDR"]))
    ja = jpenc.load_msg_ja()
    lines = []
    n = 0
    have = {mid for mid, _ in orig}
    orig = sorted(orig + [(mid, b"") for mid in ja if mid not in have and mid >= ABBR_BASE])
    for mid, b in orig:
        if mid in ja:
            e = jpenc.encode(ja[mid], idx)
            if len(e) > 255:
                raise SystemExit(f"長すぎ {mid}: {len(e)} バイト")
            lines.append((mid, e))
            n += 1
        else:
            lines.append((mid, b))
    msgtool.HDR_CAP = (len(files["MSG.HDR"]) - 2) // 6
    hdr, dbs, misc = msgtool.write_all(lines)
    if len(dbs) > len(files["MSG.DBS"]):
        raise SystemExit(f"MSG.DBS が大きすぎる {len(dbs)}")
    used = len(dbs) // 1024
    dbs = dbs + b"\0" * (len(files["MSG.DBS"]) - len(dbs))
    print(f"MSG: 訳 {n} / {len(orig)}、{len(dbs) // 1024} ブロック中 {used} を使用")
    return bytearray(hdr), bytearray(dbs), bytearray(misc)


def build_scenario(scen, idx):
    n = 0
    for off, en, ja in jpenc.load_scen_ja():
        if scen[off:off + len(en) + 1] != en.encode() + b"\0":
            continue
        e = jpenc.encode(ja, idx)          # "/" は 2 行に分ける位置 (DOS 版も Gold 版と同じ。1 行 10 バイトまで)
        if len(e) > 15:
            e = e[:14] if len(e) % 2 == 0 else e[:15]
        scen[off:off + len(e) + 1] = e + b"\0"
        n += 1
    print(f"SCENARIO.DBS: 名前 {n} 欄")
    return scen


def main():
    files = originals()
    chars, nsmall = jpenc.ordered_chars()
    idx = {c: i for i, c in enumerate(chars)}
    out = {}
    out["VGA.DRV"] = build_driver(files["VGA.DRV"], chars, load_bdf(), nsmall, load_misaki())
    out["DS.EXE"] = build_exe(files["DS.EXE"])
    for f in OVERLAYS:
        ov = build_overlay(f, files[f]) if f in SPLIT_OVERLAYS else files[f]
        ov = build_partyline(f, ov)
        ov = build_center(f, ov)
        ov = build_npcwrap(f, ov)
        ov = build_banner(f, ov)
        ov = build_twoline(f, ov) if f in TWOLINE7_OVERLAYS else ov
        ov = build_initials(f, ov)
        out[f] = build_pitch(f, ov) if f in LIST_OVERLAYS else ov
    out["MSG.HDR"], out["MSG.DBS"], out["MISC.HDR"] = build_msgs(files, idx)
    out["SCENARIO.DBS"] = build_scenario(files["SCENARIO.DBS"], idx)
    OUT.mkdir(exist_ok=True)
    for f, d in out.items():
        (OUT / f).write_bytes(d)
    print("出力:", OUT)
    if "--install" in sys.argv:
        for f in out:
            shutil.copyfile(OUT / f, GAME / f)
        print("インストール:", GAME)
    if "--uninstall" in sys.argv:
        for f in FILES:
            shutil.copyfile(ORIG / f, GAME / f)
        print("元に戻した:", GAME)


if __name__ == "__main__":
    main()
