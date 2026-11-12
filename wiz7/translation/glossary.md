# Wizardry 7 日本語化 用語集・翻訳ガイド

翻訳者 (人間・AI とも) はまずこのファイルを全部読むこと。

---

## 1. ファイル形式

- 1 行 = `ID<TAB>訳文`。ID は原文 (`msg_en.txt`) と同じものを使う。
- `{xx}` は制御バイト (16 進)。**原文にあるものは同じ位置に必ずそのまま残す**。
- 訳さない (英語のままにする) 行は **出力しない**。出力されなかった ID は原文が表示される。
- 原文が空文字列の行は出力しない。

## 2. ゲームの表示の仕組み (訳し方に直結する)

### 2-1. 本文 (物語・会話) はブロック単位で流し込まれる

- **連続した ID の並び** (例: 15000〜15010) が 1 つの文章ブロック。ゲームはブロック内の
  メッセージを順につなげ、テキストウィンドウ (幅 全角約17文字) で自動的に折り返す。
- つまり **英語の 1 行 = 画面の 1 行ではない**。訳文はブロック全体として自然な日本語になればよく、
  文の切れ目を英語の行の切れ目と合わせる必要はない。
  ただし内容の順序は守り、各 ID にほぼ同じ分量を割り振ること (前の ID に詰め込みすぎない)。
- **1 メッセージの上限: 全角 80 文字 (= 160 バイト) まで**。半角文字は 0.5 文字分。目安は原文 1 行分の内容。
  英語原文 1 行 (最大 71 字) は日本語ならたいてい全角 20〜30 字に収まる。
- 1 つの ID を空にしてはいけない (どうしても内容がなければ `…` など 1 文字を入れる)。

### 2-2. 制御記号 (位置ごと必ず保持)

本文中で次の記号は「語の先頭」にあると表示制御として働く。**原文と同じ位置に同じ記号を残す**。

| 記号 | 意味 |
|---|---|
| `@` | クリック待ち → 空行 (段落区切り)。行頭に付くことが多い。`!NEW CITY@` のような見出しの末尾にも付く |
| `[` | クリック待ち |
| `!` | 改行 (語の先頭のときのみ。`!DIONYSCEUS@` のような見出し) |
| `&` | 行を確定して改行 |
| `%` | 一時停止 |
| `]` | テキストウィンドウを消去 |
| `~` | 次の 1 文字を引数として読む (`~` と次の 1 文字は絶対に変えない) |

- 行末の ` &%]` のような制御の並びは、**直前の半角スペースも含めて**そのまま残す。
  例: `BAHHL-GRAAAG... &%]` → `バアール＝グラアアグ…… &%]`
- **これらの記号を普通の文字として使わないこと**。感嘆符・疑問符は必ず全角 `！` `？` を使う。

### 2-3. 置換変数 (個数と並びを保持)

| 記号 | 中身 |
|---|---|
| `^` | キャラクター名・名前などの文字列 |
| `$` | 数値や名前 (文脈次第) |
| `#` | 名前・数値 |
| `%` | 文中にある場合は数値 (`YOU FOUND % $` など) |

- 原文にある個数を必ず保ち、語順は日本語に合わせて入れ替えてよい。
  例: `^ has defeated # at $` → `^が$で#を打ち破った`
- 変数の前後の半角スペースは不要 (日本語では詰めてよい)。

### 2-4. 句読点・文字

- 句読点は全角: `。` `、` `！` `？` `…` `「」` `『』` `・` `ー`。三点リーダは `……` (2 個) を基本とする。
- ASCII の `! ? @ [ ] & % ~ ^ $ #` は、制御・変数として原文にあるもの以外は**使用禁止**。
- 英数字は半角でよい (`HP`, `GP`, `+2` など)。数字は半角。
- 半角カナ・機種依存文字・絵文字は使わない。JIS 第 1・第 2 水準の範囲の漢字を使う。

### 2-5. UI の短い文字列 (メニュー・ラベル・戦闘メッセージ)

- 大文字だけの短い英文 (`SELECT CHARACTER RACE`, `HIT!` など) は画面の固定欄に表示される。
- **全角の文字数を英語の文字数の 6 割以下**に収める (例: 英語 10 文字 → 全角 6 文字まで)。
  入らないときは簡潔な言い換え・略語を使う。
- 先頭・末尾の空白で中央寄せしているもの (`  (FULL)  `) は、空白を残してもよいし詰めてもよい。
- `*CANCEL*` のような `*` 囲みは `*取消*` のように残す。

