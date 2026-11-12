"""DOS 版の日本語表示をエミュレータ (unicorn) で試す (ゲームを起動せずに確かめる)

  python dos/emutest.py ID [ID ...]   そのメッセージ群を物語の窓と同じ規則で折り返し、out/emutest.png に描く

DS.EXE の文字列幅・語分けの中継、VGA.DRV の 1 文字描画を、ビルドした dos/out/ のファイルのまま動かす。
物語の窓: 幅 184 (0xb8) を超えたら改行、1 行 12 ドット、中央寄せ (ゲームの add_word / draw_line と同じ考え方)。
"""
import struct
import sys
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_MODE_16, Uc
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                               UC_X86_REG_IP, UC_X86_REG_SP, UC_X86_REG_SS)

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build_dos  # noqa: E402
import jpenc      # noqa: E402

ROOT, DRV, DATA, FONT, VID = 0x1000, 0x2000, 0x3000, 0x4000, 0x5000
HALT = 0xFFF0                     # ROOT:HALT に hlt を置き、そこへ戻ったら止める


class Emu:
    def __init__(self):
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        exe = (HERE / "out" / "DS.EXE").read_bytes()[0x200:0x200 + 0xFF60]
        uc.mem_write(ROOT * 16, exe)
        uc.mem_write(ROOT * 16 + HALT, b"\xf4")
        drv = (HERE / "out" / "VGA.DRV").read_bytes()
        uc.mem_write(DRV * 16 + 0x100, drv)
        uc.mem_write(ROOT * 16 + build_dos.DRV_SEGVAR, struct.pack("<H", DRV))
        uc.mem_write(ROOT * 16 + 0x2DB3, struct.pack("<HH", 0x12F, DRV))       # 1 文字描画の飛び先
        uc.mem_write(ROOT * 16 + 0x2DBB, struct.pack("<HH", 0x137, DRV))       # 位置設定
        font = (build_dos.GAME / "VBFONT2.VGA").read_bytes()
        uc.mem_write(FONT * 16, font)
        uc.mem_write(ROOT * 16 + 0x364A + 4, struct.pack("<H", FONT))         # フォント 2
        uc.mem_write(DRV * 16 + 0x1E5, struct.pack("<H", VID))                # 描画先
        self.set16(0x293, 15)      # 文字色
        self.set16(0x291, 0)       # 背景色
        self.set16(0x299, 0)

    def set16(self, off, v):
        self.uc.mem_write(DRV * 16 + off, struct.pack("<H", v))

    def call(self, addr, args):
        """DS.EXE の near 関数を cdecl で呼ぶ"""
        uc = self.uc
        sp = 0xF000
        for a in reversed(args):
            sp -= 2
            uc.mem_write(DATA * 16 + sp, struct.pack("<H", a))
        sp -= 2
        uc.mem_write(DATA * 16 + sp, struct.pack("<H", HALT))
        for r, v in ((UC_X86_REG_CS, ROOT), (UC_X86_REG_DS, DATA), (UC_X86_REG_ES, DATA), (UC_X86_REG_SS, DATA),
                     (UC_X86_REG_SP, sp), (UC_X86_REG_BP, 0)):
            uc.reg_write(r, v)
        uc.emu_start(ROOT * 16 + addr, ROOT * 16 + HALT, count=2000000)
        return uc.reg_read(UC_X86_REG_AX)

    def put(self, off, b):
        self.uc.mem_write(DATA * 16 + off, b)

    def width(self, s):
        self.put(0x100, s + b"\0")
        return self.call(build_dos.STRWIDTH, [0x100, 2])

    def draw(self, s, x, y):
        self.call(0x3850, [x, y])
        for ch in s:
            self.call(0x3877, [ch, 2])

    def units(self, msg):
        """オーバーレイの新しい語分けと同じ手順で単位を取り出す: [(単位, つなぎ)]"""
        self.put(0x200, msg + b"\0")
        out = []
        p = 0x200
        first = 1
        while True:
            self.put(0x1F0, struct.pack("<H", p))
            n = self.call(build_dos.UNIT_STUB, [0x1F0, first, 0x1F8])
            first = 0
            p = struct.unpack("<H", bytes(self.uc.mem_read(DATA * 16 + 0x1F0, 2)))[0]
            if n == 0:
                return out
            unit = bytes(self.uc.mem_read(DATA * 16 + p, n))
            sep = bytes(self.uc.mem_read(DATA * 16 + 0x1F8, 1))
            out.append((unit, b" " if sep == b" " else b""))
            p += n


def main():
    from PIL import Image
    ids = [int(a) for a in sys.argv[1:]] or [15000, 15001, 15002, 15003]
    ja = jpenc.load_msg_ja()
    chars, _ = jpenc.ordered_chars()
    idx = {c: i for i, c in enumerate(chars)}
    rev = {v: k for k, v in idx.items()}

    def dec(b):
        s, i = "", 0
        while i < len(b):
            if b[i] >= 0x80 and i + 1 < len(b):
                s += rev.get(((b[i] - 0x80) << 7) | (b[i + 1] - 0x80), "?")
                i += 2
            else:
                s += chr(b[i])
                i += 1
        return s

    emu = Emu()
    lines, line = [], b""
    for mid in ids:
        if mid not in ja:
            continue
        for unit, sep in emu.units(jpenc.encode(ja[mid], idx)):
            if unit[:1] in (b"@", b"&", b"%", b"]", b"!", b"#", b"~"):
                if line:
                    lines.append(line)
                line = b""
                if unit[:1] == b"@":
                    lines.append(b"")
                continue
            tmp = line + (sep if line else b"") + unit
            if line and emu.width(tmp) > 0xB8:
                lines.append(line)
                line = unit
            else:
                line = tmp
    if line:
        lines.append(line)
    for l in lines:
        print(f"{emu.width(l):3d} | {dec(l)}")
    y = 4
    for l in lines:
        w = emu.width(l)
        emu.draw(l, 160 - (w + 1) // 2, y)
        y += 12
    vid = bytes(emu.uc.mem_read(VID * 16, 320 * 200))
    img = Image.new("L", (320, 200))
    img.putdata([255 if v == 15 else 0 for v in vid])
    out = HERE / "out" / "emutest.png"
    img.resize((960, 600), Image.NEAREST).save(out)
    print(out)


if __name__ == "__main__":
    main()
