# Wizardry 7 Gold 日本語化MOD

Steam 版『Wizardry 7: Crusaders of the Dark Savant』の **Gold Edition (Windows 版, WIZARD.EXE 1.1b)** と
**DOS 版 (DSAVANT フォルダ, DOSBox で起動)** を日本語化する MOD。訳文 (`translation/msg_ja.txt` など) は両方で共通。
DOS 版の仕組みは下の「DOS 版」を参照。

## 仕組み

- `WIZARD.EXE` は `LoadLibrary("wing32.dll")` で WinG を読み込む。
  ゲームフォルダに置いた自作 `wing32.dll` が読み込まれ、そこから EXE をメモリ上でパッチする。
  - WinG の 10 関数は DIBSection / BitBlt で自前実装している。
    Steam 同梱の WING32.DLL は「システムフォルダ以外では起動拒否」するため転送しない。
    結果として、管理者権限での WinG インストールも不要。
- 元のゲームファイルは一切書き換えない。追加するのは `wizardry7gold/wing32.dll` と `wizardry7gold/jpmod/` のみ。

### フックしている箇所 (WIZARD.EXE Gold 1.1b)

| アドレス | 関数 | 用途 |
|---|---|---|
| 0x425d90 | `draw_char(uchar ch, short font)` | 全角文字を東雲フォント (BDF) か GDI で描画 (640x400 の実解像度) |
| 0x426cc7 | `short text_width(char*, short font)` | 全角文字の幅を返す |
| 0x467e33 | `msg_fetch(short id, char *dst)` | 翻訳文があれば差し替え |
| strlen 呼び出し 82 か所 | `strlen()*6` で固定幅レイアウトしている箇所 | 全角を 1 文字と数える (`src/strlen_sites.h`) |
| 0x47b169 | `split_words(char*)` (Gold の物語表示) | 日本語を 1 字単位で行に詰める (`hk_gold_split`) |
| 0x40ef63 | `wrap_text(char*)` (旧来の本文表示) | 同上 (`hk_wrap`) |
| 0x476c0e〜0x476cce | 仲間を加える一覧の 1 行 (0x4b07c0 + 20×行) の性別・職業 | 元は性別名の先頭 1 バイト + "-" + 職業名の先頭 3 バイトで字が切れる。性別 1 字 + "-" + 職業の略称 2 字 (Wiz6・DOS 版と同じ「野伏」など) にする (`stub_party`) |
| 0x40f176 ほか 4 関数 | 音声・音楽ファイルのパスを組み立てる 80 バイトのバッファ | ゲーム本体のあふれ対策 (下の「音声ファイルのパスのあふれ」) |
| IAT (USER32) | `DialogBoxParamA` `SetWindowTextA` `SetDlgItemTextA` `MessageBoxA` `CreateWindowExA` | 起動時メニュー・設定などの Windows 画面とメニューバーを `ui_ja.txt` で訳す |

主なデータ: フォント表 0x4a7930 (24byte/件, +0x0c=ヘッダ), カーソル X/Y 0x4a803c/0x4a8030 (論理 320x200),
前景/背景色 0x4a9468/0x4a945c, 背景塗り 0x4a7118, フレームバッファ [0x4a7100] (640x400 8bpp, 論理座標の 2 倍)。

### 拡大表示 (`jpmod.ini` の `[display]`)

- ゲームは 640x400 の画面をウィンドウ中央に等倍で転送する。DLL は転送を拡大に置き換える
  (`height=1080` なら高さ 1080 ピクセルまで、ウィンドウに収まる範囲で縦横比を保って中央に。`0` で元の大きさ)。
  - WinG の `WinGBitBlt`/`WinGStretchBlt` (ゲーム画面)、取り込み表の `StretchDIBits`/`PatBlt` (ゲーム領域内の描画だけ)
  - 動画 (MCI の AVI、`style child`): `status <alias> window handle` で得た動画ウィンドウの `MoveWindow`/`SetWindowPos`
    を拡大し、`put <alias> destination` で動画もウィンドウいっぱいに描かせる
  - マウス: ゲームのウィンドウをサブクラス化し、マウスメッセージの座標を元の 640x400 上の位置に戻す。
    `SetCursorPos` は逆に拡大後の位置にする