## 3. 訳さない (英語のまま、出力しない) もの

| ID | 理由 |
|---|---|
| 454, 456 | 種族・職業の頭文字表 (内部データ) |
| 2000, 2001, 3900 など制御バイトだけの行 | 内部データ |
| `party_options` `cast_on_who}` `use_on_who}` `move_where}` など小文字+記号の識別子 | 内部識別子 |
| 2500〜2599 | マニュアルのコピープロテクト単語 (入力と照合する) |
| 1800〜1810 | `?Temple?` などの噂キーワード (照合用の可能性) |
| 2900〜2999 | スタッフクレジット |
| 7000〜7199 | 会話の入力キーワード (プレイヤーがキーボードで打つ語) |
| `!NEW CITY@` `{1f}OUR PARTY@` `"GUILD%` `BARLONE^0'` `{02}NO/{03}YE/` など大文字だけの見出し | 会話で入力語と照合するキーワード表 (`tools/batch.py` の `is_keyword`) |

※ 会話で NPC がキーワードを示唆する箇所 (例: "Ask me about the TEMPLE") は、
プレイヤーが英語で入力する必要があるため、**大文字のキーワードを英語のまま括弧で残す**:
`「神殿 (TEMPLE) のことを聞きたいか？」`

## 4. 文体

- 地の文 (情景描写・ナレーション): **だ・である調**。二人称 "you" は「あなたたち」ではなく
  主語を省略するか「一行」「パーティ」とする。例: "you feel alive" → 「生きている実感が湧いてくる」
- 重々しい古風な文体 (Thou, Thy, Ye) の話者: 「汝」「そなた」「〜であろう」「〜するがよい」
- 軍人 (ウンパニ、トラン兵): きびきびした命令口調「〜せよ」「〜であります」
- 商人・庶民: くだけた口調「〜だよ」「〜かい？」
- ラットキン: 調子のいい悪党口調「〜でさぁ」「旦那」
- 擬音・叫び (`BAHHL-GRAAAG`, `Hsssst`) はカタカナで雰囲気を出す: 「バアールグラアアグ」「シーッ」
- システムメッセージ (大文字の UI 文): 簡潔な体言止め・常体。「鍵がかかっている！」「呪文は失敗した！」
- 原文の大文字だけの文 (叫び・碑文) は普通の日本語でよい。強調したい場合は「 」で囲む。

## 5. 固有名詞

**用語は Wizardry 6 の日本語化 (`../Wizardry6/JPN_PATCH/trans/glossary.tsv`, `parts/spells_ui2.tsv`) と統一する。**
種族・職業・能力値・称号・スキル・呪文は Wiz6 と同じ訳。違いは `python tools/wiz6cmp.py` で確認できる。

### 5-1. 種族 (100〜110)

人間 / エルフ / ドワーフ / ノーム / ホビット / フェアリー / リザードマン / ドラコン / フェルパー / ラウルフ / ムーク

### 5-2. 職業 (120〜133)

戦士 / 魔術師 / 僧侶 / 盗賊 / レンジャー / 錬金術師 / 吟遊詩人 / 超能力者 / ヴァルキリー / 司教 / 君主 / 侍 / 修道僧 / 忍者

### 5-3. 能力値

STR 力 / INT 知恵 / PIE 信仰 / VIT 体力 / DEX 器用 / SPD 速さ / PER 魅力 / KAR カルマ /
HP / ST (スタミナ) / GP (金) / AC / LVL レベル / RNK 称号 / EXP 経験 / MKS 倒数

204〜211 (略称) は項目名の直後に数値を続けて描くので、半角空白を後ろに足して 3 文字幅にそろえる
(`力␣␣` `知恵␣` `カルマ`)。全角 1 字も半角 1 字も font0 では同じ幅。

### 5-4. 呪文系統

FIRE 火 / WATER 水 / AIR 風 / EARTH 土 / MENTAL 精神 / MAGIC 魔法
MAGE 魔術師 / PRIEST 僧侶 / ALCHEMIST 錬金術師 / PSIONIC 超能力者 (呪文の種別としては「魔術」「僧侶呪文」…ではなく職業名で呼ぶ)

### 5-5. 勢力・種族 (ゲーム世界)

