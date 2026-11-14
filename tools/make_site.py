# -*- coding: utf-8 -*-
"""紹介ページ（シリーズのトップ／ウィザードリィ6／7／8）を作る:  python tools/make_site.py  ->  site/

他のシリーズ（higesho.github.io/ultima-ja, mm-ja）と同じ構成・共通スタイル。
画面の写真は site/wiz6/img/・site/wiz7/img/ に置いた PNG だけが載る（下の SHOTS・SHOTS7 の名前で置く）。
ウィザードリィ7 の日本語化の本体は ../Wizardry7/jpmod（配布物は jpmod/release/Wizardry7_JP.zip）。
ウィザードリィ8 の日本語化の本体は ../Wizardry8/JPMod（配布物は JPMod/release/Wizardry8_JP.zip）。写真は site/wiz8/img/（SHOTS8・TALK8）。
"""
import os, shutil, html

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
SITE = os.path.join(ROOT, 'site')

BASE = 'https://higesho.github.io/wiz-ja/'
REPO = 'https://github.com/higesho/wiz-ja'
RELEASE = REPO + '/releases/tag/wiz6-v1.1'
ZIP = 'Wizardry6_JP.zip'

# 画面の写真（ファイル名, 見出し, alt）— 撮れたものから置いていく
SHOTS = [
    ('menu.png', 'メインメニュー', 'ウィザードリィ6 日本語版のメインメニューの画面'),
    ('create.png', 'キャラクター作成', 'キャラクター作成の画面'),
    ('status.png', '能力', 'キャラクターの能力の画面'),
    ('maze.png', '迷宮のメッセージ', '迷宮の中のメッセージの画面'),
    ('talk.png', '会話', '住人との会話の画面'),
    ('battle.png', '戦闘', '戦闘の画面'),
    ('shop.png', '店', '店の画面'),
    ('item.png', '品物', '品物の説明の画面'),
    ('spell.png', '呪文', '呪文の画面'),
]

RELEASE7 = REPO + '/releases/tag/wiz7-v1.1'
ZIP7 = 'Wizardry7_JP.zip'
SHOTS7 = [
    ('title.png', 'タイトル画面', 'ウィザードリィ7 のタイトル画面'),
    ('menu.png', '起動時のメニュー', 'ウィザードリィ7 日本語版の起動時のメニューの画面'),
    ('story.png', '物語', '冒頭の物語の画面'),
    ('status.png', '能力', 'キャラクターの能力の画面'),
    ('charmenu.png', 'キャラクターメニュー', 'キャラクターメニューの画面'),
    ('talk.png', '会話', '住人との会話の画面'),
    ('battle.png', '戦闘', '戦闘の画面'),
    ('dos.png', 'DOS 版', 'DOS 版で店の主人に会った画面'),
]

RELEASE8 = REPO + '/releases/tag/wiz8-v1.0.4'
ZIP8 = 'Wizardry8_JP.zip'
# ウィザードリィ8 の日本語化の本体は ../../Wizardry8/JPMod（配布物は JPMod/release/Wizardry8_JP.zip）
SHOTS8 = [
    ('title.png', 'タイトル画面', 'ウィザードリィ8 のタイトル画面'),
    ('field.png', 'フィールド', '墜落した船のそばの浜辺を歩くフィールドの画面'),
    ('status.png', 'キャラクターシート', 'フェアリーの魔術師のキャラクターシート。能力値・特性・装備の効果が日本語で出ている'),
    ('movie.png', '動画の字幕', 'オープニング動画に日本語の字幕が出ている画面'),
    ('journal.png', '日誌', '日誌のイベントの画面'),
]
# 「会話のしかた」の中に載せる写真
TALK8 = [
    ('topics.png', '台詞から拾われたキーワードが話題に追加される', '話題の一覧に、台詞から拾われたキーワードが追加された画面'),
    ('ask.png', '<code>rumors</code> と入力して質問', '入力欄に rumors と打って質問した画面'),
    ('whereis.png', '<code>where is anna</code> で所在を尋ねる', '入力欄に where is anna と打って所在を尋ねた画面'),
    ('talkabout.png', '<code>talk about marten</code> で質問', 'talk about marten と入力し、トレメインがマーテンについて語る画面'),
]

STYLE_SRC = os.path.join(HERE, 'site_style.css')

OTHER_SERIES = [
    ('https://higesho.github.io/ultima-ja/', 'ウルティマ 日本語化'),
    ('https://higesho.github.io/mm-ja/', 'マイト・アンド・マジック 日本語化'),
    ('https://higesho.github.io/dnd-ja/', 'D&amp;D 日本語化'),
]


