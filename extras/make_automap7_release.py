# -*- coding: utf-8 -*-
"""ウィザードリィ7 オートマップ（オプション）の配布用 zip を作る:  python extras/make_automap7_release.py
  -> release/Wizardry7_Automap.zip  (Wiz7Map.exe, 地図の説明書.txt)
先に extras/automap/_cc7.bat で automap/out/wiz7map.exe をビルドしておく。"""
import os, zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
EXE = os.path.join(ROOT, 'extras', 'automap', 'out', 'wiz7map.exe')
DOC = os.path.join(ROOT, 'extras', 'automap', 'dist7', '地図の説明書.txt')
OUT = os.path.join(ROOT, 'release', 'Wizardry7_Automap.zip')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.write(EXE, 'Wiz7Map.exe')
    z.write(DOC, '地図の説明書.txt')
print(OUT, os.path.getsize(OUT), 'bytes')