- 起動時メニューなどの Windows ダイアログは拡大しない。`smooth=1` でなめらかな拡大 (HALFTONE、ぼやける)。

### 内部文字コード

翻訳文 (UTF-8) は DLL 起動時に変換する。ASCII はそのまま、全角は出現順に番号 k を振り
`[0x80 + k/127][0x80 + k%127]` の 2 バイトにする。両バイトとも 0x80 以上なので、
本文中の制御文字 (`@ & % ] ! # $ ^ ~` など) と衝突しない。

### フォント

| font | ファイル | 原寸 (論理px) | 用途 | 日本語の既定値 |
|---|---|---|---|---|
| 0 | VBFONT0 | 6x6 固定幅 | メニュー・ラベル | 東雲 12 ドット, 送り 6 |
| 2 | VBFONT2 | 12x14 可変幅 | 本文 | 東雲 12 ドットを 2 倍 (24px), 送り 12 (1 行 15 字) |

`jpmod.ini` でフォント・サイズ・送り幅・縦位置を変更できる。

- 日本語の字形は **東雲フォント** (`fonts/shinonome/`、パブリックドメイン、
  http://openlab.ring.gr.jp/efont/shinonome/ の shinonome-0.9.11p1) の BDF を DLL が直接読む (`[fontN] bdf=`)。
  JIS コードは Shift_JIS 経由で Unicode にする (cp932 の対応表なので ～ は U+FF5E など訳文と一致)。
  訳文で使う文字だけ読み込む。BDF に無い文字や `bdf=` を書かないフォントは `face` の GDI フォントで描く。
- `scale=2` で BDF を整数倍に拡大できる (最近傍)。12 ドットの 2 倍はゲーム本来のドットの大きさと同じ。
- 送り幅は論理 px (実画面の 2 倍) 単位。16 ドット等倍なら 8 (23 字/行) か 9 (20 字/行)。

## メッセージデータ

- `MSG.HDR`: int16 件数 + 7 バイトのレコード (先頭ID, ブロック内オフセット, 追加件数, 1KB ブロック番号)
- `MSG.GLD`: Pascal 文字列の連続 (11,018 件, 約 333KB)。物語文は 1 メッセージ = 1 行で、原文側で改行済み。

### 本文の表示と折り返し

ゲーム中の物語・会話の本文は **Gold 版で追加された表示処理** を通る (実機で確認)。

- `0x4782a6(id, mode)` が連続した ID を順に取得し (ID が途切れたらブロック終わり)、1 件ずつ `0x47b169` に渡す。
- `0x47b169` はメッセージを空白で語に分け、語中の `_` を空白に変えて (改行しない空白) `0x47b210` に渡す。
- `0x47b210` は語頭の制御記号を処理し (表 `0x4a2820` = `! % & ] @ # ~`)、行バッファ `0x4b6130` に空白でつなぐ。
  幅 0xb8 (論理 184px) を超えたら `0x47b7a6` で 1 行描く。
  - `!` 改行 / `%` `&` 改行+クリック待ち / `@` 改行+改ページ / `]` 窓消去 / `#` 看板表示 / `~x` 引数付き (効果音など)
- `0x47b7a6` は行を中央寄せで描く (行末の `$` は左寄せ、`^` は前の行と同じ位置。物語の訳文では使われていない)。
  4 行を超えるとクリック待ちで改ページするので、行数が増えても読めずに流れることはない。
- DLL は `0x47b169` を `hk_gold_split` に置き換えている。日本語には空白が無く 1 メッセージが 1 語になって折り返せないため。
  全角 1 字ごとを折り返し位置にし、行頭禁則文字 (。、」…) は前の文字に、開き括弧 (「『（) は後ろの文字に付け、
  全角どうしの間の空白 (メッセージの境目) は詰める。制御記号の処理と描画は元の関数を呼ぶ。東雲 12 ドット 2 倍 (送り 12) で全角 15 字が 1 行。
- 旧来の表示処理 `0x40f176` → `0x40ef63` (幅 0xb0、4 行スクロール) も一部の画面に残っているので、`hk_wrap` で同様に置き換えている。
  こちらの行は 128 バイト (`0x4b5460`) なので、DLL が行枠の残りに収まるよう切り詰める (`hk_msg_fetch`、戻り先 0x40f192)。
- 大文字だけの見出し (`!NEW CITY@` など) は会話の入力キーワードと照合する表なので英語のまま残す。
- `"` は制御記号ではないので「 」に置き換えてよい。
- `tools/wrapsim.py` が Gold の表示を再現する (実機の画面と一致を確認済み)。現状、日本語の行数は英語の 0.82 倍、
  幅あふれ・禁則違反は 0。

### 会話キーワードの書き添え

- 会話ではプレイヤーが英単語を打ち、メッセージ中のキーワード表 (`!NEW CITY@` `"MAP%` `#ROOM%` など) と照合する。
  キーワード表は英語のまま残し、台詞の訳語の後ろに `(英語)` を添えて何を打てばよいか分かるようにする。
- `translation/keywords_ja.tsv`: キーワード → 訳文中の訳語 (固有名詞は用語集、普通名詞は Haiku が例文から対応付け)。
  訳語を空にしたキーワードは添えない。`python tools/keywords.py extract` で一覧を作り直せる (既存の行は残る)。
- `batch.py merge` が結合のときに添える (訳文の作業ファイルには入れない)。規則:
  会話ブロック (連続 ID) で原文にそのキーワードがあれば、訳文で最初に出る訳語の直後に 1 回だけ。
  1 字の訳語は熟語の一部 (王国の王) には付けない。16 字以下のキーワードは空白を `_` にする
  (ゲームは `_` を改行しない空白として描く)。DLL は `(…)` と直後の句読点を前の字と同じ単位にして折り返す。
- 会話・物語 (ID 8000 以降) だけに添える (`keywords.py` の `MIN_ID`)。それより前は UI・職業名・称号などで、
  v1.0 では誤って添えていた (盗賊(THIEF) など。v1.0.1 で修正)。現状 1,852 か所。

### 品物・怪物・人物の名前 (SCENARIO.GLD)

- 名前は SCENARIO.GLD の 16 バイトの欄 (NUL 終端)。品物はファイル位置 ≡ 32 (mod 72)、怪物は 16 バイト間隔で 4 欄
  (単数・複数・未識別の単数・複数)、人物は ≡ 72 (mod 148)。`tools/scen_names.py extract` が `translation/scen_en.tsv` に取り出す。
- 訳は `translation/scen_names_ja.tsv` (原文→訳)。Wiz6 日本語化の `scen_ja.txt` と同じ原文はその訳 (ダガー・鋼の兜 など)、
  残りは同じ流儀で Haiku が訳した。`scen_names.py build` が `translation/scen_ja.txt` (ファイル位置→訳) を作り、DLL が読む。
- DLL は CreateFileA/_lopen/OpenFile で SCENARIO.GLD のハンドルを覚え、ReadFile/_lread/_hread で読んだバッファの
  該当位置が「原文+NUL」と一致したときだけ訳に置き換える (元のファイルは変えない)。版が違ってデータの並びがずれても、
  別のデータを壊さず英語のまま残る (一致しなかった所は jpmod.log に「scenario name mismatch」と出る)。
- 長さ: 欄は 15 バイト + NUL (全角 7 字)。品物の名前は "/" で 2 行に分けて描かれ、1 行 10 バイト (全角 5 字) まで
  (超えると切れる)。`scen_names.py import` / `build` が検査する。
- 品物 368・怪物 373・人物 69 種、計 1,389 欄を訳した。持ち物の画面で表示を確認済み。

### 改ページの端切れ・語中の制御記号

- 1 行 15 字・4 行で改ページなので、段落が 60 字を少し超えると数文字だけのページ (「い……」) ができる。
  `python tools/orphans.py` で一覧 (4 行のページのあふれで、1 行・8 字以下のもの)。`export N` で Haiku 用の作業ファイル、
  `apply` で反映。2026-11-11 に 115 か所を訳文の短縮で 0 にした。
- 制御記号 (@ ! & % ] ~) は語頭でしか効かないので、「……@次の文」のように全角の直後に続けると記号がそのまま出る。
  `python tools/fix_ctrl_space.py` (`--fix`) が原文で語頭のものに空白を補う (全角どうしの空白は表示で詰まる)。39 か所を修正。

