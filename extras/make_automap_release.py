# -*- coding: utf-8 -*-
"""オートマップ（オプション）の配布用 zip を作る:  python extras/make_automap_release.py
  -> release/Wizardry6_Automap.zip  (Wiz6Map.exe, 地図の説明書.txt)
先に extras/automap/_cc.bat で automap/out/wiz6map.exe をビルドしておく。"""
import os, zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
EXE = os.path.join(ROOT, 'extras', 'automap', 'out', 'wiz6map.exe')
DOC = os.path.join(ROOT, 'extras', 'automap', 'dist', '地図の説明書.txt')
OUT = os.path.join(ROOT, 'release', 'Wizardry6_Automap.zip')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.write(EXE, 'Wiz6Map.exe')
    z.write(DOC, '地図の説明書.txt')
print(OUT, os.path.getsize(OUT), 'bytes')
