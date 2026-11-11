# -*- coding: utf-8 -*-
"""紹介ページ（シリーズのトップ／ウィザードリィ6／ウィザードリィ7）を作る:  python tools/make_site.py  ->  site/

他のシリーズ（higesho.github.io/ultima-ja, mm-ja）と同じ構成・共通スタイル。
画面の写真は site/wiz6/img/・site/wiz7/img/ に置いた PNG だけが載る（下の SHOTS・SHOTS7 の名前で置く）。
ウィザードリィ7 の日本語化の本体は ../Wizardry7/jpmod（配布物は jpmod/release/Wizardry7_JP.zip）。
"""
import os, shutil, html

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
SITE = os.path.join(ROOT, 'site')

BASE = 'https://higesho.github.io/wiz-ja/'
REPO = 'https://github.com/higesho/wiz-ja'
RELEASE = REPO + '/releases/tag/wiz6-v1.0'
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

RELEASE7 = REPO + '/releases/tag/wiz7-v1.0.2'
ZIP7 = 'Wizardry7_JP.zip'
SHOTS7 = [
    ('menu.png', '起動時のメニュー', 'ウィザードリィ7 日本語版の起動時のメニューの画面'),
    ('story.png', '物語', '冒頭の物語の画面'),
    ('status.png', '能力', 'キャラクターの能力の画面'),
    ('charmenu.png', 'キャラクターメニュー', 'キャラクターメニューの画面'),
    ('talk.png', '会話', '住人との会話の画面'),
    ('battle.png', '戦闘', '戦闘の画面'),
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
         '<a href="%swiz7/index.html"%s>ウィザードリィ7</a>' % (prefix, ' class="here"' if here == 'wiz7' else '')]
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


def top_page(shots, shots7=()):
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
Steam で販売されている版を対象にしています。いまは第6作「Bane of the Cosmic Forge」と第7作「Crusaders of the Dark Savant」を公開しています。
パッチは、ゲームのフォルダにファイルを上書きで置くだけで日本語になります。</p>
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
      <span class="orig">Wizardry 7: Crusaders of the Dark Savant（1992年）／ Wizardry Gold 版</span>
      <span class="desc">前作の結末から続くシリーズ第7作。惑星ガーディアを舞台に、ダークサヴァントや諸勢力と秘宝を争う。</span>
      <span class="more"><a href="wiz7/index.html">紹介とダウンロード</a></span>
    </div>
  </li>
</ul>

<div class="text">
<h2>このプロジェクトについて</h2>
<ul>
<li>非公式のファンメイドパッチです。ゲーム本体は Steam などで入手してください。</li>
<li>パッチは、ゲームのフォルダにファイルを上書きで置くものです。外し方は作品ごとの紹介ページを見てください。</li>
<li>日本語の表示には、ウィザードリィ6 では美咲フォント（8×8 ドット）、ウィザードリィ7 では東雲フォントを使っています。</li>
<li>種族・職業・呪文などの用語は、作品をまたいでそろえています。</li>
</ul>