### 用語は Wizardry 6 日本語化と統一

- `../Wizardry6/JPN_PATCH/trans` (glossary.tsv, parts/spells_ui2.tsv) と同じ訳語を使う。
  `tools/apply_wiz6.py` が種族・職業・能力値・称号・技能・呪文などの ID を Wiz6 の訳にし、本文中の旧訳語も置き換えた。
  Wiz6 に無い Wiz7 の呪文は同じ意味訳の流儀で決めた (`WIZ7_ONLY_SPELLS`)。
- 文の言い回し (「選べ」と「選んでください」など) はそろえていない。違いは `tools/wiz6cmp.py` の出力で確認できる。

### メッセージの書き込み先バッファ (重要)

- ゲームは `msg_fetch` の結果を、英語原文に合わせた固定長のスタック上バッファに読み込む箇所がある
  (例: 名前入力ダイアログ `0x46f11d` のタイトルは `[ebp-0x18]` の 24 バイト)。
  訳文が長いとあふれて保存 EBP を壊し、後で落ちる (「このキャラクターを保存しますか」で落ちた原因)。
- `tools/msgbuf_scan.py` が全 350 箇所を解析し、書き込み先の容量を求める:
  - `src/msgbuf_sites.h`: 呼び出し箇所 (戻りアドレス) → 容量。DLL は超える訳文を全角の境目で切り詰めてログに記録する。
  - `translation/msgbuf.tsv`: ID → 容量。`batch.py check` は超える訳文をエラーにする。