def nav(prefix, here):
    """prefix: ルートからの相対パス（'' か '../'）, here: 'top' か 'wiz6' か 'wiz7'"""
    a = ['<a href="%sindex.html"%s>作品一覧</a>' % (prefix, ' class="here"' if here == 'top' else ''),
         '<a href="%swiz6/index.html"%s>ウィザードリィ6</a>' % (prefix, ' class="here"' if here == 'wiz6' else ''),
         '<a href="%swiz7/index.html"%s>ウィザードリィ7</a>' % (prefix, ' class="here"' if here == 'wiz7' else ''),
         '<a href="%swiz8/index.html"%s>ウィザードリィ8</a>' % (prefix, ' class="here"' if here == 'wiz8' else '')]
    a += ['<a href="%s">%s</a>' % (u, t) for u, t in OTHER_SERIES]
    a.append('<a href="%s">GitHub</a>' % REPO)
    return '\n    '.join(a)


def head(title, desc, url, og_type, og_image, css):
    img = '<meta property="og:image" content="%s">\n' % og_image if og_image else ''
    return ('<!doctype html>\n<html lang="ja">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            '<title>%s</title>\n<meta name="description" content="%s">\n'
            '<meta property="og:type" content="%s">\n<meta property="og:title" content="%s">\n'
            '<meta property="og:description" content="%s">\n<meta property="og:url" content="%s">\n%s'
            '<link rel="stylesheet" href="%s">\n</head>\n<body>\n'
            % (title, desc, og_type, title.split(' — ')[0], desc, url, img, css))


def header(prefix, here):
    return ('<header class="top"><div class="band">\n  <a class="brand" href="%sindex.html">ウィザードリィ 日本語化</a>\n'
            '  <nav>\n    %s\n  </nav>\n</div></header>\n' % (prefix, nav(prefix, here)))


FOOTER = ('<footer class="foot"><div class="band">\n'
          '  <p>非公式のファン翻訳です。開発元・販売元とは関係ありません。</p>\n'
          '  <p>Wizardry は各権利者の商標または登録商標です。ゲームの画像・文章の権利は原作の権利者にあります。</p>\n'
          '</div></footer>\n</body>\n</html>\n')


def existing_shots(game='wiz6', shots=None):
    d = os.path.join(SITE, game, 'img')
    return [s for s in (shots or SHOTS) if os.path.exists(os.path.join(d, s[0]))]


def top_page(shots, shots7=(), shots8=()):
    thumb8 = ('<a class="thumb" href="wiz8/index.html"><img src="wiz8/img/%s" alt="%s" loading="lazy"></a>' % (shots8[0][0], html.escape(shots8[0][2]))
              if shots8 else '<a class="thumb none" href="wiz8/index.html">VIII</a>')
    thumb7 = ('<a class="thumb" href="wiz7/index.html"><img src="wiz7/img/%s" alt="%s" loading="lazy"></a>' % (shots7[0][0], html.escape(shots7[0][2]))
              if shots7 else '<a class="thumb none" href="wiz7/index.html">VII</a>')
    thumb = ('<a class="thumb" href="wiz6/index.html"><img src="wiz6/img/%s" alt="%s" loading="lazy"></a>' % (shots[0][0], html.escape(shots[0][2]))
             if shots else '<a class="thumb none" href="wiz6/index.html">VI</a>')
    og = BASE + 'wiz6/img/' + shots[0][0] if shots else None
    desc = 'ウィザードリィ（Wizardry）シリーズを日本語で遊べるようにする、非公式のファンメイド日本語化プロジェクト。'
    s = head('ウィザードリィ シリーズ 日本語化プロジェクト', desc, BASE, 'website', og, 'style.css')
    s += header('', 'top')
    s += '''
<main class="band">
<section class="hero">
<h1>ウィザードリィ シリーズ 日本語化プロジェクト</h1>
<p class="tag">ウィザードリィ（Wizardry）シリーズを日本語で遊べるようにする、非公式のファンメイド日本語化プロジェクトです。
Steam で販売されている版を対象にしています。いまは第6作「Bane of the Cosmic Forge」、第7作「Crusaders of the Dark Savant」、第8作「Wizardry 8」を公開しています。
パッチは、ゲームのフォルダにファイルを置くだけで日本語になります。</p>
</section>

<h2 id="titles">作品一覧</h2>
<ul class="titles">
  <li class="ready">
    %s
    <div class="body">
      <span class="state ready">公開中</span>
      <span class="name"><a href="wiz6/index.html">ウィザードリィ6</a></span>
      <span class="orig">Wizardry 6: Bane of the Cosmic Forge（1990年）</span>
      <span class="desc">サブタイトルに「宇宙の鋳造所」を冠した、シリーズ第6作。一人称視点の3Dの迷宮を、6人のパーティーで進む。</span>
      <span class="more"><a href="wiz6/index.html">紹介とダウンロード</a></span>
    </div>
  </li>
  <li class="ready">
    %s
    <div class="body">
      <span class="state ready">公開中</span>
      <span class="name"><a href="wiz7/index.html">ウィザードリィ7</a></span>
      <span class="orig">Wizardry 7: Crusaders of the Dark Savant（1992年）／ Wizardry Gold 版・DOS 版</span>
      <span class="desc">前作の結末から続くシリーズ第7作。惑星ガーディアを舞台に、ダークサヴァントや諸勢力と秘宝を争う。</span>
      <span class="more"><a href="wiz7/index.html">紹介とダウンロード</a></span>
    </div>
  </li>
  <li class="ready">
    %s
    <div class="body">
      <span class="state ready">公開中</span>
      <span class="name"><a href="wiz8/index.html">ウィザードリィ8</a></span>
      <span class="orig">Wizardry 8（2001年）</span>
      <span class="desc">第7作から続く、シリーズ第8作。惑星ドミナスを舞台に、宇宙の環への昇神をめぐって諸勢力と争う。動画には日本語の字幕付き。</span>
      <span class="more"><a href="wiz8/index.html">紹介とダウンロード</a></span>
    </div>
  </li>
</ul>

<div class="text">
<h2>このプロジェクトについて</h2>
<ul>
<li>非公式のファンメイドパッチです。ゲーム本体は Steam などで入手してください。</li>
<li>パッチは、ゲームのフォルダにファイルを置くものです。外し方は作品ごとの紹介ページを見てください。</li>
<li>日本語の表示には、ウィザードリィ6 では美咲フォント（8×8 ドット）、ウィザードリィ7・8 では東雲フォント（ウィザードリィ7 の DOS 版と 8 の小さい文字は美咲フォントも）を使っています。</li>
<li>種族・職業・呪文などの用語は、作品をまたいでそろえています。</li>
</ul>

<h2>ご意見・誤訳の報告</h2>
<p>誤訳や不自然な訳に気づいたら、<a href="%s/issues">GitHub の Issues</a> で教えてください。</p>
</div>
</main>
''' % (thumb, thumb7, thumb8, REPO)
    s += '\n' + FOOTER
    return s


