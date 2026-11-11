"""ビルド成果物と翻訳データを wizardry7gold フォルダへ配置する。

元のゲームファイルは一切上書きしない。追加するのは
  wizardry7gold/wing32.dll
  wizardry7gold/jpmod/ (msg_ja.txt, ui_ja.txt, jpmod.ini, fonts/)
のみ。--remove で取り除く。
"""
import shutil
import sys
from pathlib import Path

MOD = Path(__file__).resolve().parent.parent
GAME = MOD.parent / "wizardry7gold"
DEST = GAME / "jpmod"


def deploy():
    if not (GAME / "WIZARD.EXE").exists():
        sys.exit(f"WIZARD.EXE が見つかりません: {GAME}")
    DEST.mkdir(exist_ok=True)
    shutil.copy2(MOD / "build" / "wing32.dll", GAME / "wing32.dll")
    shutil.copy2(MOD / "translation" / "msg_ja.txt", DEST / "msg_ja.txt")
    shutil.copy2(MOD / "translation" / "ui_ja.txt", DEST / "ui_ja.txt")
    shutil.copy2(MOD / "translation" / "scen_ja.txt", DEST / "scen_ja.txt")
    shutil.copytree(MOD / "fonts", DEST / "fonts", dirs_exist_ok=True)
    ini = DEST / "jpmod.ini"
    if not ini.exists():
        shutil.copy2(MOD / "jpmod.ini", ini)
    print(f"deployed to {GAME}")


def remove():
    (GAME / "wing32.dll").unlink(missing_ok=True)
    shutil.rmtree(DEST, ignore_errors=True)
    print("removed")


if __name__ == "__main__":
    remove() if "--remove" in sys.argv else deploy()