| 原文 | 訳語 | 備考 |
|---|---|---|
| Dark Savant / the Savant | ダークサヴァント / サヴァント | 黒衣の超越者 |
| Savant Kui'Sa-Ka | サヴァント・クイ＝サ＝カ | |
| Savant Guardian(s) / Savant Guard | サヴァント・ガーディアン / サヴァントの衛兵 | |
| T'Rang / T'Rangs / T'Rang Empire | トラン / トラン族 / トラン帝国 | 蜘蛛型の異種族 |
| Umpani / Imperial Umpani Forces / Umpani Imperium / Imperial Umpani Federation | ウンパニ / ウンパニ帝国軍 / ウンパニ帝国 / ウンパニ帝国連邦 | サイ型の軍人種族 |
| Special Tactical Forces | 特殊戦術部隊 | ウンパニ |
| Dane / Danes / Magna Dane | デイン / デイン人 / マグナ・デイン | |
| Gorn / Gorn Empire / Gorn King / Gorn Lord / Gorn Army / Gorn Nation / Gorn Domains | ゴーン / ゴーン帝国 / ゴーン王 / ゴーンの君主 / ゴーン軍 / ゴーン国 / ゴーン領 | |
| Rattkin / Rattkin Magi | ラットキン / ラットキンの魔術師 | ネズミの盗賊団 |
| Munk / Munks | マンク / マンク族 | |
| Helazoid / Helazoid Legion(s) / Helazoid Flyers | ヘラゾイド / ヘラゾイド軍団 / ヘラゾイド飛行隊 | |
| Cosmic Forces | 宇宙の諸力 | |
| Cosmic Lord(s) | 宇宙の君主 | |
| Cosmic Circle | 宇宙の環 | |
| Fellowship | 同胞団 | 修道会 |
| High Fathers | 高位神父たち | |
| Watcher(s) | 監視者 | |
| Dartaen Alliance | ダルタエン同盟 | |
| Crusaders | 十字軍 | (タイトル) |
| Hellspawn | 地獄の落とし子 | |

### 5-6. 重要な事物

| 原文 | 訳語 |
|---|---|
| Astral Dominae | アストラル・ドミナエ |
| Cosmic Forge | 魔筆 (SFC 版ウィザードリィ6 の副題「禁断の魔筆」から) |
| Guardia / Lost Guardia | ガーディア / 失われしガーディア |
| Higardi / Higardia | ハイガーディ / ハイガーディア |
| Black Ship | 黒い船 |
| Holy Work | 聖なる御業 |
| Holy Sacrament | 聖なる秘跡 |
| Divine Plan / Divine Order | 神の計画 / 神の秩序 |
| Sacred Stone | 聖なる石 |
| Serpentine Staff | 蛇の杖 |
| Eternal Night | 永遠の夜 |
| Civil War | 内戦 |
| Destiny | 運命 |
| Universe | 宇宙 |
| Disruptor | ディスラプター |
| Command Headquarters | 司令部 |
| Supply Depot | 補給基地 |
| Recruiting Station | 募兵所 |
| Spaceport / Spaceport Center | 宇宙港 / 宇宙港センター |
| Security Cell | 保安房 |
| Firing Range | 射撃場 |
| Black Market | 闇市 |
| Constabulary | 警邏隊 |
| Apothecary | 薬屋 |
| Magik Emporium | マジック・エンポリウム |
| Paluke's Armory | パルークの武具店 |
| Innkeeper / Inn | 宿屋の主人 / 宿屋 |
| Guild | ギルド |
| Last Known Whereabouts | 最後に確認された居場所 |

### 5-7. 地名

