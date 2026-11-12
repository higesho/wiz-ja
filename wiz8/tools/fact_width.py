"""Check that every journal entry (FACT.DBS) fits on its line: the journal draws an entry on one line,
clipped (0x5bdd00): right column 435 px wide starting 5 px in, left column 175 px.
Measures with the Japanese journal font the way the game does (advance = offx + width per glyph)."""
import os, sys, json, struct
from slf import read_slf, BS
from sti import STI
from mkfont import add_japanese
from jptext import normalize, layout

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'
ORIG = list(map(ord, 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_+=|\\{}[]:;"\'<>,.?/ '))
LIMIT = 435 - 5


def main():
    chars = [chr(c) for c in struct.unpack('<%dH' % (os.path.getsize(GAME + r'\wiz8jp.chr') // 2),
                                           open(GAME + r'\wiz8jp.chr', 'rb').read())]
    h, ents = read_slf(GAME + r'\Data\DATA.SLF')
    f = open(GAME + r'\Data\DATA.SLF', 'rb')
    for n, o, l, st in ents:
        if n.upper().endswith('JOURNAL_FONT.STI'):
            f.seek(o)
            s = STI.load(add_japanese(f.read(l), chars, {}, n)[0])
    table = ORIG + [0] * (252 - len(ORIG)) + [ord(c) for c in chars]
    adv = {}
    for i, c in enumerate(table):
        if c and c not in adv and i < len(s.subs):
            g = s.subs[i]
            adv[c] = g['offx'] + g['w']

    def width(t):
        return sum(adv.get(ord(c), adv.get(ord('A'), 8)) for c in t)

    ja = json.load(open(os.path.join(GAME, 'JPMod', 'work', 'ja', 'dbs_fact.json'), encoding='utf-8'))
    bad = 0
    for k, v in ja.items():
        w = width(layout(normalize(v)))
        if w > LIMIT:
            bad += 1
            print('%4d  %s  %s' % (w, k.rsplit('@', 1)[1], v))
    print(len(ja), 'entries,', bad, 'too wide (limit %d px)' % LIMIT)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