<h2>ご意見・誤訳の報告</h2>
<p>誤訳や不自然な訳に気づいたら、<a href="%s/issues">GitHub の Issues</a> で教えてください。</p>
</div>
</main>
''' % (thumb, thumb7, REPO)
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
    desc = 'Steam 版 ウィザードリィ7（Wizardry 7: Crusaders of the Dark Savant）の Wizardry Gold 版を日本語で遊べるようにする、非公式のファンメイドパッチ。'
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
<p class="sub">Wizardry 7: Crusaders of the Dark Savant（Wizardry Gold 版）</p>
<h1>ウィザードリィ7 日本語化パッチ</h1>
<p class="tag">Steam 版の「Wizardry 7: Crusaders of the Dark Savant」に付属する Wizardry Gold（Windows 版）を、日本語で遊べるようにする非公式のファンメイドパッチです。
物語や住人たちとの会話、メニュー、キャラクターの作成と能力の画面、戦闘の文言、品物・怪物・人物・呪文・職業・種族・称号の名前、起動時のメニューまで日本語にしています。
ゲームのフォルダにファイルを上書きで置くだけで日本語になります。</p>
<p><a class="dl" href="%(release)s">ダウンロード（GitHub）</a></p>
</section>

<div class="text">
<h2>どんなゲーム？</h2>
<p>ウィザードリィ7「Crusaders of the Dark Savant」は、1992年に発売されたシリーズ第7作です。前作の結末から物語が続き、惑星ガーディアを舞台に、
ダークサヴァントやさまざまな勢力と、秘宝をめぐって争います。Steam 版には、Windows 向けに作り直された Wizardry Gold 版が付属しており、このパッチはその Gold 版を日本語にします。</p>
<p>種族・職業・呪文などの用語は、<a href="../wiz6/index.html">ウィザードリィ6 日本語化パッチ</a>とそろえています。</p>
</div>
%(shots)s
<div class="text">
<h2>入れ方</h2>
<ol>
<li>ゲームを終了します。</li>
<li>ダウンロードした <code>%(zip)s</code> を展開し、中の <code>wizardry7gold</code> フォルダを、ゲームのフォルダに上書きでコピーします。</li>
<li>いつもどおり Steam から Wizardry Gold（Windows 版）を起動します。</li>
</ol>
<p>ゲームのフォルダは、Steam でゲームを右クリック → 管理 → ローカルファイルを閲覧、で開く場所です（<code>wizardry7gold</code> フォルダがある場所）。
元のゲームのファイルは書き換えません。<code>wing32.dll</code> と <code>jpmod</code> フォルダを追加するだけです。</p>

<h2>外し方</h2>
<ul>
<li><code>wizardry7gold</code> フォルダの中の <code>wing32.dll</code> と <code>jpmod</code> フォルダを削除すると、元の英語版に戻ります。
Steam の「ゲームファイルの整合性を確認」では、追加したファイルは消えません。セーブデータの形式は変更していません。</li>
</ul>

<h2>遊ぶときの注意</h2>
<ul>
<li>会話で入力する言葉（キーワード）は英語のままです。日本語に訳すと会話が成立しなくなるため、原文のまま残しています。
台詞の中のキーワードには「ニューシティ(NEW CITY)」のように英語を添えています。</li>
<li>キャラクターの名前は英字で入力します。</li>
<li>ゲーム画面は高さ 1080 ドットまで拡大して表示します。大きさは <code>jpmod</code> フォルダの <code>jpmod.ini</code> で変えられます。起動時のメニューなど Windows の画面は拡大されません。</li>
<li>画像として描かれている文字（EXIT など）は英語のままです。</li>
<li>文章は機械翻訳をもとにしています。不自然な訳や誤訳があるかもしれません。全場面の確認は終わっていません。</li>
<li>Windows 11 の Steam 版（Wizardry Gold 1.1b）で動作を確認しています。ほかの版は確認していません。<code>WIZARD.EXE</code> が違う版では警告が出て、日本語化されずに英語のまま起動します。DOS 版には対応していません。</li>
</ul>

<h2>ご意見・誤訳の報告</h2>
<p>誤訳や不自然な訳に気づいたら、<a href="%(repo)s/issues">GitHub の Issues</a> で教えてください。</p>

<h2>クレジット</h2>
<ul>
<li>日本語の表示に東雲フォント（パブリックドメイン）を使っています。</li>
<li>このパッチの使用は自己責任でお願いします。</li>
</ul>
</div>
</main>
""" % dict(release=RELEASE7, shots=shots_html, zip=ZIP7, repo=REPO)
    s += '\n' + FOOTER
    return s


def main():
    os.makedirs(os.path.join(SITE, 'wiz6', 'img'), exist_ok=True)
    os.makedirs(os.path.join(SITE, 'wiz7', 'img'), exist_ok=True)
    shutil.copyfile(STYLE_SRC, os.path.join(SITE, 'style.css'))
    shots = existing_shots()
    shots7 = existing_shots('wiz7', SHOTS7)
    open(os.path.join(SITE, 'index.html'), 'w', encoding='utf-8', newline='\n').write(top_page(shots, shots7))
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
