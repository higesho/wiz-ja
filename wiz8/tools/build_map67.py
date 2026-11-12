"""Build the Wiz6/7 term-alignment map  -> work/terms67/map.tsv

columns: old_ja, new_ja, en_regex, scope
  scope all   : replace in every line
        word  : replace only in lines whose English matches en_regex (re.search, case-sensitive)
        exact : replace only in lines whose stripped English fully matches en_regex
        keep  : protect old_ja from shorter rules (new_ja == old_ja)
"""
import os, re, sys, json, glob
sys.path.insert(0, os.path.dirname(__file__))
import compare67 as c

OUT = os.path.join(c.OUT, 'map.tsv')

# ---- proper nouns / world terms (Wiz7 glossary)
ALL = [
    ('コズミック・ロード', '宇宙の君主'), ('コズミック・サークル', '宇宙の環'),
    ('トラング', 'トラン'), ('ムンク', 'マンク'), ('ヒガルディ', 'ハイガーディ'),
    ('アストラル・ドミネ', 'アストラル・ドミナエ'), ('デスティネ・ドミナス', 'デスティナエ・ドミナス'),
    ('バーローネ', 'バルローネ'), ('ラズーカ', 'ラズカ'), ('黒き船', '黒い船'),
    ('失われたガーディア', '失われしガーディア'), ('ジャン・エット', 'ジャン＝エット'),
    ('アル・チャイルド', 'アル＝チャイルド'), ('アル・アサイズ', 'アル＝アサイズ'),
    ('アル・マデウス', 'アル＝マデウス'), ('アル・ルア', 'アル＝ルア'),
    ('アル・セデクサス', 'アル＝セデクサス'), ('アル・アドリアン', 'アル＝アドリアン'),
    ('ヒューマン', '人間'),
]
KEEP = ['ロードデー', 'レンジャー・ロード', '雷撃', 'ロードハウス', 'プロローグ', 'エピローグ', 'ダウンロード', 'ロード画面']
WORD = [
    # world
    # Cosmic Forge (魔筆) is pending: Wiz6/7 use 宇宙の鋳造所, but Wiz8 treats it as a hand-held pen
    ('シャーマン', '呪術師', r'Shaman'),
    # classes
    ('ファイター', '戦士', r'\bFighters?\b'), ('ロード', '君主', r'\bLords?\b'),
    ('サムライ', '侍', r'\bSamurai'), ('ニンジャ', '忍者', r'\bNinjas?\b'),
    ('モンク', '修道僧', r'\bMonks?\b'), ('ローグ', '盗賊', r'\bRogues?\b'),
    ('バード', '吟遊詩人', r'\bBards?\b'), ('プリースト', '僧侶', r'\bPriests?\b'),
    ('アルケミスト', '錬金術師', r'\bAlchemists?\b'), ('ビショップ', '司教', r'\bBishops?\b'),
    ('サイオニック能力', '超能力', r'Psionic Powers'), ('サイオニクス', '超能力', r'\bPsionics\b'),
    ('サイオニック', '超能力者', r'Psionics? (have|and)|Mages, Psionics|excellent Psionics|Mook are Psionic'),
    ('サイオニック', '超能力', r'\bPsionics?\b'),
    ('メイジ', '魔術師', r'\bMages?\b'),
    # stats
    ('筋力', '力', r'\b(Strength|STR|Str)\b'), ('知力', '知恵', r'\b(Intelligence|INT|Int)\b'),
    ('信仰心', '信仰', r'\b(Piety|PIE|Pie)\b'), ('生命力', '体力', r'\b(Vitality|VIT|Vit)\b'),
    ('生命', '体力', r'\b(VIT|Vit)\b'), ('器用さ', '器用', r'\b(Dexterity|DEX|Dex)\b'),
    ('素早さ', '速さ', r'\b(Speed|SPD|Spd)\b'),
    ('ゴールド', '金貨', r'\b([Gg]old|GOLD|GP)\b'),
    # conditions
    ('錯乱', '狂気', r'[Ii]nsan'), ('鈍足', '減速', r'\bSlow'),
    # skills
    ('メイス&フレイル', '棍棒&フレイル', r'Mace & Flail'), ('強撃', '強打', r'Power Strike'),
    ('蛇の俊敏', '蛇の速さ', r'Snake Speed'),
    # spells (old name -> Wiz6/7 name), only where the English names the spell
    ('傷の治癒', '治癒', r'Heal Wounds'), ('傷の創出', '裂傷', r'Make Wounds'),
    ('肌のかゆみ', 'かゆみ', r'Itching Skin'), ('麻痺', '金縛り', r'\bParalyze\b'),
    ('睡眠', '眠り', r'\bSleep\b'), ('軽度状態治癒', '状態回復', r'Cure Lesser Cond'),
    ('秘密察知', '秘密探知', r'Detect Secrets?'), ('神聖の罠', '罠看破', r'Divine Trap'),
    ('付与の刃', '魔法剣', r'Enchanted Blade'), ('鑑定', '識別', r'Identify Item'),
    ('飛道具防壁', '矢よけ', r'Missile Shield'), ('甲高い音', '金切り音', r'Shrill Sound'),
    ('装甲化', '装甲', r'Armorplate'), ('麻痺治癒', '麻痺治療', r'Cure Paralysis'),
    ('毒治癒', '解毒', r'Cure Poison'), ('ノックノック', '解錠', r'Knock[- ]Knock'),
    ('魔法の幕', '魔法障壁', r'Magic Screen'), ('有毒の煙', '毒煙', r'Noxious Fumes'),
    ('サイオニックの火', '念の炎', r'Psionic Fire'), ('鞭打つ岩', '石つぶて', r'Whipping Rocks'),
    ('酸の爆弾', '酸爆弾', r'Acid Bomb'), ('装甲融解', '装甲溶解', r'Armormelt'),
    ('病気治癒', '病気治療', r'Cure Disease'), ('火炎爆弾', '火炎弾', r'Fire Bomb'),
    ('呪い解除', '解呪', r'Remove Curse'), ('スーパーマン', '超人', r'Superman'),
    ('サイオニック爆破', '念波', r'Psionic Blast'), ('風の浄化', '空気浄化', r'Purify Air'),
    ('正気', '正気回復', r'Sane Mind'), ('吸収の雲', '吸精の雲', r'Draining Cloud'),
    ('火炎嵐', '炎の嵐', r'Firestorm'), ('雷', '稲妻', r'(Book of |^)Lightning$|Lightning spell'),
    ('死への願望', '死の願い', r'Death Wish'), ('精神剥ぎ', '精神破壊', r'Mind Flay'),
    ('毒の投針', '毒矢', r'Poison Darts'),
    # item halves
    (' (U)', '（上）', r'\(U\)'), ('(U)', '（上）', r'\(U\)'),
    (' (L)', '（下）', r'\(L\)'), ('(L)', '（下）', r'\(L\)'),
]
EXACT = [
    ('サイオニック', '超能力者', 'Psionics?'), ('メイジ', '魔術師', 'Wizard'), ('戦士', '武人', 'Warrior'),
    # class abbreviation, skill groups
    ('拳', '修', 'MON'), ('学術', '学問', 'Academic'), ('魔術', '魔法', 'Magic'),
    ('武器術', '武器', 'Weaponry'), ('身体', '肉体', 'Physical'),
    ('エキスパート', '上級', 'Expert'), ('専門', '上級', 'Expert'),
    ('秘宝', '秘宝学', 'Artifacts'), ('神話', '神話学', 'Mythology'), ('長柄', '長柄武器', 'Polearm'),
    # rank titles shared with Wiz6/7
    ('軍閥', '武将', 'Warlord'), ('シュヴァリエ', '勲爵士', 'Chevalier'),
    ('パラディン', '聖騎士', 'Paladin'), ('十字軍士', '聖戦士', 'Crusader'), ('君主', '帝王', 'Monarch'),
    ('オリンピアン', 'オリンポス人', 'Olympian'), ('猟師', '森番', 'Woodsman'), ('弓使い', '射手', 'Archer'),
    ('道案内', '探索者', 'Pathfinder'), ('遊撃騎兵', '先駆け', 'Outrider'), ('刃の使い手', '刀使い', 'Bladesman'),
    ('術師', '修験者', 'Shugenja'), ('長老', '頭領', 'Grandfather'), ('詐欺師', 'ペテン師', 'Trickster'),
    ('吟遊詩人', '楽士', 'Minstrel'), ('聖歌隊長', '歌い手', 'Cantor'), ('治療師', '癒し手', 'Healer'),
    ('副司祭', '助祭', 'Curate'), ('聖人', '聖者', 'Saint'), ('托鉢修道士', '托鉢僧', 'Friar'),
    ('副牧師', '教区司祭', 'Vicar'), ('参事会員', '聖堂司祭', 'Canon'), ('判事', '司法官', 'Magistrate'),
    ('教区司祭', '教区長', 'Diocesan'), ('託宣者', '神託者', 'Oracle'), ('預言者', '予言者', 'Prophet'),
    ('奇術師', '召喚師', 'Conjurer'), ('邪術師', '魔道士', 'Warlock'),
    # monsters
    ('蝙蝠', 'コウモリ', 'Bats?'), ('王ガニ', 'タラバガニ', 'King Crabs?'), ('下級悪魔', '下位悪魔', 'Lesser Demons?'),
    ('狂いネズミ', '狂乱ネズミ', 'Rabid Rats?'), ('霊', 'スピリット', 'Spirits?'),
]
ITEM_SKIP = {'Flail'}
ITEM_PICK = {'Muramasa Blade': '村正', 'Maenad\'s Lance': 'マエナドの槍', 'Vulcan Hammer': 'ヴァルカンの槌',
             'Blade Cuisinart': 'クイジナート刃', 'Bo': '棒', 'Cure Poison': '解毒'}


