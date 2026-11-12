"""Pre-release touch-ups (2026-11-12): spelling unification, a few wording fixes so keyword
annotations land, and the manual keyword table work/kw67/manual.json (lid|KEYWORD -> term or null)."""
import os, json, glob, re, sys

JP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JA = os.path.join(JP, 'work', 'ja')

# spelling unification (glossary: Hogar=ホーガー, Bayjin=ベイジン, Brekek=ブレケク, Shaman=呪術師)
SPELL = [('ホガール', 'ホーガー'), ('ホガー', 'ホーガー'), ('バイジン', 'ベイジン'),
         ('ブレキーク', 'ブレケク'), ('シャーマン', '呪術師')]

# per-line wording: id -> (old, new)
EDIT = {
    'NPC SCRIPTS/NPC_TRAMAIN.NSF@1ddd': ('ドミナス一の伝説', 'ドミナスで一番の伝説'),
    'NPC SCRIPTS/NPC_HELI.NSF@4c7a': ('飲んだくれにもいろいろあるけど、あんたは超大酒飲みだね。',
                                      '飲んだくれにもいろいろあるけどね、マイルズ、あんたは超大酒飲みだよ。'),
    'NPC SCRIPTS/NPC_LORRAC.NSF@1c0f': ('親切な窓口係', '親切な銀行の窓口係'),
    'NPC SCRIPTS/NPC_MILANO.NSF@2f9c': ('……サヴァントも含めてな', '……ダークサヴァントも含めてな'),
    'NPC SCRIPTS/NPC_MYLES.NSF@8c24': ('港か……', 'マリーナか……'),
    'NPC SCRIPTS/NPC_VI.NSF@4bc0': ('あの男がHLLを', 'このマーテンって男がHLLを'),
}

