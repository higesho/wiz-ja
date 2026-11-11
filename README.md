# ウィザードリィ 日本語化プロジェクト（wiz-ja）

ウィザードリィ（Wizardry）シリーズを日本語で遊べるようにする、非公式のファンメイド日本語化プロジェクトです。

- 紹介ページ: https://higesho.github.io/wiz-ja/
- 第6作「Wizardry 6: Bane of the Cosmic Forge」の日本語化パッチ: [ダウンロード](https://github.com/higesho/wiz-ja/releases/tag/wiz6-v1.1)
- 第7作「Wizardry 7: Crusaders of the Dark Savant」（Wizardry Gold 版）の日本語化パッチ: [ダウンロード](https://github.com/higesho/wiz-ja/releases/tag/wiz7-v1.0)

ゲームのフォルダに、ファイルを上書きで置くだけで日本語になります。使い方は、ダウンロードした zip の中の `説明書.txt` を見てください。
誤訳や不自然な訳は [Issues](https://github.com/higesho/wiz-ja/issues) で教えてください。

このリポジトリには、サイトのページ、日本語の訳文、パッチを作るためのツールとソースが入っています。
**原作のファイルや英語の原文は入っていません。** 日本語化したゲームのファイルは、持ち主の手元のゲームから作ります。

## 自分で作るとき（ウィザードリィ6）

必要なもの: Python 3.10 以上、`pip install keystone-engine capstone pillow`

1. Steam 版ゲームの `BANE` フォルダの中身を、リポジトリの `original/` に、そのままコピーします（元のファイルのバックアップを兼ねます）。
   `WFONT0.EGA` `WFONT1.EGA` などの `W*.EGA` もコピーしてください。
2. 訳文を検査します（任意）。
   ```
   python tools/check.py trans/parts/ja_ui1.txt trans/parts/ja_ui2.txt trans/parts/ja_ui3.txt trans/parts/ja_s08.txt ...
   ```
3. ビルドします。`out/` に日本語化したファイルができます。
   ```
   python tools/build.py
   ```
4. 配布用の zip（`release/Wizardry6_JP.zip`）を作ります。
   ```
   python tools/make_release.py
   ```

## 仕組み（ウィザードリィ6）

- 画面は 8×8 ドットのマス目（40×25）で、日本語 1 文字 = 1 マスです。美咲フォント（BDF 版、8×8）の字形を使います。
- 日本語を描く処理は、グラフィックドライバ `EGA.DRV` の後ろに足したモジュール（`src/jpext.asm`）です。
  `WROOT.EXE` の 1 文字を描く 3 か所を、このモジュールの呼び出しに差し替えています。
- ゲーム中の文字列（`MSG.DBS`、ハフマン圧縮）は、0x80 以上のバイトを日本語の字形番号として使います
  （よく使う 95 字は 1 バイト、それ以外は 2 バイト）。
- 日本語 1 文字は 2 バイトのことがあるので、文字列の長さから位置を決めている所（中央揃え、会話の折り返し）は、
  表示の幅で計算するように直しています（`src/wroot_helpers.asm`）。
- アイテム・怪物・NPC の名前は `SCENARIO.DBS` の名前欄（16 バイト）を置き換えています。
- 職業の略称（パーティの欄）は、フォント `WFONT1.EGA` の絵柄を描き直しています。
- 会話のキーワードやなぞなぞの答えは、プレイヤーが英語で入力するので、原文のまま残しています（`tools/story_fix.py`）。

## 訳文のファイル

- `trans/parts/ja_*.txt` … `ID<TAB>s<TAB>訳文`。制御文字は `\xNN`、メニューの頭文字キーは `{X}`。
- `trans/scen_ja.txt` … アイテム・怪物・NPC の名前（`I<n>` `M<n>.<k>` `N<n>`）。
- `trans/glossary.tsv` … 用語集。`trans/GUIDE.md` … 訳すときのルール（長さの制限、表示のしくみ）。
- 検査は `tools/check.py`（文字数、制御文字、右端のはみ出し、折り返し）です。

## ウィザードリィ7（`wiz7/`）

- Steam 版に付属の Wizardry Gold（Windows 版、`WIZARD.EXE` 1.1b）が対象です。
- ゲームが読み込む WinG（`wing32.dll`）の互換品を作り、ゲームのフォルダに置きます。この DLL がメモリ上でゲームを書き換え、
  日本語の文字を東雲フォントで描き、文章を訳文に差し替え、日本語の折り返しを行い、画面を拡大します。元のファイルは書き換えません。
- ソースは `wiz7/src/`（C、MSVC でビルド）、ツールは `wiz7/tools/`、訳文は `wiz7/translation/ja/`、
  詳しい仕組みと作業手順は `wiz7/README.md` にあります。英語の原文は入っていません（`tools/extract_msgs.py` で手元のゲームから取り出します）。
- 用語（種族・職業・能力値・称号・技能・呪文）はウィザードリィ6 の訳とそろえています。

## ライセンスなど

- 日本語の表示に美咲フォント（門真なむ氏、フリーソフトウェア）を使っています。詳しくは `dist_files/ライセンス.txt`。
- ウィザードリィ7 では東雲フォント（パブリックドメイン）を使っています。詳しくは `wiz7/dist_files/ライセンス.txt`。
- 非公式のファン翻訳です。開発元・販売元とは関係ありません。Wizardry は各権利者の商標または登録商標です。
  ゲームの画像・文章の権利は原作の権利者にあります。