def wiz6_page(shots):
    og = BASE + 'wiz6/img/' + shots[0][0] if shots else None
    desc = 'Steam 版 ウィザードリィ6（Wizardry 6: Bane of the Cosmic Forge）を日本語で遊べるようにする、非公式のファンメイドパッチ。'
    s = head('ウィザードリィ6 日本語化パッチ — ウィザードリィ シリーズ 日本語化プロジェクト', desc,
             BASE + 'wiz6/', 'article', og, '../style.css')
    s += header('../', 'wiz6')
    shots_html = ''
    if shots:
        figs = '\n'.join('  <figure><img src="img/%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure>'
                         % (f, html.escape(alt), cap) for f, cap, alt in shots)
        shots_html = '\n<h2>画面</h2>\n<div class="shots">\n%s\n</div>\n' % figs
    s += '''
<main class="band">
<section class="hero">
<p class="sub">Wizardry 6: Bane of the Cosmic Forge</p>
<h1>ウィザードリィ6 日本語化パッチ</h1>
<p class="tag">Steam 版の「Wizardry 6: Bane of the Cosmic Forge」を日本語で遊べるようにする、非公式のファンメイドパッチです。
メニュー、キャラクターの作成と能力の画面、戦闘や店の文言、迷宮の中のメッセージ、住人たちとの会話、
品物・怪物・呪文・職業・種族の名前まで、ゲームの中の文章を日本語にしています。
ゲームのフォルダにファイルを上書きで置くだけで日本語になります。</p>
<p><a class="dl" href="%(release)s">ダウンロード（GitHub）</a></p>
</section>

<div class="text">
<h2>どんなゲーム？</h2>
<p>ウィザードリィ6「Bane of the Cosmic Forge」は、1990年に発売されたシリーズ第6作です。
一人称視点で歩く3Dの迷宮を、6人のパーティーで進みます。サブタイトルの「Cosmic Forge」は、このパッチでは「魔筆」と訳しています。</p>
<p>種族は11種類（人間、エルフ、ドワーフ、ノーム、ホビット、フェアリー、リザードマン、ドラコン、フェルパー、ラウルフ、ムーク）、
職業は14種類（戦士、魔術師、僧侶、盗賊、レンジャー、錬金術師、吟遊詩人、超能力者、ヴァルキリー、司教、君主、侍、修道僧、忍者）から選べます。</p>
</div>
%(shots)s
<div class="text">
<h2>入れ方</h2>
<ol>
<li>ゲームを終了します。念のため、ゲームの <code>BANE</code> フォルダをコピーして保存しておくと安心です。</li>
<li>ダウンロードした <code>%(zip)s</code> を展開し、中の <code>BANE</code> フォルダを、ゲームのフォルダに上書きでコピーします（「ファイルの置き換え」を選びます）。</li>
<li>いつもどおり Steam から起動します。</li>
</ol>
<p>ゲームのフォルダは、Steam でゲームを右クリック → 管理 → ローカルファイルを閲覧、で開く場所です（<code>BANE</code> フォルダがある場所）。</p>

<h2>外し方</h2>
<ul>
<li>Steam の「ゲームファイルの整合性を確認」を実行すると、元の英語版に戻ります。保存しておいた <code>BANE</code> フォルダを上書きで戻しても、元に戻ります。セーブデータの形式は変更していません。</li>
</ul>

<h2>遊ぶときの注意</h2>
<ul>
<li>合言葉（起動時の絵の合言葉）は英語（ローマ字）で入力します。答えは、Steam 版に付属の「Bonus Content」フォルダの「Wizardry 6 - Magic Word Icon List.pdf」で調べます。</li>
<li>会話で入力する言葉（キーワード）とクイズ・なぞなぞの答えは、英語のままです。日本語に訳すと会話が成立しなくなるため、原文のまま残しています。</li>
<li>キャラクターの名前は英字で入力します。</li>
<li>ボタンや矢印など、絵として描かれている文字（TURN など）は英語のままです。</li>
<li>文字は 8×8 ドットの小さな文字です。1 行に入る文字数の都合で、短く言い換えた箇所があります。職業の略称は 2 文字に縮めています。</li>
<li>全場面の確認は終わっていません。表示の崩れや誤訳があるかもしれません。</li>
<li>Windows 11 の Steam 版（付属の DOSBox）で動作を確認しています。ほかの版では動かない可能性があります。</li>
<li>ほかの改造パッチで <code>BANE</code> フォルダのファイルを書き換えるものとは、同時に使えません。</li>
</ul>

<h2>ご意見・誤訳の報告</h2>
<p>誤訳や不自然な訳に気づいたら、<a href="%(repo)s/issues">GitHub の Issues</a> で教えてください。</p>

<h2>クレジット</h2>
<ul>
<li>日本語の表示に美咲フォント（門真なむ氏、フリーソフトウェア）を使っています。</li>
<li>このパッチの使用は自己責任でお願いします。</li>
</ul>
</div>
</main>
''' % dict(release=RELEASE, shots=shots_html, zip=ZIP, repo=REPO)
    s += '\n' + FOOTER
    return s


