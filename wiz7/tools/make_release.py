"""上書きで置くだけの配布用 zip を作る:  python tools/make_release.py  ->  release/Wizardry7_JP.zip

中身 (ゲームのフォルダ = wizardry7gold がある場所に展開する):
  wizardry7gold/wing32.dll
  wizardry7gold/jpmod/jpmod.ini        (dist_files/jpmod.ini: ログなしの配布用設定)
  wizardry7gold/jpmod/msg_ja.txt       (translation/msg_ja.txt: batch.py merge の結果)
  wizardry7gold/jpmod/ui_ja.txt
  wizardry7gold/jpmod/fonts/shinonome/ (BDF と元の LICENSE など)
  DSAVANT/ の 19 ファイル     (DOS 版。dos/build_dos.py の出力 dos/out。日本語化した DOS 版のファイルで、上書きする)
  説明書.txt, ライセンス.txt
Gold 版は元のゲームのファイルを含めない (wing32.dll と jpmod を追加するだけ)。
DOS 版は、日本語化したゲームのファイル (DS.EXE・VGA.DRV・オーバーレイ・MSG・SCENARIO) を上書きする
(ウィザードリィ6 日本語化パッチと同じ形)。先に python dos/build_dos.py を実行しておく。
"""
import sys
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
        (MOD / "translation" / "scen_ja.txt", "wizardry7gold/jpmod/scen_ja.txt"),
        (DOCS / "説明書.txt", "説明書.txt"),
        (DOCS / "ライセンス.txt", "ライセンス.txt"),
        (MOD / "fonts" / "misaki" / "misaki_LICENSE.txt", "美咲フォント_LICENSE.txt"),
    ]
    sys.path.insert(0, str(MOD / "dos"))
    from build_dos import FILES as DOS_FILES   # noqa: E402
    for f in DOS_FILES:
        files.append((MOD / "dos" / "out" / f, "DSAVANT/" + f))
    for f in sorted((MOD / "fonts" / "shinonome").rglob("*")):
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