| 原文 | 訳語 |
|---|---|
| New City / New-City | ニューシティ |
| Dionysceus / Great Tower of Dionysceus | ディオニセウス / ディオニセウスの大塔 |
| Nyctalinth | ニクタリンス |
| Orkogre / Orkogre Castle / Orkogre Forest | オーコーグル / オーコーグル城 / オーコーグルの森 |
| Rattkin Ruins | ラットキンの廃墟 |
| Munkharama / Munkharama Bridge | マンクハラマ / マンクハラマ橋 |
| City of Sky / Holy City | 天空都市 / 聖都 |
| Ukpyr / Ukpyr Mountains | ウクピア / ウクピア山脈 |
| Witch Mtns. / Witch Mountains | 魔女の山脈 |
| North Witch / South Witch / East Witch / West Witch | 北の魔女 / 南の魔女 / 東の魔女 / 西の魔女 |
| Lost Temple / Hidden Temple | 失われた神殿 / 隠された神殿 |
| Giantlands | 巨人の地 |
| Isle of Crypts | 墓所の島 |
| Tramontaine / Tramontane | トラモンテーン |
| Dragon Mtns. / Dragon Mountains | 竜の山脈 |
| Old Funhouse / Funhouse | 古い見世物小屋 / 見世物小屋 |
| Old Cemetery | 古い墓地 |
| Old City | 旧市街 |
| Myrmideon Road / Myrmideon Forest | ミュルミドン街道 / ミュルミドンの森 |
| Red Forest / North Forest / Dark Forest | 赤の森 / 北の森 / 暗黒の森 |
| River Road | 川沿いの道 |
| Eyrn River / Eryn River | エアン川 |
| Wilderness | 荒野 |
| Ancient Tombs | 古代の墳墓 |
| Thesminster Abbey | テスミンスター修道院 |
| Unholy Tower | 不浄の塔 |
| Temple of ... | 〜の神殿 |
| Sea | 海 |
| Razuka | ラズカ |
| Palace | 宮殿 |

### 5-8. 人名 (肩書きは日本語の位置に)

| 原文 | 訳語 |
|---|---|
| H'Jenn-Ra T'Rang | ジェンラ・トラン |
| Shritis T'Rang | シュリティス・トラン |
| K'Borra T'Rang | ボッラ・トラン |
| D'Rang T'Rang | ドラン・トラン |
| General Yamo | ヤモ将軍 |
| Rodan Lewarx | ロダン・ルワークス |
| Tracker Rhallick | 追跡者ラリック |
| Lt. Gruntwrapper | グラントラッパー中尉 |
| King Ulgar | ウルガー王 |
| Lord Galiere | ガリエール卿 |
| Capt. Boerigard | ボーリガード大尉 |
| Don Barlone / Barlone' | ドン・バルローネ / バルローネ |
| Ratsputin | ラッツプーチン |
| Mick the Pick | 鍵開けミック |
| Dame Ke-Li | ケ＝リ夫人 |
| Jan-Ette | ジャン＝エット |
| Kymas Turan | キーマス・トゥラン |
| Xen Xheng | ゼン・シェン |
| Brother TShober | トショーバー修道士 |
| Brother Almagorte / Brother Belcanzor | アルマゴルテ修道士 / ベルカンゾル修道士 |
| Bela | ベラ |
| Father Rulae | ルーレ神父 |
| Murkatos | マーカトス |
| Hilynda | ヒリンダ |
| Vi Domina | ヴィ・ドミナ |
| Old Blindmeis / Old Blienmeis | ブラインマイス翁 |
| Sergeant Balbrak / Sgt. Balbrak | バルブラク軍曹 |
| Sergeant Kabomm | カボム軍曹 |
| Lieutenant Gromo | グロモ中尉 |
| Rutis Pyr | ルーティス・パイア |
| Ratskell | ラッツケル |
| Rossarian | ロッサリアン |
| Gran Melange | グラン・メランジュ |
| Vitalia | ヴィタリア |
| Phoonzang | フーンザン |
| Aletheides | アレテイデス |
| Bodendoi | ボデンドイ |
| Dedaelis | ダイダリス |
| Torquesade | トルクサード |
| Plumac | プルマック |
| Dahlia | ダリア |
| Humpawhammer | ハンパハンマー |
| Paluke | パルーク |
| Brombadeg | ブロンバデグ |
| Bertie | バーティ |
| Chalu / Haio / Hoyo / Jonga / Argus / Hector | チャル / ハイオ / ホヨ / ジョンガ / アーガス / ヘクター |

記載のない固有名詞は **原語の発音に近いカタカナ**にし、同じバッチ内では表記を統一する。
ハイフン入りの名前は `＝` でつなぐ (`Ke-Li` → `ケ＝リ`)。

### 5-9. 肩書き

General 将軍 / Commander 司令官 / Commander-In-Chief 最高司令官 / Captain, Capt. 大尉 / Lieutenant, Lt. 中尉 /
Sergeant, Sgt. 軍曹 / King 王 / Queen 女王 / Lord 卿 (人名の後) / Dame 夫人 / Brother 修道士 / Father 神父 /
Elder 長老 / Tracker 追跡者

## 6. 呪文名 (4000〜4101) — 確定済み、この表記を使う

Wiz6 日本語化の意味訳に合わせる。Wiz6 に無い Wiz7 の呪文 (念の炎・病気治療・人物探知など) は同じ流儀で決めた
(`tools/apply_wiz6.py` の WIZ7_ONLY_SPELLS)。