- EXE の逆アセンブル (`wizard_full.asm`) を作り直したら `python tools/msgbuf_scan.py wizard_full.asm` を再実行する。

### 音声ファイルのパスのあふれ (ゲーム本体の不具合、2026-11-12)

- 人物の台詞 (`0x40f176` → `0x40ef63`) は、台詞ごとに「カレントフォルダ`\res\media\audio\ID.wav`」を
  80 バイトのスタック上バッファ `[ebp-0x1a4]` に組み立てて音声を鳴らす。フォルダのパスが 54 字を超えるとあふれ、
  直後のメッセージのバッファ `[ebp-0x154]` の先頭が `".wav"` の末尾 `av` で上書きされ、台詞が「av av av …」に化ける
  (英語でも起きる。Steam の既定の場所 `C:\Program Files (x86)\Steam\...\wizardry7gold` でも 69 字)。
- DLL の `patch_pathbufs` が、80 バイトのパス用バッファを使う `lea eax,[ebp-X]` (6 バイトの形) を
  DLL 内の 512 バイトの静的バッファを指す `mov eax,imm32` に替える (`0x40f176` 台詞, `0x43d69a` 音声, `0x409141`・`0x476d65` 音楽)。
  `0x416e80`・`0x44a315` (音楽) は 3 バイトの形で替えられないので元のまま。

## 翻訳の作業手順

```bat
python tools\extract_msgs.py      :: 原文 -> translation\msg_en.txt
python tools\batch.py split       :: 未訳分を translation\work\en\bNNN.txt (約150件ずつ) に分割
:: 訳文を translation\ja\bNNN.txt に書く (用語集: translation\glossary.md)
python tools\batch.py check translation\ja\b001.txt   :: 制御記号・長さ・文字の検証
python tools\batch.py merge       :: translation\ja\*.txt -> translation\msg_ja.txt (エラー行は除外)
python tools\batch.py status      :: 進捗
python tools\wrapsim.py           :: 本文の折り返しを再現し、幅あふれ・禁則・読めずに流れる行を英語と比較
python tools\wrapsim.py show ID   :: そのブロックの表示を再現 (png ID で画像)
python tools\termcheck.py         :: 用語集の固有名詞が訳文で使われているか (表記揺れの候補)
python tools\wiz6cmp.py translation\wiz6_diff.md   :: Wizardry 6 日本語化との訳語の違い
python tools\apply_wiz6.py --dry  :: 用語を Wiz6 の訳にそろえる (--dry は変更点の表示だけ)
python tools\keywords.py stats    :: 会話キーワードの書き添え件数
```

