"""上書きで置くだけの配布用 zip を作る:  python tools/make_release.py  ->  release/Wizardry7_JP.zip

中身 (ゲームのフォルダ = wizardry7gold がある場所に展開する):
  wizardry7gold/wing32.dll
  wizardry7gold/jpmod/jpmod.ini        (dist_files/jpmod.ini: ログなしの配布用設定)
  wizardry7gold/jpmod/msg_ja.txt       (translation/msg_ja.txt: batch.py merge の結果)
  wizardry7gold/jpmod/ui_ja.txt
  wizardry7gold/jpmod/fonts/shinonome/ (BDF と元の LICENSE など)
  説明書.txt, ライセンス.txt
元のゲームのファイルは含めない。
"""
import zipfile
from pathlib import Path

MOD = Path(__file__).resolve().parent.parent
REL = MOD / "release"
DOCS = MOD / "dist_files"


def main():
    REL.mkdir(exist_ok=True)
    path = REL / "Wizardry7_JP.zip"
    files = [
        (MOD / "build" / "wing32.dll", "wizardry7gold/wing32.dll"),
        (DOCS / "jpmod.ini", "wizardry7gold/jpmod/jpmod.ini"),
        (MOD / "translation" / "msg_ja.txt", "wizardry7gold/jpmod/msg_ja.txt"),
        (MOD / "translation" / "ui_ja.txt", "wizardry7gold/jpmod/ui_ja.txt"),
        (DOCS / "説明書.txt", "説明書.txt"),
        (DOCS / "ライセンス.txt", "ライセンス.txt"),
    ]
    for f in sorted((MOD / "fonts").rglob("*")):
        if f.is_file():
            files.append((f, "wizardry7gold/jpmod/" + f.relative_to(MOD).as_posix()))
    for src, _ in files:
        if not src.exists():
            raise SystemExit(f"見つかりません: {src}")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, arc in files:
            z.write(src, arc)
    print(f"{path}  {len(files)} files, {path.stat().st_size} bytes")
    for _, arc in files:
        print("  " + arc)


if __name__ == "__main__":
    main()