def item_rows():
    ref = c.load_refs()
    rows = []
    for cat, en, ja in c.load_ours():
        if cat != 'アイテム' or en in ITEM_SKIP:
            continue
        k = c.key(en)
        cands = ref.get(k) or []
        pref = [j for s, e, j in cands if s == '6品'] or [j for s, e, j in cands if s in ('7品', '7UI')] \
            or [j for s, e, j in cands if s == '6UI']
        pref = [re.sub(r'\s*\([A-Z][A-Z .\-]*\)', '', p) for p in pref]
        if not pref:
            continue
        new = ITEM_PICK.get(en, pref[0])
        new = new.replace('（上）', '（上）')
        # our ja already carries (U)/(L) -> handled by the WORD rules; compare on the base
        base_old = re.sub(r'\s*\((U|L)\)$', '', ja)
        base_new = re.sub(r'\s*[（(](上|下)[）)]$', '', new)
        if base_old and base_new and base_old != base_new and not re.search(r'[A-Za-z]{2}', base_new):
            rx = r'\b' + re.escape(re.sub(r'\s*\((U|L)\)$', '', en)) + r'(?: ?\((?:U|L)\))?(?![A-Za-z])'
            rows.append((base_old, base_new, rx, 'item'))
    return sorted(set(rows))


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    rows = [(a, b, '', 'all') for a, b in ALL] + [(k, k, '', 'keep') for k in KEEP]
    rows += [(a, b, r, 'word') for a, b, r in WORD]
    rows += [(a, b, r, 'exact') for a, b, r in EXACT]
    rows += item_rows()
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write('# old\tnew\ten_regex\tscope\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    print(len(rows), 'rules ->', OUT)


if __name__ == '__main__':
    main()