| 原文 | 訳語 | 原文 | 訳語 |
|---|---|---|---|
| ENERGY BLAST | エネルギー弾 | BLINDING FLASH | 閃光 |
| PSIONIC FIRES | 念の炎 | FIREBALL | 火球 |
| FIRE SHIELD | 炎の盾 | DAZZLING LIGHTS | まばゆい光 |
| FIRE BOMB | 火炎弾 | LIGHTNING | 稲妻 |
| PRISMIC MISSILE | 虹の矢 | FIRESTORM | 炎の嵐 |
| NUCLEAR BLAST | 核爆発 | CHILLING TOUCH | 凍てつく手 |
| STAMINA | スタミナ | TERROR | 恐怖 |
| WEAKEN | 弱体化 | SLOW | 減速 |
| HASTE | 加速 | CURE PARALYSIS | 麻痺治療 |
| ICE SHIELD | 氷の盾 | RESTFULL | 安息 |
| ICEBALL | 氷球 | PARALYZE | 金縛り |
| SUPERMAN | 超人 | DEEP FREEZE | 氷結 |
| DRAINING CLOUD | 吸精の雲 | CURE DISEASE | 病気治療 |
| POISON | 毒 | MISSILE SHIELD | 矢よけ |
| SHRILL SOUND | 金切り音 | STINK BOMB | 悪臭弾 |
| AIR POCKET | 空気の泡 | SILENCE | 沈黙 |
| POISON GAS | 毒ガス | CURE POISON | 解毒 |
| WHIRLWIND | 旋風 | PURIFY AIR | 空気浄化 |
| DEADLY POISON | 猛毒 | LEVITATE | 浮遊 |
| TOXIC VAPORS | 毒霧 | NOXIOUS FUMES | 毒煙 |
| ASPHYXIATION | 窒息 | DEADLY AIR | 死の大気 |
| DEATH CLOUD | 死の雲 | ACID SPLASH | 酸の飛沫 |
| ITCHING SKIN | かゆみ | ARMOR SHIELD | 防護盾 |
| DIRECTION | 方位 | KNOCK-KNOCK | 解錠 |
| BLADES | 刃の嵐 | ARMORPLATE | 装甲 |
| WEB | 蜘蛛糸 | WHIPPING ROCKS | 石つぶて |
| ACID BOMB | 酸爆弾 | ARMORMELT | 装甲溶解 |
| CRUSH | 圧壊 | CREATE LIFE | 生命創造 |
| CURE STONE | 石化解除 | MENTAL ATTACK | 精神攻撃 |
| SLEEP | 眠り | BLESS | 祝福 |
| CHARM | 魅了 | CURE LESSER CND | 状態回復 |
| DIVINE TRAP | 罠看破 | DETECT SECRET | 秘密探知 |
| IDENTIFY | 識別 | CONFUSION | 混乱 |
| WATCHBELLS | 警鐘 | HOLD MONSTERS | 束縛 |
| MINDREAD | 読心 | SANE MIND | 正気回復 |
| PSIONIC BLAST | 念波 | ILLUSION | 幻影 |
| WIZARD EYE | 千里眼 | SPOOKS | 怪異 |
| DEATH | 即死 | LOCATE OBJECT | 物品探知 |
| MIND FLAY | 精神破壊 | LOCATE PERSON | 人物探知 |
| HEAL WOUNDS | 治癒 | MAKE WOUNDS | 裂傷 |
| MAGIC MISSILE | 魔法の矢 | DISPEL UNDEAD | 不死退散 |
| ENCHANTED BLADE | 魔法剣 | BLINK | 瞬間移動 |
| MAGIC SCREEN | 魔法障壁 | CONJURATION | 召喚 |
| ANTI-MAGIC | 反魔法 | REMOVE CURSE | 解呪 |
| HEALTHFULL | 健全 | LIFESTEAL | 生命吸収 |
| ASTRAL GATE | 星界の門 | ZAP UNDEAD | 不死撃滅 |
| RECHARGE | 再充填 | WORD OF DEATH | 死の言葉 |
| RESURRECTION | 蘇生 | DEATH WISH | 死の願い |
| HOLY WATER | 聖水 | HELPFOOD | 補給食 |
| MAGICFOOD | 魔法食 | MAP | 地図 |
| RESTORE LIFE | 生命回復 | INVISIBILITY | 透明化 |