def wiz7_page(shots):
    og = BASE + 'wiz7/img/' + shots[0][0] if shots else None
    desc = 'Steam 版 ウィザードリィ7（Wizardry 7: Crusaders of the Dark Savant）を日本語で遊べるようにする、非公式のファンメイドパッチ。Wizardry Gold 版と DOS 版に対応。'
    s = head('ウィザードリィ7 日本語化パッチ — ウィザードリィ シリーズ 日本語化プロジェクト', desc,
             BASE + 'wiz7/', 'article', og, '../style.css')
    s += header('../', 'wiz7')
    shots_html = ''
    if shots:
        figs = '\n'.join('  <figure><img src="img/%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure>'
                         % (f, html.escape(alt), cap) for f, cap, alt in shots)
        shots_html = '\n<h2>画面</h2>\n<div class="shots">\n%s\n</div>\n' % figs
    s += """
<main class="band">
<section class="hero">
<p class="sub">Wizardry 7: Crusaders of the Dark Savant（Wizardry Gold 版・DOS 版）</p>
<h1>ウィザードリィ7 日本語化パッチ</h1>
<p class="tag">Steam 版の「Wizardry 7: Crusaders of the Dark Savant」を、日本語で遊べるようにする非公式のファンメイドパッチです。
Steam 版に入っている Wizardry Gold（Windows 版）と、DOSBox で動く元の DOS 版の両方に対応しています。
物語や住人たちとの会話、メニュー、キャラクターの作成と能力の画面、戦闘の文言、品物・怪物・人物・呪文・職業・種族・称号の名前まで日本語にしています。
ゲームのフォルダにファイルを上書きで置くだけで日本語になります。</p>
<p><a class="dl" href="%(release)s">ダウンロード（GitHub）</a></p>
</section>

<div class="text">
<h2>どんなゲーム？</h2>
<p>ウィザードリィ7「Crusaders of the Dark Savant」は、1992年に発売されたシリーズ第7作です。前作の結末から物語が続き、惑星ガーディアを舞台に、
ダークサヴァントやさまざまな勢力と、秘宝をめぐって争います。Steam 版には、元の DOS 版と、Windows 向けに作り直された Wizardry Gold 版の両方が入っています。</p>
<p>種族・職業・呪文などの用語は、<a href="../wiz6/index.html">ウィザードリィ6 日本語化パッチ</a>とそろえています。訳文は 2 つの版で共通です。</p>
</div>
%(shots)s
<div class="text">
<h2>入れ方</h2>
<ol>
<li>ゲームを終了します。</li>
<li>（DOS 版を入れる場合）ゲームのフォルダの <code>DSAVANT</code> フォルダを、別の場所に丸ごとコピーして残しておきます。外すときに戻すと英語版に戻ります。</li>
<li>ダウンロードした <code>%(zip)s</code> を展開し、中の <code>wizardry7gold</code> フォルダと <code>DSAVANT</code> フォルダを、ゲームのフォルダに上書きでコピーします。
片方の版だけ日本語にしたいときは、そのフォルダだけをコピーします。</li>
<li>Steam から起動します。「プレイ」を押すと起動方法を選ぶ画面が出るので、遊ぶ版を選びます。</li>
</ol>
<p>ゲームのフォルダは、Steam でゲームを右クリック → 管理 → ローカルファイルを閲覧、で開く場所です（<code>wizardry7gold</code>・<code>DSAVANT</code>・<code>dosbox_windows</code> フォルダがある場所）。</p>

<h2>起動のしかた</h2>
<table class="ver">
<tr><th>版</th><th>Steam の起動方法の選択</th><th>日本語化のしくみ</th></tr>
<tr><td>Wizardry Gold 版（Windows）</td><td><code>Play Wizardry 7: COTDS Gold Edition</code></td><td><code>wing32.dll</code> と <code>jpmod</code> フォルダを追加するだけ（元のファイルは書き換えない）</td></tr>
<tr><td>DOS 版（DOSBox）</td><td><code>Play Wizardry 7: COTDS (MS-DOS)</code></td><td><code>DSAVANT</code> の 19 個のファイルを日本語化したものに置き換える</td></tr>
</table>
<p>DOS 版は、起動の途中の保安認証（マニュアルの単語を入れる画面）で、何も入力せずに Enter を押すと進めます（Steam 版の元の動作です）。</p>

<h2>外し方</h2>
<ul>
<li>Gold 版：<code>wizardry7gold</code> フォルダの中の <code>wing32.dll</code> と <code>jpmod</code> フォルダを削除すると、元の英語版に戻ります。</li>
<li>DOS 版：入れる前に残しておいた <code>DSAVANT</code> フォルダのコピーを上書きで戻します。
コピーが無いときは Steam の「ゲームファイルの整合性を確認」で元のファイルに戻せますが、そのときキャラクターやセーブのデータも元に戻ってしまうことがあります。
先に <code>PCFILE</code>・<code>SAVEGAME</code> で始まるファイルを別の場所にコピーしておいてください。</li>
<li>セーブデータの形式は変更していません。日本語化の前後で同じセーブデータを使えます。</li>
</ul>

<h2>遊ぶときの注意</h2>
<ul>
<li>会話で入力する言葉（キーワード）は英語のままです。日本語に訳すと会話が成立しなくなるため、原文のまま残しています。
台詞の中のキーワードには「ニューシティ(NEW CITY)」のように英語を添えています。看板では、枠に収めるため英語の添え書きを最後の行にまとめています。</li>
<li>キャラクターの名前は英字で入力します。画像として描かれている文字（EXIT など）は英語のままです。</li>
<li>文章は機械翻訳をもとにしています。不自然な訳や誤訳があるかもしれません。全場面の確認は終わっていません。</li>
<li>Windows 11 の Steam 版で動作を確認しています。ほかの版（GOG 版など）は確認していません。</li>
</ul>

<h3>Gold 版</h3>
<ul>
<li><strong>フォルダの場所（パス）の長さ</strong>：元のゲームには、ゲームのフォルダのパスが長いと人物の台詞が「av av av」のように化ける不具合があります
（音声ファイルの名前を組み立てる所があふれるためで、英語版でも起きます。<code>wizardry7gold</code> フォルダのパスがおよそ 53 文字を超えると起き、
Steam の既定の場所 <code>C:\Program Files (x86)\Steam\steamapps\common\…</code> でも超えます）。このパッチではこの不具合を直しています。
音楽の読み込みの一部（休息のときの曲など）に同じ原因の所が残っていますが、こちらは直していません。</li>
<li>ゲーム画面は高さ 1080 ドットまで拡大して表示します。大きさは <code>jpmod</code> フォルダの <code>jpmod.ini</code> で変えられます。起動時のメニューなど Windows の画面は拡大されません。</li>
<li><code>WIZARD.EXE</code> が違う版（Gold 1.1b 以外）では警告が出て、日本語化されずに英語のまま起動します。</li>
</ul>

<h3>DOS 版</h3>
<ul>
<li>Steam の「Play Wizardry 7: COTDS (MS-DOS)」で動作を確認しています。「with DOSBox Daum」と、Linux 版の起動方法では確認していません。</li>
<li><code>DSAVANT</code> のファイルは Steam 版の DOS 版専用です。ほかの版のゲームには上書きしないでください。</li>
<li>メニューや一覧などの小さい文字は美咲フォント（8×8 ドット）で表示します。画面の解像度が低いため、文字が小さく詰まって見える所があります。</li>
</ul>

<h2>ご意見・誤訳の報告</h2>
<p>誤訳や不自然な訳に気づいたら、<a href="%(repo)s/issues">GitHub の Issues</a> で教えてください。</p>

<h2>クレジット</h2>
<ul>
<li>日本語の表示に東雲フォント（パブリックドメイン）を、DOS 版ではあわせて美咲フォント（Num Kadoma 氏）を使っています。</li>
<li>このパッチの使用は自己責任でお願いします。</li>
</ul>
</div>
</main>
""" % dict(release=RELEASE7, shots=shots_html, zip=ZIP7, repo=REPO)
    s += '\n' + FOOTER
    return s


