"""SAVEGAME.DBS の位置を書き換える (テスト用)
  python savetest/setloc.py LEVEL SECTOR LX LY FACING [src.dbs]
  位置ブロック (ファイル 0x234c〜): level, x, y, lx, ly, sector, facing / 直前の位置 0x235a〜 も同じ値に
  x, y は SCENARIO.DBS の level 記録 (0xac40 + level*0xad0) +0x490/+0x4a0 の区画基点 + lx/ly
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[2] / "DSAVANT"
lv, sec, lx, ly, f = map(int, sys.argv[1:6])
src = Path(sys.argv[6]) if len(sys.argv) > 6 else HERE / "s0_start.dbs"
scen = (HERE.parent / "original" / "SCENARIO.DBS").read_bytes()
rec = 0xac40 + lv * 0xad0
x = scen[rec + 0x490 + sec] + lx
y = scen[rec + 0x4a0 + sec] + ly
d = bytearray(src.read_bytes())
blk = struct.pack("<7H", lv, x, y, lx, ly, sec, f)
d[0x234c:0x235a] = blk
d[0x235a:0x2368] = blk
out = HERE / f"loc_{lv}_{sec}_{lx}_{ly}.dbs"
out.write_bytes(d)
(GAME / "SAVEGAME.DBS").write_bytes(d)
print(f"level {lv} sector {sec} local ({lx},{ly}) -> ({x},{y}) facing {f}: {out.name} を SAVEGAME.DBS に")