# keyword annotations the table could not place: term to put "(KEYWORD)" after, or None = leave out
# (pun / different sense / already written in English / debug NPC)
S = 'NPC SCRIPTS/'
MANUAL = {
    'NPC_ANTONE.NSF@1b|ARMOR': '武具', 'NPC_ANTONE.NSF@82ba|ARMOR': '武具', 'NPC_ANTONE.NSF@8ba8|ARMOR': '武具',
    'NPC_BALBRAK.NSF@62bb|SPECIAL TACTICAL FORCES': None,
    'NPC_BELA.NSF@2eb0|PEN': '使った筆',
    'NPC_BILDUBLU.NSF@5b2|BAR': 'この店', 'NPC_BILDUBLU.NSF@fed|BAR': '店に', 'NPC_BILDUBLU.NSF@51d8|BAR': '俺の店',
    'NPC_BILDUBLU.NSF@548d|BAR': '俺の店', 'NPC_HELI.NSF@39c|BAR': '店', 'NPC_HELI.NSF@71e|BAR': 'カウンター',
    'NPC_BILDUBLU.NSF@23b9|OPERATION': '作戦', 'NPC_KUNAR.NSF@28a8|OPERATION': None,
    'NPC_BRAFFIT.NSF@493e|DOMINA': 'ドミナ',
    'NPC_CHIEFGARI.NSF@25e8|RATS': 'ラットキン', 'NPC_CHIEFGARI.NSF@264f|RATS': 'ラットキン',
    'NPC_CHIEFGARI.NSF@4539|RATS': 'ラットキン', 'NPC_CHIEFGARI.NSF@464e|RATS': 'ラットキン',
    'NPC_CROCK.NSF@1286|SPIDERS': None,
    'NPC_CROCK.NSF@1925|CIRCLE': '宇宙の環', 'NPC_LORD_BRAFFIT.NSF@1679|CIRCLE': '宇宙の環',
    'NPC_ZANT.NSF@51cf|CIRCLE': '宇宙の環', 'NPC_ZANT.NSF@aef3|CIRCLE': '宇宙の環',
    'NPC_DRAZIC.NSF@13ec|WAR': '戦い', 'NPC_KING.NSF@3510|WAR': '戦い', 'NPC_MADRAS.NSF@42c5|WAR': '戦い',
    'NPC_RODAN.NSF@2db3|WAR': '戦い', 'NPC_YAMIR.NSF@7a41|WAR': '戦い', 'NPC_YAMIR.NSF@8156|WAR': '戦い',
    'NPC_DRAZIC.NSF@14f0|PEACE': '平和', 'NPC_DRAZIC.NSF@28bb|PEACCCE': None,
    'NPC_KUNAR.NSF@11b6|PEACE': '平和', 'NPC_SCREGHOLO.NSF@2d95|PEACE': '平和のうちに',
    'NPC_EWAXX.NSF@1414|MARTEN': None,
    'NPC_FERRO.NSF@b40|FORGE': None, 'NPC_ORBITTRACK.NSF@345e|FORGE': 'フォージ',
    'NPC_FUZZFAS.NSF@836|LIFE': None, 'NPC_KING.NSF@17c4|LIFE': None, 'NPC_QUEEN.NSF@23ee|LIFE': None,
    'NPC_QUEEN.NSF@48bd|LIFE': None, 'NPC_SHAMAN.NSF@4dfa|LIFE': None, 'NPC_URQ.NSF@122f|LIFE': None,
    'NPC_FUZZFAS.NSF@3b98|TRYNNIE': None,
    'NPC_FUZZFAS.NSF@47f4|SHAMAN': '呪術師', 'NPC_FUZZFAS.NSF@5a15|SHAMAN': '呪術師',
    'NPC_MYLES.NSF@6d74|MYLES': None,
    'NPC_HELI.NSF@8bb7|LORD': 'ブラフィット卿', 'NPC_QUEEN.NSF@5252|LORD': '新たな主',
    'NPC_SCREG.NSF@2482|LORD': '卿', 'NPC_ZANT.NSF@1963|LORD': '運命の主',
    'NPC_KUNAR.NSF@4784|CORD': None,
    'NPC_KUNAR.NSF@4c3f|BOMB': '爆撃',
    'NPC_LORRAC.NSF@328d|BANK': None, 'NPC_YAMIR.NSF@319b|BANK': None,
    'NPC_MADRAS.NSF@54b5|GENERAL': None,
    'NPC_MEDUNA.NSF@466e|MISSION': 'ミッション',
    'NPC_MILANO.NSF@5690|GOD': None,
    "NPC_MYLES.NSF@2910|ANTONE'S": None, "NPC_MYLES.NSF@9a51|ANNA'S": None,
    'NPC_MYLES.NSF@5052|RESCUE': '助ける',
    'NPC_MYLES.NSF@7217|RESCUE-FOR-PROFIT': '救出で金を稼ぐ仕事',
    'NPC_PLAYDAY.NSF@2f73|YAMIR': None,
    'NPC_RUBBLE.NSF@3469|PASS': '新兵証', 'NPC_RUBBLE.NSF@59fa|SUITS': 'スーツ',
    'NPC_SADOK.NSF@37fd|PAINTING': 'あの絵', 'NPC_SADOK.NSF@5fa8|PAINTING': 'マーテンの絵',
    'NPC_SADOK.NSF@58e6|NARGISST MONITOR': 'ナーギストのモニター',
    'NPC_SCREG.NSF@15d|FACILITY': '施設', 'NPC_SCREG.NSF@389a|FACILITY': '施設',
    'NPC_SCREG.NSF@4808|ALLIANCE': 'ムーク連合', 'NPC_SCREG.NSF@5ac1|ALLIANCE': 'ムーク連合',
    'NPC_SCREG.NSF@4b70|EXPEDITION': '探査',
    'NPC_SETFACT.NSF@750|SEX': None,
    'NPC_TANTRIS.NSF@433e|MACHINE': '装置',
    'NPC_YAMIR.NSF@dd6|FORT': None,
    'NPC_ZANT.NSF@16ab|PEAK': 'この峰',
    'NPC_ZANT.NSF@580e|DOMINUS': None,   # already inside (DESSSTINAE DOMINUS)
    'NPC_ZANT.NSF@8355|LURE': 'おびき寄せ', 'NPC_ZANT.NSF@ef17|LURE': 'おびき寄せ',
    'NPC_ZANT.NSF@8b40|RAVEN': 'レイヴン', 'NPC_ZANT.NSF@8cb1|RAVEN': 'レイヴン',
}


def main():
    changed = 0
    for f in sorted(glob.glob(os.path.join(JA, '*.json'))):
        if os.path.basename(f).startswith('_'):
            continue
        d = json.load(open(f, encoding='utf-8'))
        if not isinstance(d, dict):
            continue
        n = 0
        for k, v in d.items():
            if k.startswith('_') or not isinstance(v, str):
                continue
            w = v
            for a, b in SPELL:
                w = w.replace(a, b)
            if k in EDIT:
                a, b = EDIT[k]
                if a in w:
                    w = w.replace(a, b)
                elif b not in w:
                    print('EDIT not applied', k)
            if w != v:
                d[k] = w; n += 1
        if n:
            json.dump(d, open(f, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
            print(os.path.basename(f), n); changed += n
    p = os.path.join(JP, 'tools', 'fact_ja.py')
    s = open(p, encoding='utf-8').read()
    t = s
    for a, b in SPELL:
        t = t.replace(a, b)
    if t != s:
        open(p, 'w', encoding='utf-8').write(t); print('fact_ja.py updated')
    json.dump({S + k: v for k, v in MANUAL.items()}, open(os.path.join(JP, 'work', 'kw67', 'manual.json'), 'w', encoding='utf-8'),
              ensure_ascii=False, indent=0)
    print(changed, 'strings changed;', len(MANUAL), 'manual keyword entries')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