def figs_html(shots):
    return '\n'.join('  <figure><img src="img/%s" alt="%s" loading="lazy"><figcaption>%s</figcaption></figure>'
                     % (f, html.escape(alt), cap) for f, cap, alt in shots)


def wiz8_page(shots, talk):
    og = BASE + 'wiz8/img/' + shots[0][0] if shots else None
    desc = 'Steam 版 ウィザードリィ8（Wizardry 8）を日本語で遊べるようにする、非公式のファンメイドパッチ。動画の日本語字幕付き。'
    s = head('ウィザードリィ8 日本語化パッチ — ウィザードリィ シリーズ 日本語化プロジェクト', desc,
             BASE + 'wiz8/', 'article', og, '../style.css')
    s += header('../', 'wiz8')
    shots_html = '\n<h2>画面</h2>\n<div class="shots">\n%s\n</div>\n' % figs_html(shots) if shots else ''
    talk_html = '\n<div class="shots">\n%s\n</div>\n' % figs_html(talk) if talk else ''
    body = """
<main class="band">
<section class="hero">
<p class="sub">Wizardry 8（2001年）</p>
<h1>ウィザードリィ8 日本語化パッチ</h1>
<p class="tag">Steam 版の「Wizardry 8」を日本語で遊べるようにする、非公式のファンメイドパッチです。
物語や住人たちとの会話、メニューと各画面、戦闘の文言、日誌、品物・怪物・人物・呪文・技能の名前と説明まで日本語にしています。
動画（オープニング、各陣営の導入、エンディング）には日本語の字幕を付けました。
ゲームのフォルダにファイルを置くだけで日本語になります。元のゲームのファイルは書き換えません。</p>
<p><a class="dl" href="@RELEASE@">ダウンロード（GitHub）</a></p>
</section>

<div class="text">
<h2>どんなゲーム？</h2>
<p>ウィザードリィ8は、2001年に発売されたシリーズ第8作です。第7作の結末から物語が続き、惑星ドミナスを舞台に、
ダークサヴァントやトラン、ウンパニなどの勢力と、宇宙の環への昇神をめぐって争います。</p>
<p>種族・職業・呪文などの用語は、<a href="../wiz6/index.html">ウィザードリィ6</a>・<a href="../wiz7/index.html">ウィザードリィ7</a> の日本語化パッチとそろえています。</p>
</div>
@SHOTS@
<div class="text">
<h2>入れ方</h2>
<ol>
<li>ゲームを終了します。</li>
<li>ダウンロードした <code>@ZIP@</code> を展開し、中の <code>ddraw.dll</code>・<code>wiz8jp.dat</code>・<code>wiz8jp.chr</code>・<code>wiz8jp.sub</code> の 4 つを、ゲームのフォルダにコピーします。</li>
<li>いつもどおり Steam から起動します。</li>
</ol>
<p>ゲームのフォルダは、Steam でゲームを右クリック → 管理 → ローカルファイルを閲覧、で開く場所です（<code>Wiz8.exe</code>・<code>Data</code> フォルダがある場所）。</p>

<h2>外し方</h2>
<ul>
<li>置いた 4 つのファイル（<code>ddraw.dll</code>・<code>wiz8jp.dat</code>・<code>wiz8jp.chr</code>・<code>wiz8jp.sub</code>）を削除すると、元の英語版に戻ります。元のファイルは書き換えていないので、整合性の確認も要りません。</li>
<li>セーブデータの形式は変更していません。日本語化の前後で同じセーブデータを使えます（メッセージ欄の過去の文は、保存したときの言語のままです）。</li>
</ul>

<h2>しくみ</h2>
<p><code>ddraw.dll</code> をゲームのフォルダに置くと、ゲームが起動時に読み込みます（Windows の ddraw.dll への中継を兼ねています）。
ゲームがデータを読み込むときに、<code>wiz8jp.dat</code> の訳文と日本語の字形を当てます。<code>wiz8jp.dat</code> は元のファイルとの差分で、
元のファイルが Steam 版と違うときは、そのファイルは英語のまま表示します。</p>

<h2>動画と字幕</h2>
<p>元のゲーム（Steam 版）には、動画を再生する所で画面の準備に失敗し、動画が映らない・止まる不具合があります。
このパッチでは、動画を Glide（Steam 版に付属の nGlide）の画面に直接描くように直し、動画が映るようにしました。
台詞のある動画（オープニング、ウンパニ・トラン・無所属・新しいパーティーの導入、エンディング）には日本語の字幕が出ます。</p>
</div>

<div class="text">
<h2 id="talk">会話のしかた（キーワードで質問する）</h2>
<p>ウィザードリィ8の会話は、言葉（キーワード）で相手に質問するしくみです。
ゲームの中のキーワードは英語のままですが、<strong>日本語の言葉を選んで尋ねられます</strong>（選んだ言葉を、相手が知っている英語のキーワードに置き換えて尋ねます）。
キーボードで打つときだけ、英語で入力します。</p>

<h3>台詞の中のキーワード</h3>
<p>台詞の中で、質問に使える言葉には「ドミナス(DOMINUS)」「宇宙の環(COSMIC CIRCLE)」のように英語を添えています。
かっこの中の英語が、キーボードで打つときのキーワードです。</p>
<p>相手の台詞に出てきたキーワードは、ゲームが自動で拾って、画面右の話題の一覧に追加します。
訳語のある言葉は日本語で、ない言葉は英語のまま入ります（長い言葉は一覧の幅で省略して表示されます）。
一覧の言葉を選んで「話題」を押せば、入力しなくても質問できます。一覧は「人物」「場所」「アイテム」「その他」で絞り込めます。</p>

<h3>質問のしかた</h3>
<ul>
<li><strong>一覧から選ぶ</strong>：話題の一覧で言葉を選び、「話題」を押すとその言葉について尋ねます。「所在」を押すと、その人や場所がどこにあるかを尋ねます。</li>
<li><strong>記録欄でクリックする</strong>：画面下の会話の記録欄で「ドミナス(DOMINUS)」のような言葉にポインターを乗せると黄色くなり、クリックすると入力欄にその言葉（ドミナス）が入ります。そのまま「話題」か「所在」を押して尋ねます。</li>
<li><strong>入力する</strong>：右下の入力欄に英語で打ち、Enter キーを押します（キーボードから日本語は打てません）。大文字・小文字は区別しません。
一覧にまだない言葉でも、相手が知っていれば答えてくれます。</li>
</ul>
<p>パーティーの質問は「マーテンについて教えて。」のように、選んだ言葉＋日本語の文で表示されます（英語で打ったときは「martenについて教えて。」）。</p>

<h3>入力できる文の形</h3>
<p>入力欄では、キーワードだけを打つほかに、次の書き出しが使えます。<code>talk about</code> と <code>where</code> で始まる形は、あとにキーワードを続けます。<code>join</code> と <code>recruit</code> はそれだけで打ちます。</p>
<table class="ver">
<tr><th>入力の形</th><th>意味</th><th>入力の例</th></tr>
<tr><td><code>キーワード</code></td><td>その言葉について尋ねる</td><td><code>rumors</code>　<code>marten</code>　<code>dark savant</code></td></tr>
<tr><td><code>talk about キーワード</code></td><td>その言葉について尋ねる</td><td><code>talk about marten</code></td></tr>
<tr><td><code>where is 名前</code></td><td>人や場所がどこにあるかを尋ねる</td><td><code>where is anna</code>　<code>where is arnika</code></td></tr>
<tr><td><code>where are 名前</code></td><td>同上（複数のもの）</td><td><code>where are umpani</code></td></tr>
<tr><td><code>where's 名前</code></td><td>同上</td><td><code>where's tramain</code></td></tr>
<tr><td><code>join</code></td><td>仲間に加わってほしいと頼む（仲間になる人物だけ）</td><td><code>join</code></td></tr>
<tr><td><code>recruit</code></td><td>同上</td><td><code>recruit</code></td></tr>
</table>
<p>上のどれにも当てはまらない英文を打ったときは、文を単語に分けて、その中にキーワードがあればそれで質問します
（例：<code>what about marten</code> なら <code>marten</code> で尋ねます）。二語以上のキーワード（<code>dark savant</code> など）は、その言葉だけを打ってください。</p>

<h3>よく使うキーワードの例</h3>
<table class="ver">
<tr><th>種類</th><th>キーワード（入力する英語）</th></tr>
<tr><td>あいさつ・世間話</td><td><code>hi</code>　<code>how are you</code>　<code>rumors</code>　<code>bye</code></td></tr>
<tr><td>商売</td><td><code>trade</code>　<code>buy</code>　<code>items</code>　<code>armor</code>　<code>magic</code>　<code>potion</code></td></tr>
<tr><td>物語の鍵</td><td><code>dark savant</code>（ダークサヴァント）　<code>cosmic circle</code>（宇宙の環）　<code>ascension peak</code>（昇神の峰）　<code>destinae dominus</code>（デスティナエ・ドミナス）　<code>marten</code>（マーテン）　<code>phoonzang</code>（フーンザン）</td></tr>
<tr><td>場所</td><td><code>arnika</code>（アルニカ）　<code>trynton</code>（トリントン）　<code>marten bluff</code>　<code>rapax castle</code>　<code>new city</code>　<code>monastery</code>（修道院）</td></tr>
<tr><td>種族・勢力</td><td><code>t'rang</code>（トラン）　<code>umpani</code>（ウンパニ）　<code>rapax</code>（ラパックス）　<code>mook</code>（ムーク）　<code>higardi</code>（ハイガーディ）　<code>trynnie</code>（トリニー）</td></tr>
<tr><td>人物</td><td><code>vi domina</code>（ヴィ・ドミナ）　<code>anna</code>（アンナ）　<code>tramain</code>（トレメイン）　<code>zant</code>　<code>barlone</code></td></tr>
</table>
<p>答えの中にさらにキーワードが出てくるので、それをたどって話を広げていくのがウィザードリィ8の会話の進め方です。
多くの住人は、つづりを少し間違えた言葉（<code>ascention</code> など）も聞き取ってくれます。</p>
</div>
@TALK@
<div class="text">
<h2>遊ぶときの注意</h2>
<ul>
<li>会話で入力する言葉（キーワード）は英語のままです（上の「会話のしかた」を見てください）。謎かけの答えや、端末のログイン名・パスワードなども英語で入力します。</li>
<li>キャラクターの名前は英字で入力します。</li>
<li>ボタンなど、画像として描かれている文字は英語のままです。</li>
<li>日誌は 1 件を 1 行で表示する作りのため、訳文を短くまとめています。</li>
<li>文章は機械翻訳をもとにしています。不自然な訳や誤訳があるかもしれません。全場面の確認は終わっていません。</li>
<li>Windows 11 の Steam 版（nGlide で描画）で動作を確認しています。ほかの版（GOG 版など）は確認していません。</li>
<li>ほかに <code>ddraw.dll</code> を使う MOD やツール（画面のラッパーなど）とは同時に使えません。</li>
</ul>

<h2>ご意見・誤訳の報告</h2>
<p>誤訳や不自然な訳に気づいたら、<a href="@REPO@/issues">GitHub の Issues</a> で教えてください。</p>

<h2>クレジット</h2>
<ul>
<li>日本語の表示に東雲フォント（パブリックドメイン）と美咲フォント（Num Kadoma 氏）の字形を使っています。動画の字幕は Windows の Yu Gothic UI で表示します。</li>
<li>このパッチの使用は自己責任でお願いします。</li>
</ul>
</div>
</main>
"""
    for k, v in (('@RELEASE@', RELEASE8), ('@ZIP@', ZIP8), ('@REPO@', REPO), ('@SHOTS@', shots_html), ('@TALK@', talk_html)):
        body = body.replace(k, v)
    return s + body + '\n' + FOOTER


