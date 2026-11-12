"""DOS 版 (DSAVANT) の逆アセンブル補助
  python ddis.py FILE ADDR [LEN]      ADDR は実行時のオフセット (16 進)。FILE は DS.EXE / *.OVR / VGA.DRV
  python ddis.py FILE --find HEX      実行時アドレスの即値 (16 bit) を参照している所を探す

配置 (調べた結果):
  DS.EXE   : コード セグメントは image の先頭 (オフセット = image 位置)。データ (DS) は image 0xff60 から
  *.OVR    : ファイル位置 i のコードは CS:0x5047+i に置かれる。ヘッダ [4..5] = コードの大きさ csz、
             csz 以降のデータは DS:0x908a+(i-csz) に置かれる
  VGA.DRV  : 別セグメント。ファイル位置 = オフセット
"""
import struct
import sys
from pathlib import Path

import capstone

D = Path(__file__).resolve().parents[2] / "DSAVANT"
OVB = 0x5047
OVD = 0x908a
DSEG = 0xff60


def load(fn):
    d = (D / fn).read_bytes()
    if fn.upper() == "DS.EXE":
        img = d[0x200:]
        return img[:DSEG], 0, img[DSEG:], 0
    if fn.upper().endswith(".OVR"):
        csz = struct.unpack("<H", d[4:6])[0]
        return d[:csz], OVB, d[csz:], OVD
    return d, 0, b"", 0


def dis(code, base, addr, n):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    i = addr - base
    for ins in md.disasm(code[i:i + n], addr):
        print(f"{ins.address:05x}  {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}")


if __name__ == "__main__":
    fn = sys.argv[1]
    code, base, data, dbase = load(fn)
    if sys.argv[2] == "--find":
        v = struct.pack("<H", int(sys.argv[3], 16))
        i = code.find(v)
        while i >= 0:
            print(f"{base + i:05x}")
            i = code.find(v, i + 1)
    else:
        dis(code, base, int(sys.argv[2], 16), int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x100)
