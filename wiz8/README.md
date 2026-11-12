# ウィザードリィ8 日本語化（wiz8/）

Steam 版「Wizardry 8」の非公式日本語化パッチのソース・ツール・訳文です。
配布物（`Wizardry8_JP.zip`）の入れ方は `dist_files/説明書.txt` と紹介ページ https://higesho.github.io/wiz-ja/wiz8/ にあります。

**原作のファイルや英語の原文は入っていません。** 日本語化のデータ（`wiz8jp.dat`）は、手元のゲームのファイルから作ります。

## しくみ

- `ddraw.dll`（`hook/`、C、MSVC の 32 ビットでビルド）をゲームのフォルダに置くと、Wiz8.exe が起動時に読み込みます。
  22 個の export はすべて Windows の ddraw.dll へ中継します（`ddraw_proxy.h`）。元のファイルは上書きも改名もしません。
- 仮想ファイル（`vfile.h`）: ゲームのファイル管理（FileOpen / FileRead / FileSeek など 8 関数）の入口を差し替え、
  読み取り専用で開かれたファイルが `wiz8jp.dat` にあれば、元のファイルを読んでサイズと CRC-32 を確かめ、差分を当てて渡します。
  元のファイルが違うときは英語のままにします。
- `wiz8jp.dat` は「元のファイルの X から N バイトをコピー」「このバイトを挿入」の列です（`tools/patchdat.py`）。
  中身は日本語の訳文と、フォント（STI）に足した日本語の字形です。
- 日本語の字形は東雲フォント（12・16 ドット、パブリックドメイン）と美咲フォント（8 ドット）から作ります（`tools/mkfont.py`、`tools/bdfglyph.py`）。
  字形の番号は 252 番以降で、文字の一覧は `wiz8jp.chr`（UTF-16LE）です。
- 日本語の折り返し: 訳文の文字の間に U+E000（幅 0 の字形）を入れ（`tools/jptext.py`）、exe の単語区切りの判定が
  U+E000 も区切りとみなすように差し替えます（`sep_patch.h`、`wrap_patch.h`）。
- 会話のキーワードは英語の入力のまま。台詞の中では「訳語(KEYWORD)」と英語を添えます（`tools/keywords.py`、表は `work/kw67/`）。
- 動画: 元のゲームは DirectDraw の準備に失敗すると動画で落ちる・止まるため、Glide（nGlide）の画面に直接描くようにし、
  日本語の字幕（`wiz8jp.sub`、`tools/movie_subs.py`）を重ねます。

## 作り方

必要なもの: Python 3.10 以上（`pip install pillow`）、Visual Studio の C コンパイラ（x86）。
ツールの中のゲームのフォルダの場所（`GAME = ...`）は、手元の場所に合わせて直してください。

1. 英語の原文を手元のゲームから取り出します: `python tools/extract.py`、`python tools/textdb.py`（`work/raw`、`work/text` ができます）。
2. 訳文は `work/ja/*.json`（id → 訳文）にあります。日誌の訳は `python tools/fact_ja.py` で作り直せます。動画の字幕の訳は `work/movies/ja.json` です。
3. `python tools/build_patch.py` で `wiz8jp.dat` と `wiz8jp.chr` をゲームのフォルダに作ります。`python tools/verify_patch.py` で検査します。
4. `hook/build.bat` で `ddraw.dll` を作ります（`hook/out/ddraw.dll`）。
5. `python tools/make_release.py` で配布用の zip を作ります。

詳しい調査の記録（アドレス、データの形式、試したこと）はリポジトリに入れていません。

## ライセンスなど

- 東雲フォント（パブリックドメイン）と美咲フォント（Num Kadoma 氏、フリーソフトウェア）を使っています。`fonts/` に元のライセンス文書があります。
- 非公式のファン翻訳です。開発元・販売元とは関係ありません。Wizardry は各権利者の商標または登録商標です。
