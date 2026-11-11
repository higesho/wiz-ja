# -*- coding: utf-8 -*-
"""上書きで置くだけの配布用 zip を作る:  python tools/make_release.py  ->  release/Wizardry6_JP.zip
中身: BANE/（日本語化で変わったファイルだけ）、説明書.txt、ライセンス.txt
"""
import os, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
ORIG, OUT, REL, DOCS = (os.path.join(ROOT, d) for d in ('original', 'out', 'release', 'dist_files'))
FILES = ['WROOT.EXE', 'EGA.DRV', 'MSG.DBS', 'MSG.HDR', 'MISC.HDR', 'SCENARIO.DBS', 'WFONT1.EGA',
         'WBASE.OVR', 'WDOPT.OVR', 'WINIT.OVR', 'WMAZE.OVR', 'WMELE.OVR', 'WMEXE.OVR',
         'WMNPC.OVR', 'WPCMK.OVR', 'WPCVW.OVR', 'WPOPS.OVR', 'WTREA.OVR']


def main():
    os.makedirs(REL, exist_ok=True)
    path = os.path.join(REL, 'Wizardry6_JP.zip')
    changed = [f for f in FILES
               if open(os.path.join(OUT, f), 'rb').read() != open(os.path.join(ORIG, f), 'rb').read()]
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in changed:
            z.write(os.path.join(OUT, f), 'BANE/' + f)
        for d in ('説明書.txt', 'ライセンス.txt'):
            z.write(os.path.join(DOCS, d), d)
    print('%s  %d files in BANE, %d bytes' % (path, len(changed), os.path.getsize(path)))


if __name__ == '__main__':
    main()
