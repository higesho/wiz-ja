"""Build wiz8jp.dat (edit scripts that turn the player's own game files into the Japanese ones, applied
in memory by the hook; see patchdat.py) and wiz8jp.chr. --slf also writes Patches/PATCH.002 (development
only: it holds game data).

The glyph set is every character used by the translations (work/ja/*.json) that the game's own
252-symbol table does not have. NSF/MSG/DBS text is written back by import_text.py.
usage: python build_patch.py [--no-strings] [--no-text] [--slf]
"""
import os, re, sys, json, glob, struct, time
from slf import read_slf, read_file, write_slf, BS
from strdat import decode, encode
from mkfont import add_japanese
from glyphpack import make_pack
import hashlib, zlib
from jptext import normalize, layout
from import_text import build as import_text
from keywords import annotate
import patchdat

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'
ROOT = os.path.join(GAME, 'JPMod')
DATA = os.path.join(GAME, 'Data', 'DATA.SLF')
PATCH1 = os.path.join(GAME, 'Patches', 'PATCH.001')
OUT_PATCH = os.path.join(GAME, 'Patches', 'PATCH.002')
OUT_CHR = os.path.join(GAME, 'wiz8jp.chr')
OUT_DAT = os.path.join(GAME, 'wiz8jp.dat')

