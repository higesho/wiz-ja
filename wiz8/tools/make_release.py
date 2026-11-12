"""置くだけの配布用 zip を作る:  python tools/make_release.py  ->  release/Wizardry8_JP.zip

中身 (ゲームのフォルダ = Wiz8.exe がある場所に置く 4 ファイル + 説明書など):
  ddraw.dll    hook/out/ddraw.dll (hook/build.bat)
  wiz8jp.dat   tools/build_patch.py の出力 (ゲームのフォルダに書かれる)
  wiz8jp.chr   同上
  wiz8jp.sub   tools/movie_subs.py の出力
  説明書.txt, ライセンス.txt (dist_files/), fonts/shinonome・fonts/misaki のライセンス文書
元のゲームのファイルは含めない。先に build.bat と build_patch.py を実行しておく。
"""
import hashlib
import zipfile
from pathlib import Path

MOD = Path(__file__).resolve().parent.parent
GAME = MOD.parent
REL = MOD / "release"
DOCS = MOD / "dist_files"


def main():
    REL.mkdir(exist_ok=True)
    path = REL / "Wizardry8_JP.zip"
    dll = MOD / "hook" / "out" / "ddraw.dll"
    if dll.read_bytes() != (GAME / "ddraw.dll").read_bytes():
        print("注意: ゲームのフォルダの ddraw.dll が hook/out/ddraw.dll と違います (配布するのは hook/out の方)")
    files = [
        (dll, "ddraw.dll"),
        (GAME / "wiz8jp.dat", "wiz8jp.dat"),
        (GAME / "wiz8jp.chr", "wiz8jp.chr"),
        (GAME / "wiz8jp.sub", "wiz8jp.sub"),
        (DOCS / "説明書.txt", "説明書.txt"),
        (DOCS / "ライセンス.txt", "ライセンス.txt"),
    ]
    for name in ("LICENSE", "AUTHORS", "README"):
        files.append((MOD / "fonts" / "shinonome" / name, "fonts/shinonome/" + name))
    for name in ("misaki_LICENSE.txt", "misaki_readme.txt"):
        files.append((MOD / "fonts" / "misaki" / name, "fonts/misaki/" + name))
    for src, _ in files:
        if not src.exists():
            raise SystemExit(f"見つかりません: {src}")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, arc in files:
            z.write(src, arc)
    print(f"{path}  {len(files)} files, {path.stat().st_size} bytes")
    print("SHA-256", hashlib.sha256(path.read_bytes()).hexdigest())
    for _, arc in files:
        print("  " + arc)


if __name__ == "__main__":
    main()