def main():
    os.makedirs(os.path.join(SITE, 'wiz6', 'img'), exist_ok=True)
    os.makedirs(os.path.join(SITE, 'wiz8', 'img'), exist_ok=True)
    os.makedirs(os.path.join(SITE, 'wiz7', 'img'), exist_ok=True)
    shutil.copyfile(STYLE_SRC, os.path.join(SITE, 'style.css'))
    shots = existing_shots()
    shots7 = existing_shots('wiz7', SHOTS7)
    shots8 = existing_shots('wiz8', SHOTS8)
    talk8 = existing_shots('wiz8', TALK8)
    open(os.path.join(SITE, 'index.html'), 'w', encoding='utf-8', newline='\n').write(top_page(shots, shots7, shots8))
    open(os.path.join(SITE, 'wiz8', 'index.html'), 'w', encoding='utf-8', newline='\n').write(wiz8_page(shots8, talk8))
    print('screenshots included: wiz8 %d / %d, talk %d / %d' % (len(shots8), len(SHOTS8), len(talk8), len(TALK8)))
    open(os.path.join(SITE, 'wiz7', 'index.html'), 'w', encoding='utf-8', newline='\n').write(wiz7_page(shots7))
    open(os.path.join(SITE, 'wiz6', 'index.html'), 'w', encoding='utf-8', newline='\n').write(wiz6_page(shots))
    have = {s[0] for s in shots}
    print('pages written to', SITE)
    print('screenshots included: wiz6 %d / %d, wiz7 %d / %d' % (len(shots), len(SHOTS), len(shots7), len(SHOTS7)))
    for f, cap, _ in SHOTS:
        if f not in have:
            print('  missing: site/wiz6/img/%s  (%s)' % (f, cap))


if __name__ == '__main__':
    main()
