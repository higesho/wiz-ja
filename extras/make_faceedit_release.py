# -*- coding: utf-8 -*-
"""顔グラエディタ（オプション）の配布用 zip を作る:  python extras/make_faceedit_release.py
  -> release/Wizardry6_FaceEditor.zip  (Wiz6Face.exe, 顔グラの説明書.txt)
先に extras/faceedit/_cc.bat で faceedit/out/Wiz6Face.exe をビルドしておく。"""
import os, zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
EXE = os.path.join(ROOT, 'extras', 'faceedit', 'out', 'Wiz6Face.exe')
DOC = os.path.join(ROOT, 'extras', 'faceedit', 'dist', '顔グラの説明書.txt')
OUT = os.path.join(ROOT, 'release', 'Wizardry6_FaceEditor.zip')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.write(EXE, 'Wiz6Face.exe')
    z.write(DOC, '顔グラの説明書.txt')
print(OUT, os.path.getsize(OUT), 'bytes')