ORIG_TABLE = set(map(ord, 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_+=|\\{}[]:;"\'<>,.?/ ')) \
    | set(range(0xc0, 0x100)) | {0xbf, 0xa1, 0xfff0, 0xfff1, 0xfff2, 0xfff3, 0xfff4}
JP = re.compile(r'[\u3000-\u30ff\u4e00-\u9fff\uff00-\uffef]')


def load_translations():
    tr = {}
    for cp in glob.glob(os.path.join(ROOT, 'work', 'chunks', '*.json')):
        if os.path.basename(cp).startswith('_'):
            continue
        jp = os.path.join(ROOT, 'work', 'ja', os.path.basename(cp))
        if not os.path.exists(jp):
            continue
        c = json.load(open(cp, encoding='utf-8'))
        t = json.load(open(jp, encoding='utf-8'))
        for l in c['lines']:
            v = t.get(l['id'])
            if isinstance(v, str):
                # restore the leading/trailing spaces of the English (used for concatenation)
                v = v.strip(' ')
                lead = len(l['en']) - len(l['en'].lstrip(' '))
                trail = len(l['en']) - len(l['en'].rstrip(' '))
                v = ' ' * lead + v + ' ' * trail
                # fixed-size name fields get no extra break spaces
                names = l['id'].startswith('DATABASES/') and 'DESC' not in l['id'] and 'FACT' not in l['id']
                # conversation keywords are typed in English: 「訳語(KEYWORD)」
                v = annotate(l['id'], l['en'], v)
                tr[l['id']] = layout(normalize(v), breaks=not names)
    return tr


def is_font(up):
    # PALETTE*.STI are colour tables, not fonts
    return up.endswith('.STI') and ('FONTS' + BS in up or up.endswith('_FONT.STI')) and not up.startswith('FONTS' + BS + 'PALETTE')


def fanpatch_font_files():
    """Fan Patch 1.28 (Wiz8_v128.exe) reads most fonts from its own Data/FanPatch.Dat (Cyrillic in the
    upper half, some fonts the Steam data does not have). Glyph packs are made for those too, so their
    colours match. The fan patch folder: environment variable WIZ8_FANPATCH (default L:/Wiz8FP)."""
    path = os.path.join(os.environ.get('WIZ8_FANPATCH', r'L:\Wiz8FP'), 'Data', 'FanPatch.Dat')
    if not os.path.exists(path):
        print('  (no %s: no Fan Patch fonts)' % path)
        return []
    out = []
    f = open(path, 'rb')
    for n, o, l, st in read_slf(path)[1]:
        if n.upper().startswith('DATA' + BS) and is_font(n.upper()[5:]):
            f.seek(o)
            out.append((n[5:], f.read(l)))
    return out


def fanpatch_nsf(tr):
    """Fan Patch 1.28 replaces some NPC scripts with fixed copies (Data\ENG.SLF: NPC_ZANT.NSF). Their text is the
    same, in the same order, so the translation is applied by position. -> [(name, original, new)]"""
    from textdb import scan_prefixed
    from import_text import rebuild, check_prefixed, Report, RAW
    path = os.path.join(os.environ.get('WIZ8_FANPATCH', r'L:\Wiz8FP'), 'Data', 'ENG.SLF')
    if not os.path.exists(path):
        return []
    out = []
    for n, o, l, st in read_slf(path)[1]:
        up = n.upper()
        if not (up.startswith('DATA' + BS + 'NPC SCRIPTS' + BS) and up.endswith('.NSF')):
            continue
        n = n[5:]
        fp = read_file(path, 'DATA' + BS + n)
        base = os.path.basename(n)
        steam = open(os.path.join(RAW, 'NPC SCRIPTS', base), 'rb').read()
        sa, sb = scan_prefixed(steam, 2, False), scan_prefixed(fp, 2, False)
        if [t for _, t in sa] != [t for _, t in sb]:
            print('  fan patch %s: the text differs from the Steam file, left in English' % n)
            continue
        items = {}
        for (so, en), (fo, _) in zip(sa, sb):
            v = tr.get('NPC SCRIPTS/%s@%x' % (base, so))
            if v is not None and check_prefixed(fp, fo, 2, False, en):
                items[fo] = v
        rep = Report()
        out.append((n, fp, rebuild('nsf', fp, items, rep, n)))
        print('  fan patch %s: %d of %d strings' % (n, len(items), len(sa)))
    return out


def main():
    t0 = time.time()
    tr = load_translations()
    chars = sorted({c for s in tr.values() for c in s if ord(c) not in ORIG_TABLE and ord(c) >= 0x80}
                   | {'\u200b'})   # old break character: message logs in saves made before the switch to U+E000
    print(len(tr), 'translated lines;', len(chars), 'extra glyphs')
    files = {}
    fonts = {}           # the Japanese fonts (only for --slf; wiz8jp.dat has the glyph packs)
    if '--no-strings' not in sys.argv:
        strings = decode(read_file(PATCH1, 'STRINGS' + BS + 'STRINGDATA.DAT'))
        n = 0
        for i in range(len(strings)):
            v = tr.get('STRINGDATA#%d' % i)
            if v is not None:
                strings[i] = v + '\0'; n += 1
        files['STRINGS' + BS + 'STRINGDATA.DAT'] = encode(strings)
        print('STRINGDATA:', n, 'strings replaced')
    if '--no-text' not in sys.argv:
        tf, rep = import_text(tr)
        files.update(tf)
        print('NSF/MSG/DBS: %d files, %d strings replaced, %d warnings' % (len(tf), rep.replaced, len(rep.warn)))
        for w in rep.warn:
            print('   warning:', w)
    h, ents = read_slf(DATA)
    f = open(DATA, 'rb')
    cache = {}
    # fonts: the hook appends a Japanese glyph pack to whatever version of the font the game reads
    # (glyphpack.py); one pack per distinct set of glyphs, shared by the fonts that use it
    packs, glyphs = {}, []

    def add_font(n, orig, where):
        pack, new, info = make_pack(orig, chars, cache, n)
        pid = hashlib.md5(pack).hexdigest()[:12]
        packs[pid] = pack
        glyphs.append(('@glyphs' + BS + n, (len(orig), zlib.crc32(orig) & 0xffffffff), ('@pack' + BS + pid).encode('latin1')))
        print('  font %-10s %-36s %7d bytes  pack %s  %s' % (where, n, len(new), pid, info))
        return new

    for n, o, l, st in ents:
        up = n.upper()
        if is_font(up):
            f.seek(o)
            fonts[n] = add_font(n, f.read(l), 'Steam')
    for n, orig in fanpatch_font_files():
        add_font(n, orig, 'FanPatch')
    open(OUT_CHR, 'wb').write(struct.pack('<%dH' % len(chars), *map(ord, chars)))
    if '--slf' in sys.argv:
        # development only: a PATCH.002 holds game data and must not be distributed
        write_slf(OUT_PATCH, 'PATCH.002', 'Data' + BS, dict(files, **fonts))
        print('wrote', OUT_PATCH, os.path.getsize(OUT_PATCH), 'bytes')
    # the originals the game reads: PATCH.001 overrides DATA.SLF
    p1 = {n.upper(): (o, l) for n, o, l, st in read_slf(PATCH1)[1]}
    dd = {n.upper(): (o, l) for n, o, l, st in ents}
    f1 = open(PATCH1, 'rb')
    entries = []
    for n in sorted(files, key=str.lower):
        src, (o, l) = (f1, p1[n.upper()]) if n.upper() in p1 else (f, dd[n.upper()])
        src.seek(o)
        entries.append((n, src.read(l), files[n]))
    entries += fanpatch_nsf(tr)
    entries += glyphs + [('@pack' + BS + pid, None, p) for pid, p in packs.items()]
    entries.sort(key=lambda e: e[0].lower())     # stable: the Steam original stays first
    st = patchdat.write(OUT_DAT, entries)
    print('wrote %s: %d files, %d bytes (copy %d, insert %d); %s %d chars; %.0fs'
          % (OUT_DAT, st['files'], st['size'], st['copy'], st['insert'], OUT_CHR, len(chars), time.time() - t0))


if __name__ == '__main__':
    main()
