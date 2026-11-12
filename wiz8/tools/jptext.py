"""Japanese text layout for Wizardry 8.

Every word-wrap routine in Wiz8.exe breaks lines only at U+0020. The mod makes the space glyph
zero-width in all fonts (see mkfont.py) and uses it as an invisible break opportunity:

  * spaces the text really wants to show become U+2002 (EN SPACE, a visible blank glyph);
  * leading/trailing spaces (used when the game concatenates fragments) stay as U+0020 (invisible);
  * a U+0020 is inserted between characters wherever a line may break (kinsoku rules applied).
"""
import re

SPACE_VISIBLE = ' '
BREAK = '\ue000'   # invisible break opportunity (private use, zero-advance glyph, accepted by the hooked word-wrap; maps to CP932 F040 so it survives the NPC text conversion)
JP = re.compile(r'[　-ヿ一-鿿＀-￯]')
FMT = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l)?[sdiuxXcfgeEp%]')

# characters that must not start a line / end a line
NO_START = set('、。,.・:;?!゛゜ー～…‥」』）］｝〉》】〕’”ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ！？：；，．)]}>%(')
NO_END = set('「『（［｛〈《【〔‘“([{<')


def is_wide(c):
    o = ord(c)
    return 0x3000 <= o <= 0x9fff or 0xff00 <= o <= 0xffef or o == 0x2026 or o == 0x2015


def normalize(s):
    """unify punctuation in Japanese strings: full-width ！？, no space after Japanese punctuation."""
    if not JP.search(s):
        return s
    s = re.sub(r'(?<!%)!', '！', s)
    s = s.replace('?', '？')
    s = re.sub(r'([。、！？」』…])[ 　]+(?=[^\s%])', r'\1', s)
    return s


def layout(s, breaks=True):
    """normalized string -> string with visible/invisible spaces and break opportunities."""
    if not s:
        return s
    m = re.match(r'^( *)(.*?)( *)$', s, re.S)
    lead, body, trail = m.groups()
    # protect format specifiers as single units
    units = []
    pos = 0
    for f in FMT.finditer(body):
        units.extend(body[pos:f.start()])
        units.append(f.group())
        pos = f.end()
    units.extend(body[pos:])
    out = []
    for i, u in enumerate(units):
        if u == ' ':
            out.append(SPACE_VISIBLE)
            continue
        if breaks and out and i > 0:
            a, b = units[i - 1], u
            a_last, b_first = a[-1], b[0]
            if (a != ' ' and a_last not in '\n' and b_first != '\n'
                    and (is_wide(a_last) or is_wide(b_first))
                    and b_first not in NO_START and a_last not in NO_END):
                out.append(BREAK)
        out.append(u)
    return lead + ''.join(out) + trail


if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8')
    for t in ['%sは毒を受けた！', 'おやまあ、あんたたちかい！いつ本物の顔を拝めるかと思ってたんだよ。',
              '拡大 (ENTER)', '「反逆と冒涜の罪で指名手配: マーテン」', 'HP 10 回復', ' および ']:
        print(repr(layout(normalize(t))))