- 能力値の略称 (204〜211) は直後に数値が続けて描かれるので、半角空白で 3 文字幅にそろえる (`力␣␣` `知恵␣`)。

`translation\ja\000_manual.txt` は手作業で確定した訳 (UI・呪文・称号・人名・地名)。

## ファイル構成

```
jpmod/
  src/            wing32.dll のソース (build.bat で MSVC x86 ビルド)
  tools/          extract_msgs.py (原文抽出), deploy.py (配置/削除), 解析用スクリプト
  translation/    msg_en.txt (原文), msg_ja.txt (結合済み訳文: ID<TAB>本文, 制御バイトは {xx})
                  glossary.md (用語集・翻訳ガイド), ja/ (訳文の作業ファイル), work/en/ (分割した原文)
  jpmod.ini       設定 (初回配置時のみコピー)
  fonts/          東雲フォント (BDF, LICENSE)
  translation/ui_ja.txt  Windows 標準の画面 (起動時メニュー・設定・メニューバー) の訳
  dos/            DOS 版の日本語化 (build_dos.py, jpdrv.asm, jpenc.py, msg_dos.txt, 解析用スクリプト)
```

## ビルドと配置

```bat
src\build.bat
python tools\deploy.py          :: 配置
python tools\deploy.py --remove :: 取り除く
```

## 配布 (上書きで置くだけの zip)

```bat
python tools\batch.py merge       :: 訳文の結合 (キーワードの書き添えも)
src\build.bat
python tools\make_release.py      :: release\Wizardry7_JP.zip
```

- zip の中身: `wizardry7gold/wing32.dll`、`wizardry7gold/jpmod/` (msg_ja.txt, ui_ja.txt, scen_ja.txt, jpmod.ini, fonts/shinonome/)、
  `DSAVANT/` (日本語化した DOS 版の 19 ファイル。先に `python dosuild_dos.py`)、`説明書.txt`、`ライセンス.txt`、`美咲フォント_LICENSE.txt`。
  ゲームのフォルダ (wizardry7gold・DSAVANT がある場所) に展開して上書きするだけ。
- 配布用の設定は `dist_files/jpmod.ini` (ログなし `log=0`)。説明書・ライセンス表記も `dist_files/`。
- Gold 版を外すときは `wizardry7gold/wing32.dll` と `wizardry7gold/jpmod/` を消す (Steam の整合性確認では消えない)。
  DOS 版は元の DSAVANT を残しておいて戻す (整合性確認でも戻るが、PCFILE・SAVEGAME も戻ることがある)。
- zip を展開しての起動を実機で確認済み (2026-11-11)。
- 公開: https://github.com/higesho/wiz-ja/releases/tag/wiz7-v1.0 (v1.0, 2026-11-11)、紹介ページ https://higesho.github.io/wiz-ja/wiz7/ 。
  サイトとリポジトリは Wizardry 6 のプロジェクト (`../Wizardry6/JPN_PATCH`) の `tools/make_site.py` `tools/make_repo.py` で作る。
  手順は `../Wizardry6/JPN_PATCH/publish/公開手順.md`。

## DOS 版 (DSAVANT)

Steam の「Play Wizardry 7: COTDS (MS-DOS)」(DOSBox) で起動する元の DOS 版。Gold と同じ ID の MSG・SCENARIO なので訳文を共用する。
実行時に差し込む手段が無いので、ゲームのファイルを書き換えたものを配る (ウィザードリィ6 日本語化と同じ形)。