文中で呪文名が出てきたら同じ表記を使う。

## 7. スキル (5500〜5534) — 確定済み

WAND&DAGGER 杖&短剣 / SWORD 剣 / AXE 斧 / MACE&FLAIL 棍棒&フレイル / POLE&STAFF 長柄武器&棒 / THROWING 投擲 /
SLING スリング / BOWS 弓 / SHIELD 盾 / HANDS&FEET 素手&蹴り / SWIMMING 水泳 / CLIMBING 登攀 / SCOUTING 偵察 /
MUSIC 音楽 / ORATORY 話術 / LEGERDEMAIN 早業 / SKULDUGGERY 盗賊術 / NINJUTSU 忍術 / FIREARMS 火器 /
REFLEXTION 反射神経 / SNAKE SPEED 蛇の速さ / EAGLE EYE 鷹の目 / POWER STRIKE 強打 / MIND CONTROL 精神支配 /
ARTIFACTS 秘宝学 / MYTHOLOGY 神話学 / MAPPING 地図作成 / SCRIBE 写本 / DIPLOMACY 外交 / ALCHEMY 錬金術 /
THEOLOGY 神学 / THEOSOPHY 神智学 / THAUMATURGY 魔術学 / KIRIJUTSU 斬術 / SKILL POINTS 技能ポイント (SKILL は「技能」)

技能の分類: WEAPONRY 武器 / PHYSICAL 肉体 / PERSONAL 個人 / ACADEMIA 学問

## 8. 状態・属性

FIRE 火 / LIGHT 光 / COLD 冷気 / PARALYSIS 麻痺 / AIR 風 / POISON 毒 / MISSILES 飛び道具 / ACID 酸 /
HYPNOSIS 催眠 / PSIONIC 超能力 / LIFEFORCE 生命力 / MAGIC 魔法 / DEATH 死
状態: POISONED 毒 / DISEASED 病気 / PARALYZED 麻痺 / STONED 石化 / ASLEEP 睡眠 / AFRAID 恐怖 /
NAUSEOUS 吐き気 / BLINDED 盲目 / SILENCED 沈黙 / INSANE 狂気 / IRRITATED かゆみ / DEAD 死亡 / ASHES 灰

## 8b. 品物・怪物・人物の名前 (SCENARIO.GLD)

- 訳は `translation/scen_names_ja.tsv`。Wizardry 6 日本語化の名前 (`../Wizardry6/JPN_PATCH/trans/scen_ja.txt`) の流儀にそろえる:
  よく知られた武器はカタカナ (ダガー、ロングソード)、日本の武器は漢字 (脇差、薙刀)、防具や魔法の品は「〜の…」 (鋼の兜、銀の篭手、竜殺し)。
- 長さ: 全角 7 字まで。品物は 1 行全角 5 字までで、長いときは "/" で 2 行に分ける (ショート/ボウ、髭付きの/戦斧)。
- 呪文の名を持つ品物 (巻物・薬) は Wiz6 に訳があればそれ (テラー、解毒薬)、無ければ呪文名 (減速、幻影)。
- 本文で品物に触れるときも、なるべく同じ名前を使う。

## 9. よく出る言い回し

| 原文 | 訳語 |
|---|---|
| party | パーティ / 一行 |
| GP / gold | GP / 金貨 |
| chest | 宝箱 |
| trap | 罠 |
| locked | 鍵がかかっている |
| monster(s) | モンスター / 怪物 |
| CANCEL / EXIT / OK | 取消 / 終了 / ＯＫ |
| YES / NO | はい / いいえ |
| BUY / SELL / GIVE / POOL | 買う / 売る / 渡す / 集める |
| SEARCH / REVIEW / SPELL / USE / OPEN / ORDER / REST / DISK | 調べる / 確認 / 呪文 / 使う / 開ける / 隊列 / 休む / ディスク |
| FIGHT / PARRY / HIDE / BREATH / RUN / MOVE / EQUIP / BACKUP | 戦う / 受け流す / 隠れる / ブレス / 逃げる / 移動 / 装備 / 戻る |
| HIT! / MISSED! / CRITICAL | 命中！ / 外れた！ / 致命傷 |
| level up | レベルが上がった |
| assay / merge / drop / invoke / scribe | 鑑定 / 合わせる / 捨てる / 発動 / 写す |
| "Very well then..." | 「よかろう……」 |
| "Huh?" | 「はあ？」 |