```bat
python dosuild_dos.py              :: dos\original (元のファイル) から dos\out に作る
python dosuild_dos.py --install    :: 作ったものを DSAVANT に置く
python dosuild_dos.py --uninstall  :: DSAVANT を元に戻す
```

- 文字コード: 全角 1 字 = 2 バイト `[0x80 + n>>7][0x80 + n&0x7f]` (n は字の番号)。MSG.DBS・SCENARIO.DBS に書き込む。
  `dos/msg_dos.txt` は DOS 版だけの差し替え (職業の略称 9980〜 など)。
- 表示: VGA.DRV の後ろに `dos/jpdrv.asm` (nasm 風の記法を keystone で組み立て) を足し、ドライバの領域を 64KB に広げる。
  1 字を描く関数を差し替え、全角を東雲 12 ドット (大きいフォント) か美咲 8×8 (高さ 8 以下のフォント) で描く。幅の計算・語分けも差し替える。
- 各オーバーレイ (*.OVR) の手直し (build_dos.py の build_*): 物語の語分け (VBASE・VMAZE・VPCVW・VTREA)、人物の台詞の折り返し (VMNPC)、
  strlen×6 の中央寄せ・幅、性別の頭文字、仲間の一覧の行、一覧の行の間隔 6→7、店の 2 行の品物名 (VMNPC は 7)、「遭遇！」の帯の位置。
- 調べるときの道具: `dos/ddis.py` (逆アセンブル・参照探し)、`dos/emutest.py` (unicorn でドライバを動かす)、
  `dos/savetest/setloc.py` (SAVEGAME.DBS の位置を書き換えて任意の場所から始める。位置は level・区画・区画内の x/y)。
- 元のファイル (`dos/original/`) と出力 (`dos/out/`) は配らない・載せない。

## 開発用機能 (`jpmod.ini` の `[debug] log=1` 以上)

- `wizardry7gold/jpmod/jpmod.log` にログ。`log=2` で表示したメッセージ ID をすべて記録 (未訳箇所の特定に使う)。
- ゲーム中に F12、または `wizardry7gold/jpmod/shot.req` を作るとフレームバッファを `shot_NNN.bmp` に保存。
- 落ちたときはレジスタとスタックの中身を `jpmod.log` に記録する (`CRASH ...`)。
- `log=3` でキャラクター作成関数のフレームにハードウェア書き込み監視を仕掛ける (スタック破壊の調査用)。
- `[debug] nohooks=1` で WinG だけ提供し日本語化を無効にする (元の挙動との比較用)。

## 未対応 (今後)

- MSG.GLD は翻訳対象 8,158 件を全訳済み (Haiku による初訳。要推敲)。会話キーワード等の約 900 件は英語のまま
- SCENARIO.GLD の名前は訳した。戦闘中の怪物名・店の品物一覧などの画面はまだ実機で見ていない
- Windows 標準の画面: 起動時メニュー・メニューバーは実機で日本語表示を確認。設定・ファイルを開く・終了確認などは
  `ui_ja.txt` に訳を入れたが未確認。ファイル選択のコモンダイアログ等、表に無い文言は英語のまま
- 画像に描かれた英字 (EXIT など)
- font0 の行間が詰まる箇所の調整、レイアウト崩れの個別対応
  (能力値ラベルの上下が接する、種族名の前に「ー」が出る)
- 物語本文・会話・看板・店の一覧は実機で確認済み (2026-11-12、ニューシティのパルークの店)。`~` 付きの場面はまだ見ていない
- ゲームフォルダを長いパス (約 120 文字) に複製すると起動直後に落ちる (0x47d5ad)。元の挙動かは未確認
- 会話のキーワード入力は英語のまま (訳文に `(英語)` を添えて案内)。看板では添え書きを最後の行にまとめる (`keywords.py`)
- 音楽のパス (0x416e80・0x44a315) の 80 バイトのあふれは直していない (命令が 3 バイトの形で差し替えられない)
- Haiku による機械翻訳の推敲 (固有名詞の表記揺れ、`#!` 看板の改行位置など)
